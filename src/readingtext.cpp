/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "readingtext.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace ReadingText {

namespace {

qreal median(QList<qreal> v)
{
    if (v.isEmpty())
        return 0;
    std::sort(v.begin(), v.end());
    return v.at(v.size() / 2);
}

bool isEnd(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QChar(0x2026);
}

bool isCloser(QChar c)
{
    return c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QChar(0x201D) || c == QChar(0x2019) || c == QChar(0x00BB)
        || c == QLatin1Char(')') || c == QLatin1Char(']');
}

const QSet<QString> &abbreviations()
{
    static const QSet<QString> set = {
        // pt
        QStringLiteral("sr"), QStringLiteral("sra"), QStringLiteral("srta"), QStringLiteral("dr"), QStringLiteral("dra"),
        QStringLiteral("prof"), QStringLiteral("profa"), QStringLiteral("av"), QStringLiteral("n"), QStringLiteral("nº"),
        QStringLiteral("pág"), QStringLiteral("pag"), QStringLiteral("p"), QStringLiteral("pp"), QStringLiteral("vol"),
        QStringLiteral("cap"), QStringLiteral("ex"), QStringLiteral("obs"), QStringLiteral("fig"), QStringLiteral("séc"),
        QStringLiteral("ltda"), QStringLiteral("cia"), QStringLiteral("aprox"), QStringLiteral("art"), QStringLiteral("etc"),
        QStringLiteral("jan"), QStringLiteral("fev"), QStringLiteral("abr"), QStringLiteral("mai"), QStringLiteral("jun"),
        QStringLiteral("jul"), QStringLiteral("ago"), QStringLiteral("set"), QStringLiteral("out"), QStringLiteral("nov"),
        QStringLiteral("dez"),
        // en
        QStringLiteral("mr"), QStringLiteral("mrs"), QStringLiteral("ms"), QStringLiteral("st"), QStringLiteral("jr"),
        QStringLiteral("vs"), QStringLiteral("e.g"), QStringLiteral("i.e"), QStringLiteral("no"), QStringLiteral("inc"),
        QStringLiteral("ltd"), QStringLiteral("co"), QStringLiteral("ch"), QStringLiteral("approx"),
    };
    return set;
}

bool numeric(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[\\[(]?[0-9ivxlcdmIVXLCDM*†‡¹²³⁴⁵⁶⁷⁸⁹⁰]{1,6}[\\])]?[.,:;]?$"));
    return re.match(s).hasMatch();
}

bool pageNumberLine(const QString &t)
{
    static const QRegularExpression re(QStringLiteral(
        "^(\\p{L}+\\s+)?[-–—\\s]*\\(?[0-9ivxlcdmIVXLCDM]{1,5}\\)?(\\s*(/|de|of)\\s*[0-9]{1,5})?[-–—\\s]*$"),
        QRegularExpression::CaseInsensitiveOption);
    return t.size() <= 24 && re.match(t.trimmed()).hasMatch();
}

} // namespace

// ------------------------------------------------------------------ lines

namespace {

Line makeLine(QList<PdfWord> words)
{
    std::sort(words.begin(), words.end(), [](const PdfWord &a, const PdfWord &b) { return a.rect.left() < b.rect.left(); });
    Line l;
    QList<qreal> hs;
    QStringList parts;
    for (const PdfWord &w : words) {
        l.rect = l.words.isEmpty() ? w.rect : l.rect.united(w.rect);
        l.words << w;
        hs << w.rect.height();
        parts << w.text;
    }
    l.height = median(hs);
    l.text = parts.join(QLatin1Char(' '));
    return l;
}

} // namespace

