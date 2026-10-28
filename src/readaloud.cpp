/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "readaloud.h"

#include "pdfdocument.h"
#include "voiceclient.h"

#include <QAudioOutput>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMediaPlayer>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

constexpr int Prefetch = 3;          // sentences synthesised ahead
constexpr int PagesAhead = 2;        // pages turned into sentences ahead of the reading
constexpr double SlowSpeed = 0.7;    // "Ouvir de novo devagar"

QVariantList polygons(const QList<QRectF> &rects, qreal pad)
{
    QVariantList out;
    for (const QRectF &r : rects) {
        const QRectF p = r.adjusted(-pad, -pad * 0.6, pad, pad * 0.6);
        out << QVariant::fromValue(QPolygonF(p));
    }
    return out;
}

} // namespace

ReadAloud::ReadAloud(PdfDocument *document, QObject *parent)
    : QObject(parent)
    , m_doc(document)
    , m_client(new VoiceClient(this))
    , m_player(new QMediaPlayer(this))
    , m_output(new QAudioOutput(this))
    , m_tick(new QTimer(this))
    , m_gap(new QTimer(this))
{
    m_player->setAudioOutput(m_output);
    // 0.5–2× without the chipmunk effect
    m_player->setPitchCompensation(true);
    m_tick->setInterval(25);
    m_gap->setSingleShot(true);
    connect(m_tick, &QTimer::timeout, this, &ReadAloud::onTick);
    connect(m_gap, &QTimer::timeout, this, [this] {
        if (m_active && m_playing)
            playIndex(m_index + 1);
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia)
            onFinished();
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        setError(tr("The audio could not be played: %1").arg(message));
    });
    connect(m_doc, &PdfDocument::statusChanged, this, &ReadAloud::documentChanged);
    connect(m_client, &VoiceClient::broadcast, this, [this](const QJsonObject &ev) {
        if (ev.value(QStringLiteral("event")).toString() == QLatin1String("job")
            && ev.value(QStringLiteral("id")).toString() == m_jobId) {
            m_job = ev.toVariantMap();
            Q_EMIT exportChanged();
        }
    });
    loadSettings();
}

ReadAloud::~ReadAloud() = default;

// ------------------------------------------------------------------ settings

void ReadAloud::loadSettings()
{
    QSettings s;
    s.beginGroup(QStringLiteral("ReadAloud"));
    m_highlight = s.value(QStringLiteral("highlight"), true).toBool();
    m_follow = s.value(QStringLiteral("follow"), true).toBool();
    m_speed = std::clamp(s.value(QStringLiteral("speed"), 1.0).toDouble(), 0.5, 2.0);
    m_sentencePause = std::clamp(s.value(QStringLiteral("sentencePause"), 1.2).toDouble(), 0.0, 6.0);
    m_bilingual = s.value(QStringLiteral("bilingual"), false).toBool();
    m_native = s.value(QStringLiteral("nativeLanguage"), QLocale().name().left(2)).toString();
}

void ReadAloud::saveSetting(const QString &key, const QVariant &value)
{
    QSettings s;
    s.beginGroup(QStringLiteral("ReadAloud"));
    s.setValue(key, value);
}

bool ReadAloud::engineInstalled() const
{
    return m_client->installed();
}

void ReadAloud::setHighlightEnabled(bool on)
{
    if (m_highlight == on)
        return;
    m_highlight = on;
    saveSetting(QStringLiteral("highlight"), on);
    ++m_revision;
    Q_EMIT settingsChanged();
    Q_EMIT highlightChanged();
}

void ReadAloud::setFollowReading(bool on)
{
    if (m_follow == on)
        return;
    m_follow = on;
    saveSetting(QStringLiteral("follow"), on);
    Q_EMIT settingsChanged();
    if (on)
        follow();
}

double ReadAloud::generationSpeed() const
{
    // generated at the engine's own speed (clearest); steps of 0.1 keep the cache useful
    return std::round(m_speed * 10.0) / 10.0;
}

void ReadAloud::setSpeed(double s)
{
    s = std::clamp(std::round(s * 20.0) / 20.0, 0.5, 2.0);
    if (qFuzzyCompare(s, m_speed))
        return;
    m_speed = s;
    saveSetting(QStringLiteral("speed"), s);
    // the sentence playing now changes at once (pitch kept); the next ones are made at the new speed
    if (m_audio.contains(m_index) && !m_audio[m_index].file.isEmpty())
        m_player->setPlaybackRate(m_speed / m_audio[m_index].speed);
    for (auto it = m_audio.begin(); it != m_audio.end();) {
        if (it.key() > m_index) {
            if (it->request)
                m_client->cancelReplies(it->request);
            it = m_audio.erase(it);
        } else {
            ++it;
        }
    }
    Q_EMIT settingsChanged();
    if (m_active)
        prefetch();
}

