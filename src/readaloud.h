/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QVariant>

#include "readingtext.h"

class PdfDocument;
class QMediaPlayer;
class QAudioOutput;
class QTimer;
class VoiceClient;

// "Ouvir": reads the open document aloud with lingmo-voice-engine, sentence
// by sentence, and tells the pages what to highlight (exposed as "readAloud").
//
//  - the sentence being read softly, the word being spoken strongly, from the
//    word times the engine measures on the generated audio (whisper DTW);
//    both optional ("Destacar enquanto lê"), and the view can follow
//    ("Seguir a leitura"); the choices are remembered;
//  - play/pause, sentence and paragraph steps, a click on a sentence reads
//    from there, speed 0.5–2× without pitch change;
//  - learning mode: a pause after each sentence, repeat, "ouvir de novo
//    devagar", a click on a word shows its translation, bilingual view;
//  - "Exportar áudio": the whole document (or the chapters) as a background
//    job of the engine, with the Island showing progress.
class ReadAloud : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error WRITE setError NOTIFY stateChanged)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
    Q_PROPERTY(bool engineInstalled READ engineInstalled CONSTANT)
    Q_PROPERTY(int sentenceIndex READ sentenceIndex NOTIFY positionChanged)
    Q_PROPERTY(int sentenceCount READ sentenceCount NOTIFY positionChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY positionChanged)
    Q_PROPERTY(QString currentText READ currentText NOTIFY positionChanged)
    Q_PROPERTY(QString currentTranslation READ currentTranslation NOTIFY translationChanged)
    Q_PROPERTY(QString timing READ timing NOTIFY positionChanged)
    Q_PROPERTY(int highlightRevision READ highlightRevision NOTIFY highlightChanged)

    Q_PROPERTY(bool highlightEnabled READ highlightEnabled WRITE setHighlightEnabled NOTIFY settingsChanged)
    Q_PROPERTY(bool followReading READ followReading WRITE setFollowReading NOTIFY settingsChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY settingsChanged)
    Q_PROPERTY(QString voiceId READ voiceId WRITE setVoiceId NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)

    Q_PROPERTY(bool learning READ learning WRITE setLearning NOTIFY settingsChanged)
    Q_PROPERTY(double sentencePause READ sentencePause WRITE setSentencePause NOTIFY settingsChanged)
    Q_PROPERTY(bool bilingual READ bilingual WRITE setBilingual NOTIFY settingsChanged)
    Q_PROPERTY(QString nativeLanguage READ nativeLanguage WRITE setNativeLanguage NOTIFY settingsChanged)

    Q_PROPERTY(QVariantMap exportJob READ exportJob NOTIFY exportChanged)