QList<Line> lines(const QList<PdfWord> &input)
{
    // 1. rows: words whose boxes share most of their height, by geometry (poppler's
    //    order puts raised footnote markers and what follows them elsewhere)
    QList<PdfWord> words;
    for (const PdfWord &w : input) {
        if (!w.text.trimmed().isEmpty())
            words << w;
    }
    if (words.isEmpty())
        return {};
    QList<qreal> hs;
    for (const PdfWord &w : words)
        hs << w.rect.height();
    const qreal typical = std::max<qreal>(1, median(hs));
    std::sort(words.begin(), words.end(), [](const PdfWord &a, const PdfWord &b) { return a.rect.center().y() < b.rect.center().y(); });
    struct Row { QList<PdfWord> words; qreal top, bottom; qreal base; };
    QList<Row> rows;
    for (const PdfWord &w : words) {
        Row *target = nullptr;
        for (int i = int(rows.size()) - 1; i >= 0 && i >= int(rows.size()) - 4; --i) {
            Row &r = rows[i];
            const qreal h = std::min(r.bottom - r.top, w.rect.height());
            const qreal overlap = std::min(r.bottom, w.rect.bottom()) - std::max(r.top, w.rect.top());
            // a small raised word (footnote marker) joins the row it sits on
            const bool raised = w.rect.height() < typical * 0.8 && w.rect.bottom() > r.top && w.rect.top() < r.bottom;
            if (overlap > 0.45 * h || raised) {
                target = &r;
                break;
            }
        }
        if (target) {
            target->words << w;
            if (w.rect.height() >= typical * 0.8) {
                target->top = std::min(target->top, w.rect.top());
                target->bottom = std::max(target->bottom, w.rect.bottom());
            }
        } else {
            rows.append({{w}, w.rect.top(), w.rect.bottom(), w.rect.bottom()});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.top < b.top; });

    // 2. columns: a gutter (a wide gap at about the same x in many rows) splits
    //    the page into a left and a right column, read one after the other
    qreal pageLeft = 1e9, pageRight = -1e9;
    for (const PdfWord &w : words) {
        pageLeft = std::min(pageLeft, w.rect.left());
        pageRight = std::max(pageRight, w.rect.right());
    }
    const qreal mid = (pageLeft + pageRight) / 2;
    int split = 0;
    QList<qreal> gutters;
    for (Row &r : rows) {
        std::sort(r.words.begin(), r.words.end(), [](const PdfWord &a, const PdfWord &b) { return a.rect.left() < b.rect.left(); });
        for (int k = 1; k < r.words.size(); ++k) {
            const qreal gap = r.words[k].rect.left() - r.words[k - 1].rect.right();
            const qreal x = (r.words[k].rect.left() + r.words[k - 1].rect.right()) / 2;
            if (gap > typical * 1.6 && std::abs(x - mid) < (pageRight - pageLeft) * 0.15) {
                ++split;
                gutters << x;
                break;
            }
        }
    }
    QList<Line> out;
    if (rows.size() >= 6 && split >= rows.size() * 0.5) {
        const qreal gutter = median(gutters);
        QList<Line> left, right, full;
        for (const Row &r : rows) {
            QList<PdfWord> l, rr;
            for (const PdfWord &w : r.words)
                (w.rect.center().x() < gutter ? l : rr) << w;
            const bool spans = !l.isEmpty() && !rr.isEmpty() && l.constLast().rect.right() > gutter - typical * 0.3;
            if (spans) {
                full << makeLine(r.words);   // a title across both columns
                continue;
            }
            if (!l.isEmpty())
                left << makeLine(l);
            if (!rr.isEmpty())
                right << makeLine(rr);
        }
        // full-width rows above the columns come first
        for (const Line &f : full) {
            if (left.isEmpty() || f.rect.top() < left.first().rect.top())
                out << f;
        }
        out << left << right;
        for (const Line &f : full) {
            if (!left.isEmpty() && f.rect.top() >= left.first().rect.top())
                out << f;
        }
        return out;
    }
    for (const Row &r : rows)
        out << makeLine(r.words);
    return out;
}

// ------------------------------------------------------------------ headers and footers

QString furnitureKey(const QString &lineText)
{
    QString k = lineText.simplified().toLower();
    static const QRegularExpression digits(QStringLiteral("[0-9]+"));
    k.replace(digits, QStringLiteral("#"));
    static const QRegularExpression roman(QStringLiteral("\\b[ivxlc]{1,6}\\b"));
    k.replace(roman, QStringLiteral("#"));
    return k;
}

