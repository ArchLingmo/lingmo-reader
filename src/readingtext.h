/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QHash>
#include <QList>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>

#include "pdfdocument.h"

// What "Ouvir" reads: the text of the pages as sentences, without what a
// person reading aloud would skip — running headers and footers, page
// numbers, footnotes and the little reference markers that point to them —
// with every word's place on the page, so the sentence and the word being
// spoken can be highlighted.
//
// Pure (Qt Core/Gui only), tested in tst_readingtext.
namespace ReadingText {

struct Word {
    int page = 0;
    QList<QRectF> rects;   // usually one; two for a word hyphenated across lines
    int start = 0;         // offsets in Sentence::text
    int end = 0;
};

struct Sentence {
    QString text;
    QList<Word> words;
    int page = 0;              // page of the first word
    int paragraph = 0;         // running paragraph number in the document
    bool paragraphEnd = false; // last sentence of its paragraph
    bool heading = false;      // a title line: read with a longer pause
};

struct Line {
    QList<PdfWord> words;
    QRectF rect;
    qreal height = 0;   // median word height
    QString text;
};

// Lines of a page in reading order (poppler's word order, split where the
// baseline jumps)
QList<Line> lines(const QList<PdfWord> &words);

// Running headers/footers: normalised text of lines that repeat at the top or
// bottom of many pages (digits ignored, so "Capítulo 3 · 41" matches)
struct Furniture {
    QSet<QString> repeated;
    qreal bodyHeight = 0;   // median line height of body text
};
// pagesLines: a sample of pages (index -> lines) and their sizes
Furniture detectFurniture(const QHash<int, QList<Line>> &pagesLines, const QHash<int, QSizeF> &sizes);
QString furnitureKey(const QString &lineText);

// One page's sentences. `paragraph` is the running paragraph counter (in/out);
// a sentence cut by the end of the page continues on the next one when the
// caller passes the unfinished tail in `carry` (in/out).
QList<Sentence> pageSentences(int page, const QList<PdfWord> &words, QSizeF pageSize, const Furniture &furniture,
                              int *paragraph, Sentence *carry, const QString &lang = QStringLiteral("pt"));

// Sentence boundaries in a paragraph's text: [start, end) pairs (trimmed)
QList<QPair<int, int>> splitSentences(const QString &text, const QString &lang = QStringLiteral("pt"));

// Words whose characters overlap [start, end) of the sentence text
QList<int> wordsInRange(const Sentence &s, int start, int end);

// Rectangles of words merged per line (for the sentence highlight), per page
QHash<int, QList<QRectF>> lineRects(const Sentence &s, int firstWord = 0, int lastWord = -1);

} // namespace ReadingText
