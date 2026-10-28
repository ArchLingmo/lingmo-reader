/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QSizeF>
#include <QString>
#include <QVariantList>

#include <functional>

// EPUB, plain text, DOCX and ODT become a typeset PDF in the cache, so the
// Reader shows them with real pages and every PDF feature (search, selection,
// notes, read aloud with highlight, print) works the same way.
//
// The table of contents comes from the book (EPUB nav/NCX, headings of
// DOCX/ODT, chapter lines of plain text) and is mapped to the pages of the
// typeset PDF. The result is cached by path, size and modification time.
namespace BookConverter {

struct Result {
    QString pdfPath;
    QString title;
    QString author;
    QString language;
    QString coverPath;      // extracted cover image, if the book has one
    QString format;         // "epub", "txt", "docx", "odt"
    QVariantList outline;   // [{ title, depth, page, top, hasChildren }] like PdfDocument::outline
    QString error;
    bool ok() const { return error.isEmpty() && !pdfPath.isEmpty(); }
};

// Called with the fraction done (0..1); may be called from a worker thread
using Progress = std::function<void(qreal)>;

// Formats converted (lowercase suffixes)
QStringList suffixes();
// "epub", "txt", "docx", "odt", or "" (a PDF or unknown). By extension; a file
// without a known one is recognised by its content (zip with an OPF, word/, ...)
QString formatOf(const QString &path);
bool needsConversion(const QString &path);

// Converts (or returns the cached conversion). cacheDir empty = ~/.cache/lingmo-reader/books.
// Safe to run in a worker thread.
Result convert(const QString &path, const QString &cacheDir = QString(), const Progress &progress = {});

// ---- pieces, public for the tests

struct Chapter {
    QString title;
    int depth = 0;
    QString anchor;   // fragment the TOC points to ("" = start of the chapter)
};

struct Book {
    QString title, author, language, format;
    QString html;                     // the whole body, chapters separated by page breaks
    QList<Chapter> toc;               // titles with anchors present in html
    QHash<QString, QByteArray> images;// resource name used in html -> bytes
    QByteArray cover;                 // image bytes
};

Book readEpub(const QString &path, QString *error, const Progress &progress = {});
Book readText(const QString &path, QString *error);
Book readDocx(const QString &path, QString *error);
Book readOdt(const QString &path, QString *error);

// Typesets the book into a PDF; fills outline pages
bool typeset(const Book &book, const QString &pdfPath, QVariantList *outline, QString *error,
             QSizeF pagePoints = QSizeF(420, 595), const Progress &progress = {});   // A5: comfortable on screen and close to a printed book

} // namespace BookConverter