void ReadAloud::setVoiceId(const QString &id)
{
    if (id == m_voice || id.isEmpty())
        return;
    m_voice = id;
    saveSetting(QStringLiteral("voice-") + m_language, id);
    const int keep = m_index;
    m_audio.clear();
    Q_EMIT settingsChanged();
    if (m_active && keep >= 0) {
        m_player->stop();
        playIndex(keep);
    }
}

void ReadAloud::setLanguage(const QString &lang)
{
    if (lang == m_language || lang.isEmpty())
        return;
    m_language = lang;
    QSettings s;
    s.beginGroup(QStringLiteral("ReadAloud"));
    m_voice = s.value(QStringLiteral("voice-") + lang, defaultVoice(lang)).toString();
    m_audio.clear();
    m_translations.clear();
    Q_EMIT settingsChanged();
    Q_EMIT translationChanged();
}

void ReadAloud::setLearning(bool on)
{
    if (m_learning == on)
        return;
    m_learning = on;
    Q_EMIT settingsChanged();
    if (on && m_bilingual)
        requestTranslation(m_index);
}

void ReadAloud::setSentencePause(double s)
{
    s = std::clamp(s, 0.0, 6.0);
    if (qFuzzyCompare(s + 1, m_sentencePause + 1))
        return;
    m_sentencePause = s;
    saveSetting(QStringLiteral("sentencePause"), s);
    Q_EMIT settingsChanged();
}

void ReadAloud::setBilingual(bool on)
{
    if (m_bilingual == on)
        return;
    m_bilingual = on;
    saveSetting(QStringLiteral("bilingual"), on);
    Q_EMIT settingsChanged();
    if (on)
        requestTranslation(m_index);
}

void ReadAloud::setNativeLanguage(const QString &lang)
{
    if (lang == m_native || lang.isEmpty())
        return;
    m_native = lang;
    saveSetting(QStringLiteral("nativeLanguage"), lang);
    m_translations.clear();
    Q_EMIT settingsChanged();
    Q_EMIT translationChanged();
}

void ReadAloud::setError(const QString &e)
{
    if (e == m_error)
        return;
    m_error = e;
    if (e.isEmpty())
        m_errorCode.clear();
    Q_EMIT stateChanged();
}

QString ReadAloud::defaultVoice(const QString &lang) const
{
    QString fallback;
    for (const QVariant &v : m_voices) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("language")).toString() != lang || !m.value(QStringLiteral("usable"), true).toBool())
            continue;
        const QString id = m.value(QStringLiteral("id")).toString();
        if (id.startsWith(QLatin1String("piper:")))
            return id;   // natural and instant: the default
        if (fallback.isEmpty())
            fallback = id;
    }
    if (!fallback.isEmpty())
        return fallback;
    return lang == QLatin1String("pt") ? QStringLiteral("piper:pt_BR-faber-medium") : QStringLiteral("espeak:") + (lang == QLatin1String("en") ? QStringLiteral("en-us") : lang);
}

void ReadAloud::refreshVoices()
{
    m_client->call(QStringLiteral("voices.list"), {}, [this](const QJsonObject &r) {
        if (!r.value(QStringLiteral("ok")).toBool())
            return;
        m_voices = r.value(QStringLiteral("voices")).toArray().toVariantList();
        Q_EMIT voicesChanged();
        if (m_voice.isEmpty() || m_voice == defaultVoice(m_language)) {
            QSettings s;
            s.beginGroup(QStringLiteral("ReadAloud"));
            const QString v = s.value(QStringLiteral("voice-") + m_language, defaultVoice(m_language)).toString();
            if (v != m_voice) {
                m_voice = v;
                Q_EMIT settingsChanged();
            }
        }
    });
}

// ------------------------------------------------------------------ the document

void ReadAloud::documentChanged()
{
    if (m_active)
        stop();
    reset();
}

void ReadAloud::reset()
{
    m_sentences.clear();
    m_audio.clear();
    m_translations.clear();
    m_translating.clear();
    m_carry = ReadingText::Sentence();
    m_furniture = ReadingText::Furniture();
    m_firstPage = m_nextPage = 0;
    m_paragraph = 0;
    m_index = -1;
    m_word = -1;
    ++m_revision;
    Q_EMIT positionChanged();
    Q_EMIT highlightChanged();
}

