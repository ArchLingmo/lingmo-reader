/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QVariant>

#include <memory>

#include "bookconverter.h"

namespace Poppler {
class Document;
}

// One word of a page's text layer, in page points
struct PdfWord {
    QString text;
    QRectF rect;
    bool spaceAfter = false;
};

// A loaded document as seen by worker threads (page rendering, search).
// Poppler isn't used concurrently: every access goes through the mutex.
// Shared through std::shared_ptr so closing a document never pulls it from
// under a running render.
class PdfSnapshot
{
public:
    std::unique_ptr<Poppler::Document> document;
    int generation = 0;
    QList<QSizeF> pageSizes;   // points

    // width in device pixels; empty image on failure
    QImage render(int page, int width) const;
    QImage renderAtDpi(int page, double dpi) const;
    // Words in reading order (cached)
    QList<PdfWord> words(int page) const;
    // Poppler search, case-insensitive; rectangles in points
    QList<QRectF> search(int page, const QString &text) const;

    mutable QMutex mutex;

private:
    mutable QHash<int, QList<PdfWord>> m_words;
};

// The open document, exposed to QML as "pdf"
class PdfDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(bool locked READ locked NOTIFY statusChanged)
    Q_PROPERTY(QString path READ path NOTIFY statusChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY statusChanged)
    Q_PROPERTY(QString title READ title NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(int generation READ generation NOTIFY statusChanged)
    Q_PROPERTY(qreal maxPageWidth READ maxPageWidth NOTIFY statusChanged)
    Q_PROPERTY(qreal maxPageHeight READ maxPageHeight NOTIFY statusChanged)
    // Flattened table of contents: [{ title, depth, page, top, hasChildren }]
    Q_PROPERTY(QVariantList outline READ outline NOTIFY statusChanged)
    // "pdf", or the format typeset into a PDF: "epub", "txt", "docx", "odt"
    Q_PROPERTY(QString sourceFormat READ sourceFormat NOTIFY statusChanged)
    Q_PROPERTY(QString author READ author NOTIFY statusChanged)
    Q_PROPERTY(QString language READ language NOTIFY statusChanged)
    // the cover image of an EPUB, if any (for audiobook export)
    Q_PROPERTY(QString coverPath READ coverPath NOTIFY statusChanged)
    // the PDF poppler actually reads (the typeset one for EPUB/TXT/DOCX/ODT)
    Q_PROPERTY(QString renderedPath READ renderedPath NOTIFY statusChanged)
    // EPUB/TXT/DOCX/ODT being typeset in the background (async conversion only)
    Q_PROPERTY(bool converting READ converting NOTIFY statusChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

public:
    enum Status { Null, Ready, Locked, Error, Converting };
    Q_ENUM(Status)

    explicit PdfDocument(QObject *parent = nullptr);
    ~PdfDocument() override;

    int status() const { return m_status; }
    bool ready() const { return m_status == Ready; }
    bool locked() const { return m_status == Locked; }
    QString path() const { return m_path; }
    int pageCount() const;
    QString title() const { return m_title; }
    QString error() const { return m_error; }
    int generation() const { return m_generation; }
    qreal maxPageWidth() const;
    qreal maxPageHeight() const;
    QVariantList outline() const { return m_outline; }
    QString sourceFormat() const { return m_sourceFormat; }
    QString author() const { return m_author; }
    QString language() const { return m_language; }
    QString coverPath() const { return m_coverPath; }
    QString renderedPath() const { return m_renderedPath; }
    bool converting() const { return m_status == Converting; }
    qreal progress() const { return m_progress; }
    // The app converts in a worker thread (load() returns Converting, then
    // statusChanged); the tests keep the synchronous load
    void setAsyncConversion(bool on) { m_async = on; }

    // Ready, Locked (needs a password), Error, or Converting (async conversion)
    Q_INVOKABLE int load(const QString &path, const QString &password = QString());
    // true when the password opened the document
    Q_INVOKABLE bool unlock(const QString &password);
    Q_INVOKABLE void close();

    Q_INVOKABLE QSizeF pageSize(int page) const;
    Q_INVOKABLE QString pageLabel(int page) const;
    // "image://pdfpage/<generation>/<page>/<width>"
    Q_INVOKABLE QString imageSource(int page, int width) const;
    // [{ x, y, width, height, page, top, url }] in points; page -1 for web links
    Q_INVOKABLE QVariantList links(int page) const;
    // Text selection from one point to another (points): { text, polygons }
    Q_INVOKABLE QVariantMap selection(int page, const QPointF &from, const QPointF &to) const;
    Q_INVOKABLE QVariantMap selectAll(int page) const;
    // The word under a point (double click)
    Q_INVOKABLE QVariantMap selectWord(int page, const QPointF &point) const;
    Q_INVOKABLE QString pageText(int page) const;

    // Thread-safe: the image provider asks from its own thread
    std::shared_ptr<PdfSnapshot> snapshot() const;

    // Selection over a word list, shared with the tests
    static QVariantMap selectWords(const QList<PdfWord> &words, int first, int last);
    static int wordAt(const QList<PdfWord> &words, const QPointF &point);

Q_SIGNALS:
    void statusChanged();
    void progressChanged();

private:
    void finishLoad(std::unique_ptr<Poppler::Document> doc);
    void buildOutline();
    void setStatus(Status status, const QString &error = QString());
    void setSnapshot(std::shared_ptr<PdfSnapshot> snapshot);
    void finishConversion(const BookConverter::Result &book);

    mutable QMutex m_snapshotLock;

    std::shared_ptr<PdfSnapshot> m_snapshot;
    std::unique_ptr<Poppler::Document> m_pending;   // locked, waiting for a password
    Status m_status = Null;
    QString m_path;
    QString m_title;
    QString m_error;
    QVariantList m_outline;
    QString m_sourceFormat;
    QString m_author;
    QString m_language;
    QString m_coverPath;
    QString m_renderedPath;
    QVariantList m_convertedOutline;
    int m_generation = 0;
    qreal m_progress = 0;
    bool m_async = false;
};