Furniture detectFurniture(const QHash<int, QList<Line>> &pagesLines, const QHash<int, QSizeF> &sizes)
{
    Furniture f;
    QHash<QString, int> counts;
    QList<qreal> heights;
    for (auto it = pagesLines.cbegin(); it != pagesLines.cend(); ++it) {
        const qreal h = sizes.value(it.key()).height();
        QSet<QString> seen;
        for (const Line &l : it.value()) {
            heights << l.height;
            const bool band = l.rect.top() < h * 0.10 || l.rect.bottom() > h * 0.90;
            if (!band)
                continue;
            const QString k = furnitureKey(l.text);
            if (!seen.contains(k)) {
                seen.insert(k);
                counts[k] += 1;
            }
        }
    }
    const int pages = int(pagesLines.size());
    const int need = std::max(3, int(std::ceil(pages * 0.3)));
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        if (pages >= 3 && it.value() >= need && it.key() != QLatin1String("#"))
            f.repeated.insert(it.key());
    }
    f.bodyHeight = median(heights);
    return f;
}

// ------------------------------------------------------------------ sentences

QList<QPair<int, int>> splitSentences(const QString &text, const QString &lang)
{
    Q_UNUSED(lang)
    QList<QPair<int, int>> spans;
    const int n = int(text.size());
    auto push = [&](int a, int b) {
        while (a < b && text.at(a).isSpace())
            ++a;
        while (b > a && text.at(b - 1).isSpace())
            --b;
        bool any = false;
        for (int k = a; k < b && !any; ++k)
            any = text.at(k).isLetterOrNumber();
        if (any)
            spans.append({a, b});
    };
    int start = 0;
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (isEnd(c)) {
            int j = i + 1;
            while (j < n && (isEnd(text.at(j)) || isCloser(text.at(j))))
                ++j;
            bool split = j >= n || text.at(j).isSpace();
            if (split && c == QLatin1Char('.')) {
                // abbreviation or initial before the dot?
                int k = i - 1;
                while (k >= 0 && (text.at(k).isLetter() || text.at(k) == QLatin1Char('.') || text.at(k) == QChar(0x00BA)))
                    --k;
                const QString word = text.mid(k + 1, i - k - 1);
                if (!word.isEmpty() && (abbreviations().contains(word.toLower()) || (word.size() == 1 && word.at(0).isUpper())))
                    split = false;
                // the next word starts lowercase: not a sentence end ("etc. e mais")
                int m = j;
                while (m < n && text.at(m).isSpace())
                    ++m;
                if (m < n && text.at(m).isLower() && word.size() <= 4)
                    split = false;
            }
            if (split) {
                push(start, j);
                start = j;
            }
            i = j;
            continue;
        }
        ++i;
    }
    push(start, n);
    // very long sentences: cut at ; : then commas, so one TTS call stays short
    QList<QPair<int, int>> out;
    for (auto s : spans) {
        while (s.second - s.first > 260) {
            int cut = -1;
            for (const QString marks : {QStringLiteral(";:"), QStringLiteral(","), QStringLiteral(" ")}) {
                for (int k = s.first + 260; k > s.first + 80 && cut < 0; --k) {
                    if (marks.contains(text.at(k - 1)))
                        cut = k;
                }
                if (cut > 0)
                    break;
            }
            if (cut < 0)
                cut = s.first + 260;
            out.append({s.first, cut});
            s.first = cut;
            while (s.first < s.second && text.at(s.first).isSpace())
                ++s.first;
        }
        if (s.second > s.first)
            out.append(s);
    }
    return out;
}