QString ReadAloud::detectLanguage()
{
    const QString meta = m_doc->language().left(2).toLower();
    if (!meta.isEmpty())
        return meta;
    // a few pages of text: the language with more of its common words
    const auto snap = m_doc->snapshot();
    if (!snap)
        return QStringLiteral("pt");
    static const QHash<QString, QStringList> common = {
        {QStringLiteral("pt"), {QStringLiteral("de"), QStringLiteral("que"), QStringLiteral("não"), QStringLiteral("uma"), QStringLiteral("para"), QStringLiteral("com"), QStringLiteral("os"), QStringLiteral("é"), QStringLiteral("no"), QStringLiteral("da")}},
        {QStringLiteral("en"), {QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("of"), QStringLiteral("to"), QStringLiteral("is"), QStringLiteral("that"), QStringLiteral("with"), QStringLiteral("for"), QStringLiteral("it"), QStringLiteral("was")}},
        {QStringLiteral("es"), {QStringLiteral("el"), QStringLiteral("los"), QStringLiteral("las"), QStringLiteral("que"), QStringLiteral("por"), QStringLiteral("una"), QStringLiteral("con"), QStringLiteral("para"), QStringLiteral("es"), QStringLiteral("del")}},
        {QStringLiteral("fr"), {QStringLiteral("le"), QStringLiteral("les"), QStringLiteral("des"), QStringLiteral("est"), QStringLiteral("une"), QStringLiteral("pour"), QStringLiteral("dans"), QStringLiteral("qui"), QStringLiteral("pas"), QStringLiteral("du")}},
    };
    QHash<QString, int> score;
    const int pages = int(snap->pageSizes.size());
    for (int p = 0; p < std::min(pages, 4); ++p) {
        for (const PdfWord &w : snap->words(std::min(pages - 1, p + pages / 4))) {
            const QString t = w.text.toLower();
            for (auto it = common.cbegin(); it != common.cend(); ++it)
                score[it.key()] += it.value().contains(t);
        }
    }
    QString best = QStringLiteral("pt");
    int top = 0;
    for (auto it = score.cbegin(); it != score.cend(); ++it) {
        if (it.value() > top) {
            top = it.value();
            best = it.key();
        }
    }
    return best;
}

void ReadAloud::buildFrom(int page)
{
    const auto snap = m_doc->snapshot();
    m_sentences.clear();
    m_audio.clear();
    m_translations.clear();
    m_carry = ReadingText::Sentence();
    m_firstPage = m_nextPage = std::max(0, page);
    if (!snap)
        return;
    if (m_furniture.bodyHeight <= 0) {
        // headers/footers repeat: look at up to 16 pages spread over the document
        QHash<int, QList<ReadingText::Line>> sample;
        QHash<int, QSizeF> sizes;
        const int n = int(snap->pageSizes.size());
        const int step = std::max(1, n / 16);
        for (int p = 0; p < n && sample.size() < 16; p += step) {
            sample.insert(p, ReadingText::lines(snap->words(p)));
            sizes.insert(p, snap->pageSizes.at(p));
        }
        m_furniture = ReadingText::detectFurniture(sample, sizes);
    }
    ensureSentences(0);
}

void ReadAloud::ensureSentences(int upto)
{
    const auto snap = m_doc->snapshot();
    if (!snap)
        return;
    const int pages = int(snap->pageSizes.size());
    // sentences up to `upto` plus a couple of pages, so prefetch has text
    while (m_nextPage < pages && (m_sentences.size() <= upto + Prefetch + 1
                                  || (m_index >= 0 && m_sentences.constLast().page < m_sentences.value(m_index).page + PagesAhead))) {
        const int p = m_nextPage++;
        const auto list = ReadingText::pageSentences(p, snap->words(p), snap->pageSizes.at(p), m_furniture, &m_paragraph,
                                                     &m_carry, m_language);
        m_sentences << list;
        if (m_nextPage >= pages && !m_carry.text.isEmpty()) {
            ReadingText::Sentence last = m_carry;
            last.paragraphEnd = true;
            m_sentences << last;
            m_carry = ReadingText::Sentence();
        }
    }
    Q_EMIT positionChanged();
}

QList<ReadingText::Sentence> ReadAloud::allSentences() const
{
    QList<ReadingText::Sentence> out;
    const auto snap = m_doc->snapshot();
    if (!snap)
        return out;
    ReadingText::Furniture furniture = m_furniture;
    if (furniture.bodyHeight <= 0) {
        QHash<int, QList<ReadingText::Line>> sample;
        QHash<int, QSizeF> sizes;
        const int n = int(snap->pageSizes.size());
        const int step = std::max(1, n / 16);
        for (int p = 0; p < n && sample.size() < 16; p += step) {
            sample.insert(p, ReadingText::lines(snap->words(p)));
            sizes.insert(p, snap->pageSizes.at(p));
        }
        furniture = ReadingText::detectFurniture(sample, sizes);
    }
    int paragraph = 0;
    ReadingText::Sentence carry;
    for (int p = 0; p < snap->pageSizes.size(); ++p)
        out << ReadingText::pageSentences(p, snap->words(p), snap->pageSizes.at(p), furniture, &paragraph, &carry, m_language);
    if (!carry.text.isEmpty()) {
        carry.paragraphEnd = true;
        out << carry;
    }
    return out;
}