public:
    explicit ReadAloud(PdfDocument *document, QObject *parent = nullptr);
    ~ReadAloud() override;

    bool active() const { return m_active; }
    bool playing() const { return m_playing; }
    bool buffering() const { return m_buffering; }
    QString error() const { return m_error; }
    void setError(const QString &e);
    QString errorCode() const { return m_errorCode; }
    bool engineInstalled() const;
    int sentenceIndex() const { return m_index; }
    int sentenceCount() const { return int(m_sentences.size()); }
    int currentPage() const;
    QString currentText() const;
    QString currentTranslation() const;
    QString timing() const;
    int highlightRevision() const { return m_revision; }

    bool highlightEnabled() const { return m_highlight; }
    void setHighlightEnabled(bool on);
    bool followReading() const { return m_follow; }
    void setFollowReading(bool on);
    double speed() const { return m_speed; }
    void setSpeed(double s);
    QString voiceId() const { return m_voice; }
    void setVoiceId(const QString &id);
    QString language() const { return m_language; }
    void setLanguage(const QString &lang);
    QVariantList voices() const { return m_voices; }

    bool learning() const { return m_learning; }
    void setLearning(bool on);
    double sentencePause() const { return m_sentencePause; }
    void setSentencePause(double s);
    bool bilingual() const { return m_bilingual; }
    void setBilingual(bool on);
    QString nativeLanguage() const { return m_native; }
    void setNativeLanguage(const QString &lang);

    QVariantMap exportJob() const { return m_job; }

    // ---- control
    Q_INVOKABLE void start(int page);
    Q_INVOKABLE void startAt(int page, const QPointF &point);   // point in page points
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void nextSentence();
    Q_INVOKABLE void previousSentence();
    Q_INVOKABLE void nextParagraph();
    Q_INVOKABLE void previousParagraph();
    Q_INVOKABLE void repeatSentence();
    Q_INVOKABLE void repeatSlow();
    Q_INVOKABLE void refreshVoices();

    // ---- what the pages draw (page points)
    Q_INVOKABLE QVariantList sentencePolygons(int page) const;
    Q_INVOKABLE QVariantList wordPolygons(int page) const;
    // A word under a point: { text, sentence, page, x, y, width, height } or {}
    Q_INVOKABLE QVariantMap wordAt(int page, const QPointF &point);
    Q_INVOKABLE void translateWord(int page, const QPointF &point);

    // ---- export: { format, perChapter, speed, pause, title, author, lrc, srt, out, translated }
    Q_INVOKABLE void exportAudio(const QVariantMap &options);
    Q_INVOKABLE void exportControl(const QString &action);   // pause, resume, cancel, forget
    Q_INVOKABLE QString suggestedExportPath(const QString &format, bool perChapter) const;
    Q_INVOKABLE QVariantMap documentInfo() const;

Q_SIGNALS:
    void stateChanged();
    void positionChanged();
    void highlightChanged();
    void settingsChanged();
    void voicesChanged();
    void translationChanged();
    void exportChanged();
    // the view should bring this rectangle (page points) into view
    void followRequested(int page, const QRectF &rect);
    void wordTranslated(const QVariantMap &result);

private:
    struct Audio {
        QString file;
        QJsonArray words;     // [{s, e, start, end}]
        int durationMs = 0;
        double speed = 1.0;   // what it was generated at
        QString timing;
        int request = 0;      // pending request id
        bool failed = false;
    };

    void documentChanged();
    void reset();
    void buildFrom(int page);
    void ensureSentences(int upto);
    int sentenceAt(int page, const QPointF &point) const;
    void playIndex(int index);
    void request(int index, double speed, bool slow = false);
    void prefetch();
    void startPlayback();
    void onTick();
    void onFinished();
    void setCurrentWord(int word);
    void follow();
    void requestTranslation(int index);
    void handleError(const QJsonObject &reply);
    double generationSpeed() const;
    QString defaultVoice(const QString &lang) const;
    QString detectLanguage();
    void loadSettings();
    void saveSetting(const QString &key, const QVariant &value);
    QList<ReadingText::Sentence> allSentences() const;

    PdfDocument *m_doc;
    VoiceClient *m_client;
    QMediaPlayer *m_player;
    QAudioOutput *m_output;
    QTimer *m_tick;
    QTimer *m_gap;

    ReadingText::Furniture m_furniture;
    QList<ReadingText::Sentence> m_sentences;
    ReadingText::Sentence m_carry;
    int m_firstPage = 0;
    int m_nextPage = 0;
    int m_paragraph = 0;
    QHash<int, Audio> m_audio;
    QHash<int, QString> m_translations;
    QSet<int> m_translating;

    bool m_active = false;
    bool m_playing = false;
    bool m_buffering = false;
    bool m_slowReplay = false;
    QString m_error, m_errorCode;
    int m_index = -1;
    int m_word = -1;
    int m_revision = 0;
    int m_followedLine = -1;

    bool m_highlight = true;
    bool m_follow = true;
    double m_speed = 1.0;
    QString m_voice;
    QString m_language;
    QVariantList m_voices;
    bool m_learning = false;
    double m_sentencePause = 1.2;
    bool m_bilingual = false;
    QString m_native = QStringLiteral("pt");

    QVariantMap m_job;
    QString m_jobId;
};