QList<Sentence> pageSentences(int page, const QList<PdfWord> &words, QSizeF pageSize, const Furniture &furniture,
                              int *paragraph, Sentence *carry, const QString &lang)
{
    const QList<Line> ls = lines(words);
    if (ls.isEmpty())
        return {};
    const qreal pageH = pageSize.height();
    QList<qreal> heights, gaps, lefts;
    for (int i = 0; i < ls.size(); ++i) {
        heights << ls[i].height;
        lefts << ls[i].rect.left();
        if (i > 0) {
            const qreal g = ls[i].rect.top() - ls[i - 1].rect.bottom();
            if (g > -2 && g < ls[i].height * 3)
                gaps << g;
        }
    }
    const qreal body = furniture.bodyHeight > 0 ? furniture.bodyHeight : median(heights);
    const qreal gap = std::max<qreal>(0.5, median(gaps));
    const qreal left = median(lefts);
    qreal width = 0;
    for (const Line &l : ls)
        width = std::max(width, l.rect.width());

    // 1. what to read
    QList<bool> keep(ls.size(), true);
    for (int i = 0; i < ls.size(); ++i) {
        const Line &l = ls[i];
        const bool band = l.rect.top() < pageH * 0.10 || l.rect.bottom() > pageH * 0.90;
        if (band && (furniture.repeated.contains(furnitureKey(l.text)) || pageNumberLine(l.text)))
            keep[i] = false;
    }
    // footnotes: a block of small lines at the bottom, starting with a marker
    for (int i = int(ls.size()) - 1; i >= 0; --i) {
        if (!keep[i])
            continue;
        const Line &l = ls[i];
        if (l.rect.top() < pageH * 0.55 || l.height >= body * 0.88)
            break;
        keep[i] = false;
        const QString first = l.words.isEmpty() ? QString() : l.words.first().text;
        if (numeric(first) && i > 0 && ls[i - 1].height >= body * 0.88)
            break;   // the first line of the footnote block
    }

    // 2. paragraphs and words
    QList<Sentence> out;
    struct Piece { int page; QRectF rect; };
    QString text;
    QList<QList<Piece>> wordPieces;   // per word: its rects
    QList<QPair<int, int>> wordSpans;
    bool heading = false;
    auto flushParagraph = [&](bool end) {
        if (text.trimmed().isEmpty()) {
            text.clear();
            wordPieces.clear();
            wordSpans.clear();
            return;
        }
        const auto spans = splitSentences(text, lang);
        for (int si = 0; si < spans.size(); ++si) {
            Sentence s;
            const int a = spans[si].first, b = spans[si].second;
            s.text = text.mid(a, b - a);
            s.paragraph = *paragraph;
            s.heading = heading;
            for (int w = 0; w < wordSpans.size(); ++w) {
                if (wordSpans[w].second <= a || wordSpans[w].first >= b)
                    continue;
                Word word;
                word.start = std::max(0, wordSpans[w].first - a);
                word.end = std::min(b, wordSpans[w].second) - a;
                for (const Piece &p : wordPieces[w])
                    word.rects << p.rect;
                word.page = wordPieces[w].isEmpty() ? page : wordPieces[w].first().page;
                s.words << word;
            }
            s.page = s.words.isEmpty() ? page : s.words.first().page;
            const bool last = si == spans.size() - 1;
            s.paragraphEnd = last && end;
            out << s;
        }
        if (end)
            ++*paragraph;
        text.clear();
        wordPieces.clear();
        wordSpans.clear();
    };

    // a sentence left open at the end of the previous page continues here
    if (carry && !carry->text.isEmpty()) {
        text = carry->text;
        for (const Word &w : carry->words) {
            QList<Piece> ps;
            for (const QRectF &r : w.rects)
                ps << Piece{w.page, r};
            wordPieces << ps;
            wordSpans << qMakePair(w.start, w.end);
        }
        *carry = Sentence();
    }

    int prevKept = -1;
    for (int i = 0; i < ls.size(); ++i) {
        if (!keep[i])
            continue;
        const Line &l = ls[i];
        const bool isHeading = l.height > body * 1.25 && l.text.size() < 120;
        if (prevKept >= 0) {
            const Line &p = ls[prevKept];
            const qreal g = l.rect.top() - p.rect.bottom();
            const bool bigGap = g > gap * 1.8 + 0.6 * body;
            const bool indent = l.rect.left() > left + body * 0.9 && p.rect.right() < left + width * 0.85;
            const bool shortPrev = p.rect.right() < left + width * 0.7 && isEnd(p.text.trimmed().isEmpty() ? QChar() : p.text.trimmed().back());
            if (bigGap || indent || shortPrev || isHeading || heading)
                flushParagraph(true);
        }
        heading = isHeading;
        // words of the line, skipping footnote markers (small, raised, numeric)
        for (int k = 0; k < l.words.size(); ++k) {
            const PdfWord &w = l.words[k];
            if (w.rect.height() < l.height * 0.75 && numeric(w.text) && k > 0)
                continue;
            QString t = w.text;
            // hyphenation at the end of the previous line: join the halves
            if (!wordSpans.isEmpty() && text.endsWith(QLatin1Char('-')) && k == 0 && !t.isEmpty() && t.at(0).isLower()) {
                text.chop(1);
                wordSpans.last().second -= 1;
                text += t;
                wordSpans.last().second = int(text.size());
                wordPieces.last() << Piece{page, w.rect};
                continue;
            }
            if (!text.isEmpty() && !text.endsWith(QLatin1Char(' ')))
                text += QLatin1Char(' ');
            const int s0 = int(text.size());
            text += t;
            wordSpans << qMakePair(s0, int(text.size()));
            wordPieces << QList<Piece>{Piece{page, w.rect}};
        }
        prevKept = i;
    }
    // the last paragraph may continue on the next page (no end mark)
    const QString tail = text.trimmed();
    const bool open = carry && !tail.isEmpty() && !isEnd(tail.back()) && !(isCloser(tail.back()) && tail.size() > 1 && isEnd(tail.at(tail.size() - 2)))
                      && !heading;
    if (open) {
        // keep the complete sentences, carry the unfinished one
        const auto spans = splitSentences(text, lang);
        const int cut = spans.size() > 1 ? spans[spans.size() - 2].second : 0;
        if (cut > 0) {
            const QString rest = text.mid(cut);
            QList<QList<Piece>> restPieces;
            QList<QPair<int, int>> restSpans;
            QList<QList<Piece>> headPieces;
            QList<QPair<int, int>> headSpans;
            for (int w = 0; w < wordSpans.size(); ++w) {
                if (wordSpans[w].first >= cut) {
                    restSpans << qMakePair(wordSpans[w].first - cut, wordSpans[w].second - cut);
                    restPieces << wordPieces[w];
                } else {
                    headSpans << wordSpans[w];
                    headPieces << wordPieces[w];
                }
            }
            text = text.left(cut);
            wordSpans = headSpans;
            wordPieces = headPieces;
            flushParagraph(false);
            text = rest;
            wordSpans = restSpans;
            wordPieces = restPieces;
        }
        Sentence c;
        c.text = text;
        for (int w = 0; w < wordSpans.size(); ++w) {
            Word word;
            word.start = wordSpans[w].first;
            word.end = wordSpans[w].second;
            for (const Piece &p : wordPieces[w])
                word.rects << p.rect;
            word.page = wordPieces[w].isEmpty() ? page : wordPieces[w].first().page;
            c.words << word;
        }
        c.page = c.words.isEmpty() ? page : c.words.first().page;
        *carry = c;
    } else {
        flushParagraph(true);
    }
    return out;
}

QList<int> wordsInRange(const Sentence &s, int start, int end)
{
    QList<int> out;
    for (int i = 0; i < s.words.size(); ++i) {
        if (s.words[i].end > start && s.words[i].start < end)
            out << i;
    }
    return out;
}

QHash<int, QList<QRectF>> lineRects(const Sentence &s, int firstWord, int lastWord)
{
    QHash<int, QList<QRectF>> out;
    if (lastWord < 0)
        lastWord = int(s.words.size()) - 1;
    for (int i = std::max(0, firstWord); i <= lastWord && i < s.words.size(); ++i) {
        const Word &w = s.words[i];
        for (const QRectF &r : w.rects) {
            QList<QRectF> &list = out[w.page];
            bool merged = false;
            for (QRectF &m : list) {
                const qreal h = std::min(m.height(), r.height());
                const qreal overlap = std::min(m.bottom(), r.bottom()) - std::max(m.top(), r.top());
                if (overlap > 0.5 * h && r.left() - m.right() < h * 2.5 && r.right() > m.left()) {
                    m = m.united(r);
                    merged = true;
                    break;
                }
            }
            if (!merged)
                list << r;
        }
    }
    return out;
}

} // namespace ReadingText