int ReadAloud::sentenceAt(int page, const QPointF &point) const
{
    int best = -1;
    qreal bestDist = 1e9;
    for (int i = 0; i < m_sentences.size(); ++i) {
        for (const auto &w : m_sentences[i].words) {
            if (w.page != page)
                continue;
            for (const QRectF &r : w.rects) {
                if (r.adjusted(-2, -2, 2, 2).contains(point))
                    return i;
                const qreal dx = std::max({0.0, r.left() - point.x(), point.x() - r.right()});
                const qreal dy = std::max({0.0, r.top() - point.y(), point.y() - r.bottom()});
                const qreal d = dx + dy * 3;
                if (d < bestDist) {
                    bestDist = d;
                    best = i;
                }
            }
        }
    }
    return bestDist < 40 ? best : -1;
}

// ------------------------------------------------------------------ playing

void ReadAloud::start(int page)
{
    if (!m_doc->ready())
        return;
    if (m_language.isEmpty() || m_sentences.isEmpty())
        setLanguage(detectLanguage());
    if (m_voices.isEmpty())
        refreshVoices();
    if (m_voice.isEmpty())
        m_voice = defaultVoice(m_language);
    setError(QString());
    // reuse what is built when the page is inside it
    int index = -1;
    if (!m_sentences.isEmpty() && page >= m_firstPage && page < m_nextPage) {
        for (int i = 0; i < m_sentences.size(); ++i) {
            if (m_sentences[i].page >= page) {
                index = i;
                break;
            }
        }
    }
    if (index < 0) {
        buildFrom(page);
        index = 0;
    }
    if (m_sentences.isEmpty()) {
        setError(tr("There is no text to read on this page (it may be a scanned image)."));
        return;
    }
    m_active = true;
    m_playing = true;
    Q_EMIT stateChanged();
    playIndex(index);
}

void ReadAloud::startAt(int page, const QPointF &point)
{
    if (!m_doc->ready())
        return;
    if (m_sentences.isEmpty() || page < m_firstPage || page >= m_nextPage) {
        if (m_language.isEmpty())
            setLanguage(detectLanguage());
        buildFrom(page);
    }
    int i = sentenceAt(page, point);
    if (i < 0) {
        start(page);
        return;
    }
    if (m_voices.isEmpty())
        refreshVoices();
    if (m_voice.isEmpty())
        m_voice = defaultVoice(m_language);
    setError(QString());
    m_active = true;
    m_playing = true;
    Q_EMIT stateChanged();
    playIndex(i);
}

void ReadAloud::togglePlay()
{
    if (!m_active)
        return;
    if (m_playing)
        pause();
    else
        resume();
}

void ReadAloud::pause()
{
    m_playing = false;
    m_gap->stop();
    m_player->pause();
    m_tick->stop();
    Q_EMIT stateChanged();
}

void ReadAloud::resume()
{
    if (!m_active)
        return;
    m_playing = true;
    Q_EMIT stateChanged();
    if (m_player->mediaStatus() == QMediaPlayer::EndOfMedia || m_player->source().isEmpty())
        playIndex(m_index + (m_player->mediaStatus() == QMediaPlayer::EndOfMedia ? 1 : 0));
    else {
        m_player->play();
        m_tick->start();
    }
}

void ReadAloud::stop()
{
    m_active = false;
    m_playing = false;
    m_buffering = false;
    m_gap->stop();
    m_tick->stop();
    m_player->stop();
    m_player->setSource(QUrl());
    m_word = -1;
    ++m_revision;
    Q_EMIT stateChanged();
    Q_EMIT highlightChanged();
}

void ReadAloud::nextSentence()
{
    if (m_active)
        playIndex(m_index + 1);
}

void ReadAloud::previousSentence()
{
    if (!m_active)
        return;
    // like a music player: early in the sentence goes back one, later restarts it
    if (m_player->position() > 1500 || m_index == 0)
        playIndex(m_index);
    else
        playIndex(m_index - 1);
}

void ReadAloud::nextParagraph()
{
    if (!m_active || m_index < 0)
        return;
    const int para = m_sentences.value(m_index).paragraph;
    int i = m_index + 1;
    ensureSentences(i + 20);
    while (i < m_sentences.size() && m_sentences[i].paragraph == para)
        ++i;
    playIndex(i);
}

void ReadAloud::previousParagraph()
{
    if (!m_active || m_index < 0)
        return;
    int i = m_index;
    const int para = m_sentences.value(i).paragraph;
    // the start of this paragraph, or of the previous one when already there
    while (i > 0 && m_sentences[i - 1].paragraph == para)
        --i;
    if (i == m_index && i > 0) {
        const int prev = m_sentences[i - 1].paragraph;
        --i;
        while (i > 0 && m_sentences[i - 1].paragraph == prev)
            --i;
    }
    playIndex(i);
}

void ReadAloud::repeatSentence()
{
    if (!m_active || m_index < 0)
        return;
    m_slowReplay = false;
    m_playing = true;
    Q_EMIT stateChanged();
    playIndex(m_index);
}

void ReadAloud::repeatSlow()
{
    if (!m_active || m_index < 0)
        return;
    // generated slower by the engine (clearer than slowing the audio down)
    m_slowReplay = true;
    m_playing = true;
    m_gap->stop();
    m_player->stop();
    Q_EMIT stateChanged();
    request(m_index, std::min(generationSpeed(), SlowSpeed), true);
}

void ReadAloud::playIndex(int index)
{
    m_gap->stop();
    m_tick->stop();
    ensureSentences(index);
    if (index < 0)
        index = 0;
    if (index >= m_sentences.size()) {
        // the end of the document
        stop();
        return;
    }
    if (index != m_index)
        m_slowReplay = false;
    m_index = index;
    m_word = -1;
    m_followedLine = -1;
    ++m_revision;
    Q_EMIT positionChanged();
    Q_EMIT highlightChanged();
    follow();
    if (m_bilingual)
        requestTranslation(index);
    const auto it = m_audio.constFind(index);
    if (it != m_audio.constEnd() && !it->file.isEmpty() && qFuzzyCompare(it->speed, generationSpeed())) {
        startPlayback();
    } else {
        m_player->stop();
        m_buffering = true;
        Q_EMIT stateChanged();
        if (it == m_audio.constEnd() || !it->request || !qFuzzyCompare(it->speed, generationSpeed()))
            request(index, generationSpeed());
    }
    prefetch();
}

void ReadAloud::request(int index, double speed, bool slow)
{
    if (index < 0 || index >= m_sentences.size())
        return;
    Audio &a = m_audio[index];
    if (!slow) {
        a.speed = speed;
        a.failed = false;
    }
    const QJsonObject req{{QStringLiteral("text"), m_sentences[index].text},
                          {QStringLiteral("voice"), m_voice},
                          {QStringLiteral("lang"), m_language},
                          {QStringLiteral("speed"), speed},
                          {QStringLiteral("align"), true}};
    auto apply = [this, index, speed, slow](const QJsonObject &r, bool final) {
        Audio *a = &m_audio[index];
        if (slow) {
            if (index != m_index || !m_slowReplay)
                return;
            a = &m_audio[-1 - index];   // the slow take lives beside the normal one
        } else if (!qFuzzyCompare(a->speed, speed)) {
            return;   // speed changed meanwhile
        }
        const bool first = a->file.isEmpty();
        a->file = r.value(QStringLiteral("file")).toString();
        a->words = r.value(QStringLiteral("words")).toArray();
        a->durationMs = r.value(QStringLiteral("durationMs")).toInt();
        a->timing = r.value(QStringLiteral("timing")).toString();
        a->speed = speed;
        if (final)
            a->request = 0;
        if (index == m_index && (first || slow) && m_buffering) {
            if (slow) {
                m_buffering = false;
                Q_EMIT stateChanged();
                m_player->setSource(QUrl::fromLocalFile(a->file));
                m_player->setPlaybackRate(1.0);
                m_player->play();
                m_tick->start();
            } else {
                startPlayback();
            }
        }
        if (index == m_index)
            Q_EMIT positionChanged();
    };
    const int id = m_client->call(QStringLiteral("speak"), req,
        [this, index, apply, slow](const QJsonObject &r) {
            if (!r.value(QStringLiteral("ok")).toBool()) {
                if (!slow)
                    m_audio[index].failed = true;
                if (index == m_index || m_index < 0)
                    handleError(r);
                return;
            }
            apply(r, true);
        },
        [apply](const QJsonObject &ev) {
            if (ev.value(QStringLiteral("event")).toString() == QLatin1String("audio"))
                apply(ev, false);
        });
    if (slow) {
        m_audio[-1 - index] = Audio();
        m_audio[-1 - index].request = id;
        m_buffering = true;
        Q_EMIT stateChanged();
    } else {
        m_audio[index].request = id;
    }
}

void ReadAloud::prefetch()
{
    ensureSentences(m_index + Prefetch);
    for (int i = m_index + 1; i <= m_index + Prefetch && i < m_sentences.size(); ++i) {
        const auto it = m_audio.constFind(i);
        if (it == m_audio.constEnd() || (!it->request && it->file.isEmpty() && !it->failed))
            request(i, generationSpeed());
        if (m_bilingual && i == m_index + 1)
            requestTranslation(i);
    }
    // forget audio far behind
    for (auto it = m_audio.begin(); it != m_audio.end();) {
        if (it.key() >= 0 && it.key() < m_index - 8)
            it = m_audio.erase(it);
        else
            ++it;
    }
}

void ReadAloud::startPlayback()
{
    const Audio &a = m_audio[m_index];
    m_buffering = false;
    Q_EMIT stateChanged();
    m_player->setSource(QUrl::fromLocalFile(a.file));
    m_player->setPlaybackRate(m_speed / std::max(0.1, a.speed));
    if (m_playing) {
        m_player->play();
        m_tick->start();
    }
}

void ReadAloud::onTick()
{
    if (m_index < 0)
        return;
    const Audio &a = m_slowReplay && m_audio.contains(-1 - m_index) ? m_audio[-1 - m_index] : m_audio[m_index];
    const qint64 pos = m_player->position();
    int found = -1;
    // the word whose time span holds the position (the engine's offsets are in the sentence text)
    for (int k = 0; k < a.words.size(); ++k) {
        const QJsonObject w = a.words[k].toObject();
        if (pos >= w.value(QStringLiteral("start")).toInt())
            found = k;
        else
            break;
    }
    if (found < 0) {
        setCurrentWord(-1);
        return;
    }
    const QJsonObject w = a.words[found].toObject();
    const QList<int> words = ReadingText::wordsInRange(m_sentences[m_index], w.value(QStringLiteral("s")).toInt(),
                                                       w.value(QStringLiteral("e")).toInt());
    setCurrentWord(words.isEmpty() ? m_word : words.first());
}

void ReadAloud::setCurrentWord(int word)
{
    if (word == m_word)
        return;
    m_word = word;
    ++m_revision;
    Q_EMIT highlightChanged();
    // follow when the word moves to another line
    if (m_follow && word >= 0 && m_index >= 0) {
        const auto &w = m_sentences[m_index].words.value(word);
        const int line = w.rects.isEmpty() ? -1 : int(w.rects.first().top());
        if (line != m_followedLine) {
            m_followedLine = line;
            if (!w.rects.isEmpty())
                Q_EMIT followRequested(w.page, w.rects.first());
        }
    }
}

void ReadAloud::follow()
{
    if (!m_follow || m_index < 0 || m_index >= m_sentences.size())
        return;
    const auto &s = m_sentences[m_index];
    if (s.words.isEmpty() || s.words.first().rects.isEmpty())
        return;
    Q_EMIT followRequested(s.words.first().page, s.words.first().rects.first());
}

void ReadAloud::onFinished()
{
    m_tick->stop();
    setCurrentWord(-1);
    if (!m_playing)
        return;
    if (m_slowReplay) {
        // after the slow replay, wait (learning) or carry on
        m_slowReplay = false;
        if (m_learning) {
            m_playing = false;
            Q_EMIT stateChanged();
            return;
        }
    }
    const auto &s = m_sentences.value(m_index);
    int gapMs = s.heading ? 700 : s.paragraphEnd ? 420 : 160;
    if (m_learning)
        gapMs = int(m_sentencePause * 1000) + (s.paragraphEnd ? 300 : 0);
    m_gap->start(int(gapMs / std::max(0.5, m_speed)));
}

void ReadAloud::handleError(const QJsonObject &reply)
{
    m_buffering = false;
    m_playing = false;
    m_errorCode = reply.value(QStringLiteral("code")).toString();
    QString msg = reply.value(QStringLiteral("error")).toString();
    if (m_errorCode == QLatin1String("vram"))
        msg = tr("The graphics card doesn't have enough free memory for this voice. Choose a piper voice, or free the memory used by the AI models.");
    else if (m_errorCode == QLatin1String("ram"))
        msg = tr("There isn't enough free memory for this voice. Close some programs or choose a piper voice.");
    else if (m_errorCode == QLatin1String("install"))
        msg = tr("This voice needs an engine that isn't installed yet. Open Voice Studio to install it, or choose another voice.");
    m_error = msg;
    Q_EMIT stateChanged();
}

// ------------------------------------------------------------------ highlight geometry

int ReadAloud::currentPage() const
{
    return m_index >= 0 && m_index < m_sentences.size() ? m_sentences[m_index].page : -1;
}

QString ReadAloud::currentText() const
{
    return m_index >= 0 && m_index < m_sentences.size() ? m_sentences[m_index].text : QString();
}

QString ReadAloud::currentTranslation() const
{
    return m_translations.value(m_index);
}

QString ReadAloud::timing() const
{
    return m_audio.value(m_index).timing;
}

QVariantList ReadAloud::sentencePolygons(int page) const
{
    if (!m_active || !m_highlight || m_index < 0 || m_index >= m_sentences.size())
        return {};
    return polygons(ReadingText::lineRects(m_sentences[m_index]).value(page), 1.5);
}

QVariantList ReadAloud::wordPolygons(int page) const
{
    if (!m_active || !m_highlight || m_index < 0 || m_index >= m_sentences.size() || m_word < 0)
        return {};
    const auto &w = m_sentences[m_index].words.value(m_word);
    return w.page == page ? polygons(w.rects, 1.5) : QVariantList();
}

QVariantMap ReadAloud::wordAt(int page, const QPointF &point)
{
    if (m_sentences.isEmpty() || page < m_firstPage || page >= m_nextPage) {
        if (m_language.isEmpty())
            setLanguage(detectLanguage());
        buildFrom(page);
        ensureSentences(int(m_sentences.size()) + 30);
    }
    for (const auto &s : std::as_const(m_sentences)) {
        for (const auto &w : s.words) {
            if (w.page != page)
                continue;
            for (const QRectF &r : w.rects) {
                if (r.adjusted(-1.5, -1.5, 1.5, 1.5).contains(point)) {
                    QString text = s.text.mid(w.start, w.end - w.start);
                    static const QRegularExpression edges(QStringLiteral("^[^\\p{L}\\p{N}]+|[^\\p{L}\\p{N}]+$"));
                    text.remove(edges);
                    return {{QStringLiteral("text"), text}, {QStringLiteral("sentence"), s.text}, {QStringLiteral("page"), page},
                            {QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()},
                            {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()}};
                }
            }
        }
    }
    return {};
}

void ReadAloud::translateWord(int page, const QPointF &point)
{
    QVariantMap word = wordAt(page, point);
    if (word.isEmpty() || word.value(QStringLiteral("text")).toString().isEmpty())
        return;
    word.insert(QStringLiteral("pending"), true);
    Q_EMIT wordTranslated(word);
    m_client->call(QStringLiteral("translate"),
                   {{QStringLiteral("text"), word.value(QStringLiteral("text")).toString()},
                    {QStringLiteral("from"), m_language},
                    {QStringLiteral("to"), m_native},
                    {QStringLiteral("mode"), QStringLiteral("word")},
                    {QStringLiteral("context"), word.value(QStringLiteral("sentence")).toString()}},
                   [this, word](const QJsonObject &r) mutable {
                       word.insert(QStringLiteral("pending"), false);
                       if (r.value(QStringLiteral("ok")).toBool()) {
                           word.insert(QStringLiteral("translation"), r.value(QStringLiteral("translation")).toString());
                           word.insert(QStringLiteral("note"), r.value(QStringLiteral("note")).toString());
                       } else {
                           word.insert(QStringLiteral("error"), r.value(QStringLiteral("error")).toString());
                       }
                       Q_EMIT wordTranslated(word);
                   });
}

void ReadAloud::requestTranslation(int index)
{
    if (index < 0 || index >= m_sentences.size() || m_translations.contains(index) || m_translating.contains(index)
        || m_language == m_native)
        return;
    m_translating.insert(index);
    m_client->call(QStringLiteral("translate"),
                   {{QStringLiteral("text"), m_sentences[index].text},
                    {QStringLiteral("from"), m_language},
                    {QStringLiteral("to"), m_native},
                    {QStringLiteral("mode"), QStringLiteral("sentence")}},
                   [this, index](const QJsonObject &r) {
                       m_translating.remove(index);
                       if (r.value(QStringLiteral("ok")).toBool())
                           m_translations.insert(index, r.value(QStringLiteral("translation")).toString());
                       if (index == m_index)
                           Q_EMIT translationChanged();
                   });
}

// ------------------------------------------------------------------ export

QVariantMap ReadAloud::documentInfo() const
{
    return {{QStringLiteral("title"), m_doc->title().isEmpty() ? QFileInfo(m_doc->path()).completeBaseName() : m_doc->title()},
            {QStringLiteral("author"), m_doc->author()},
            {QStringLiteral("chapters"), [this] {
                 int n = 0;
                 for (const QVariant &o : m_doc->outline())
                     n += o.toMap().value(QStringLiteral("depth")).toInt() == 0;
                 return n;
             }()},
            {QStringLiteral("hasCover"), !m_doc->coverPath().isEmpty() || m_doc->sourceFormat() == QLatin1String("pdf")}};
}

QString ReadAloud::suggestedExportPath(const QString &format, bool perChapter) const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    const QString title = documentInfo().value(QStringLiteral("title")).toString();
    QString name = title;
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")), QStringLiteral(" "));
    name = name.simplified().left(80);
    if (name.isEmpty())
        name = tr("Audiobook");
    if (perChapter)
        return base + QLatin1Char('/') + name;
    return base + QLatin1Char('/') + name + QLatin1Char('.') + format;
}

void ReadAloud::exportAudio(const QVariantMap &options)
{
    if (!m_doc->ready())
        return;
    if (m_language.isEmpty())
        setLanguage(detectLanguage());
    if (m_voice.isEmpty())
        m_voice = defaultVoice(m_language);
    const QList<ReadingText::Sentence> sentences = allSentences();
    if (sentences.isEmpty()) {
        setError(tr("There is no text to export (the document may be scanned images)."));
        return;
    }
    // chapters from the outline (top level), else one
    struct Mark { QString title; int page; qreal top; };
    QList<Mark> marks;
    for (const QVariant &v : m_doc->outline()) {
        const QVariantMap o = v.toMap();
        if (o.value(QStringLiteral("depth")).toInt() == 0 && o.value(QStringLiteral("page")).toInt() >= 0)
            marks << Mark{o.value(QStringLiteral("title")).toString(), o.value(QStringLiteral("page")).toInt(),
                          o.value(QStringLiteral("top")).toReal()};
    }
    std::stable_sort(marks.begin(), marks.end(), [](const Mark &a, const Mark &b) {
        return a.page < b.page || (a.page == b.page && a.top < b.top);
    });
    const QString title = options.value(QStringLiteral("title"), documentInfo().value(QStringLiteral("title"))).toString();
    QJsonArray chapters;
    QJsonArray items;
    QString chapterTitle = marks.isEmpty() || marks.first().page > sentences.first().page ? title : marks.first().title;
    int nextMark = marks.isEmpty() ? -1 : (marks.first().page > sentences.first().page ? 0 : 1);
    for (const auto &s : sentences) {
        const qreal y = s.words.isEmpty() || s.words.first().rects.isEmpty() ? 0 : s.words.first().rects.first().top();
        while (nextMark >= 0 && nextMark < marks.size()
               && (s.page > marks[nextMark].page || (s.page == marks[nextMark].page && y >= marks[nextMark].top - 4))) {
            if (!items.isEmpty())
                chapters.append(QJsonObject{{QStringLiteral("title"), chapterTitle}, {QStringLiteral("items"), items}});
            items = QJsonArray();
            chapterTitle = marks[nextMark].title;
            ++nextMark;
        }
        QJsonObject item{{QStringLiteral("text"), s.text}, {QStringLiteral("paragraphEnd"), s.paragraphEnd}};
        if (s.heading)
            item.insert(QStringLiteral("pauseAfter"), 0.9);
        items.append(item);
    }
    if (!items.isEmpty())
        chapters.append(QJsonObject{{QStringLiteral("title"), chapterTitle}, {QStringLiteral("items"), items}});

    const QString format = options.value(QStringLiteral("format"), QStringLiteral("mp3")).toString();
    const bool perChapter = options.value(QStringLiteral("perChapter")).toBool();
    QJsonObject spec{
        {QStringLiteral("title"), title},
        {QStringLiteral("author"), options.value(QStringLiteral("author"), m_doc->author()).toString()},
        {QStringLiteral("lang"), m_language},
        {QStringLiteral("voice"), options.value(QStringLiteral("voice"), m_voice).toString()},
        {QStringLiteral("speed"), options.value(QStringLiteral("speed"), m_speed).toDouble()},
        {QStringLiteral("pause"), options.value(QStringLiteral("pause"), 0.35).toDouble()},
        {QStringLiteral("paragraphPause"), options.value(QStringLiteral("paragraphPause"), 0.8).toDouble()},
        {QStringLiteral("format"), format},
        {QStringLiteral("perChapter"), perChapter},
        {QStringLiteral("lyrics"), options.value(QStringLiteral("lyrics"), true).toBool()},
        {QStringLiteral("lrc"), options.value(QStringLiteral("lrc"), true).toBool()},
        {QStringLiteral("srt"), options.value(QStringLiteral("srt"), false).toBool()},
        {QStringLiteral("out"), options.value(QStringLiteral("out"), suggestedExportPath(format, perChapter)).toString()},
        {QStringLiteral("chapters"), chapters},
        {QStringLiteral("comment"), options.value(QStringLiteral("comment")).toString()},
        // learning: each sentence again, slower; and/or followed by its translation (the engine translates)
        {QStringLiteral("slowRepeat"), options.value(QStringLiteral("slowRepeat")).toBool() ? 0.7 : 0.0},
    };
    if (options.value(QStringLiteral("bilingual")).toBool() && m_language != m_native)
        spec.insert(QStringLiteral("bilingual"), QJsonObject{{QStringLiteral("to"), m_native}, {QStringLiteral("voice"), defaultVoice(m_native)}});
    if (!m_doc->coverPath().isEmpty())
        spec.insert(QStringLiteral("cover"), m_doc->coverPath());
    else if (m_doc->sourceFormat() == QLatin1String("pdf"))
        spec.insert(QStringLiteral("coverFromPdf"), m_doc->renderedPath());
    m_job = {{QStringLiteral("state"), QStringLiteral("starting")}, {QStringLiteral("progress"), 0.0}};
    Q_EMIT exportChanged();
    m_client->call(QStringLiteral("export.start"), {{QStringLiteral("spec"), spec}}, [this](const QJsonObject &r) {
        if (!r.value(QStringLiteral("ok")).toBool()) {
            m_job = {{QStringLiteral("state"), QStringLiteral("failed")}, {QStringLiteral("message"), r.value(QStringLiteral("error")).toString()}};
        } else {
            m_jobId = r.value(QStringLiteral("job")).toString();
            m_job = r.toVariantMap();
        }
        Q_EMIT exportChanged();
    });
}

void ReadAloud::exportControl(const QString &action)
{
    if (m_jobId.isEmpty())
        return;
    m_client->call(QStringLiteral("export.") + action, {{QStringLiteral("job"), m_jobId}}, [this, action](const QJsonObject &r) {
        if (r.value(QStringLiteral("ok")).toBool() && action != QLatin1String("forget"))
            m_job = r.toVariantMap();
        if (action == QLatin1String("forget")) {
            m_job.clear();
            m_jobId.clear();
        }
        Q_EMIT exportChanged();
    });
}
