/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pdfdocument.h"

#include <QFileInfo>
#include <QLineF>
#include <QMutexLocker>
#include <QUrl>

#include <poppler-link.h>
#include <poppler-qt6.h>

#include <cmath>
#include <functional>

// ------------------------------------------------------------------ snapshot

QImage PdfSnapshot::renderAtDpi(int page, double dpi) const
{
    QMutexLocker lock(&mutex);
    if (!document || page < 0 || page >= pageSizes.size())
        return QImage();
    const std::unique_ptr<Poppler::Page> p = document->page(page);
    if (!p)
        return QImage();
    return p->renderToImage(dpi, dpi);
}

QImage PdfSnapshot::render(int page, int width) const
{
    if (page < 0 || page >= pageSizes.size() || width <= 0)
        return QImage();
    const QSizeF size = pageSizes.at(page);
    if (size.width() <= 0)
        return QImage();
    // Keep a single page under ~64 Mpx whatever the zoom
    double dpi = width * 72.0 / size.width();
    const double maxDpi = std::sqrt(64e6 / qMax(1.0, size.width() * size.height())) * 72.0;
    dpi = qBound(4.0, dpi, maxDpi);
    return renderAtDpi(page, dpi);
}

QList<PdfWord> PdfSnapshot::words(int page) const
{
    QMutexLocker lock(&mutex);
    if (!document || page < 0 || page >= pageSizes.size())
        return {};
    auto it = m_words.constFind(page);
    if (it != m_words.constEnd())
        return it.value();
    QList<PdfWord> list;
    if (const std::unique_ptr<Poppler::Page> p = document->page(page)) {
        const auto boxes = p->textList();
        list.reserve(int(boxes.size()));
        for (const auto &box : boxes) {
            PdfWord w;
            w.text = box->text();
            w.rect = box->boundingBox();
            w.spaceAfter = box->hasSpaceAfter();
            list.append(w);
        }
    }
    m_words.insert(page, list);
    return list;
}

QList<QRectF> PdfSnapshot::search(int page, const QString &text) const
{
    QMutexLocker lock(&mutex);
    if (!document || page < 0 || page >= pageSizes.size() || text.isEmpty())
        return {};
    const std::unique_ptr<Poppler::Page> p = document->page(page);
    return p ? p->search(text, Poppler::Page::IgnoreCase) : QList<QRectF>();
}

// ------------------------------------------------------------------ document

PdfDocument::PdfDocument(QObject *parent)
    : QObject(parent)
{
}

PdfDocument::~PdfDocument() = default;

std::shared_ptr<PdfSnapshot> PdfDocument::snapshot() const
{
    QMutexLocker lock(&m_snapshotLock);
    return m_snapshot;
}

void PdfDocument::setSnapshot(std::shared_ptr<PdfSnapshot> snapshot)
{
    QMutexLocker lock(&m_snapshotLock);
    m_snapshot = std::move(snapshot);
}

int PdfDocument::pageCount() const
{
    return m_snapshot ? m_snapshot->pageSizes.size() : 0;
}

qreal PdfDocument::maxPageWidth() const
{
    qreal w = 0;
    if (m_snapshot) {
        for (const QSizeF &s : m_snapshot->pageSizes)
            w = qMax(w, s.width());
    }
    return w;
}

qreal PdfDocument::maxPageHeight() const
{
    qreal h = 0;
    if (m_snapshot) {
        for (const QSizeF &s : m_snapshot->pageSizes)
            h = qMax(h, s.height());
    }
    return h;
}

void PdfDocument::setStatus(Status status, const QString &error)
{
    m_status = status;
    m_error = error;
    Q_EMIT statusChanged();
}

int PdfDocument::load(const QString &path, const QString &password)
{
    setSnapshot(nullptr);
    m_pending.reset();
    m_outline.clear();
    m_title.clear();
    ++m_generation;

    QString local = path;
    if (local.startsWith(QLatin1String("file:")))
        local = QUrl(local).toLocalFile();
    m_path = local;
    if (local.isEmpty()) {
        setStatus(Null);
        return m_status;
    }
    if (!QFileInfo(local).isReadable()) {
        setStatus(Error, tr("the file can't be read"));
        return m_status;
    }

    const QByteArray pw = password.toUtf8();
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(local, pw, pw);
    if (!doc) {
        setStatus(Error, tr("it is damaged or not a PDF document"));
        return m_status;
    }
    if (doc->isLocked()) {
        m_pending = std::move(doc);
        setStatus(Locked);
        return m_status;
    }
    finishLoad(std::move(doc));
    return m_status;
}

bool PdfDocument::unlock(const QString &password)
{
    if (!m_pending)
        return false;
    const QByteArray pw = password.toUtf8();
    m_pending->unlock(pw, pw);
    // older (RC4/AES-128) files expect Latin-1
    if (m_pending->isLocked() && password.toLatin1() != pw)
        m_pending->unlock(password.toLatin1(), password.toLatin1());
    if (m_pending->isLocked()) {
        setStatus(Locked);
        return false;
    }
    finishLoad(std::move(m_pending));
    return m_status == Ready;
}

void PdfDocument::close()
{
    setSnapshot(nullptr);
    m_pending.reset();
    m_outline.clear();
    m_title.clear();
    m_path.clear();
    ++m_generation;
    setStatus(Null);
}

void PdfDocument::finishLoad(std::unique_ptr<Poppler::Document> doc)
{
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    doc->setRenderHint(Poppler::Document::TextHinting);
    doc->setRenderHint(Poppler::Document::TextSlightHinting);
    doc->setRenderHint(Poppler::Document::ThinLineShape);

    auto snap = std::make_shared<PdfSnapshot>();
    const int n = doc->numPages();
    snap->pageSizes.reserve(n);
    for (int i = 0; i < n; ++i) {
        const std::unique_ptr<Poppler::Page> p = doc->page(i);
        snap->pageSizes.append(p ? p->pageSizeF() : QSizeF(612, 792));
    }
    m_title = doc->title().trimmed();
    snap->document = std::move(doc);
    snap->generation = m_generation;
    setSnapshot(snap);
    if (n == 0) {
        setSnapshot(nullptr);
        setStatus(Error, tr("the document has no pages"));
        return;
    }
    buildOutline();
    setStatus(Ready);
}

// Page (0-based) and top (points, -1 when unknown) of a destination
static void resolveDestination(Poppler::Document *doc, const QList<QSizeF> &sizes,
                               const Poppler::LinkDestination &dest, int *page, qreal *top)
{
    *page = -1;
    *top = -1;
    Poppler::LinkDestination d = dest;
    if (d.pageNumber() <= 0 && !d.destinationName().isEmpty()) {
        if (std::unique_ptr<Poppler::LinkDestination> named = doc->linkDestination(d.destinationName()))
            d = *named;
    }
    if (d.pageNumber() <= 0 || d.pageNumber() > sizes.size())
        return;
    *page = d.pageNumber() - 1;
    if (d.isChangeTop())
        *top = d.top() * sizes.at(*page).height();
}

void PdfDocument::buildOutline()
{
    m_outline.clear();
    if (!m_snapshot)
        return;
    QMutexLocker lock(&m_snapshot->mutex);
    Poppler::Document *doc = m_snapshot->document.get();
    const QList<QSizeF> &sizes = m_snapshot->pageSizes;

    std::function<void(const QList<Poppler::OutlineItem> &, int)> walk =
        [&](const QList<Poppler::OutlineItem> &items, int depth) {
            for (const Poppler::OutlineItem &item : items) {
                if (item.isNull())
                    continue;
                int page = -1;
                qreal top = -1;
                if (const auto dest = item.destination())
                    resolveDestination(doc, sizes, *dest, &page, &top);
                const QList<Poppler::OutlineItem> children = item.children();
                m_outline.append(QVariantMap{
                    {QStringLiteral("title"), item.name().simplified()},
                    {QStringLiteral("depth"), depth},
                    {QStringLiteral("page"), page},
                    {QStringLiteral("top"), top},
                    {QStringLiteral("hasChildren"), !children.isEmpty()},
                });
                walk(children, depth + 1);
            }
        };
    walk(doc->outline(), 0);
}

QSizeF PdfDocument::pageSize(int page) const
{
    if (!m_snapshot || page < 0 || page >= m_snapshot->pageSizes.size())
        return QSizeF(612, 792);
    return m_snapshot->pageSizes.at(page);
}

QString PdfDocument::pageLabel(int page) const
{
    if (!m_snapshot || page < 0 || page >= m_snapshot->pageSizes.size())
        return QString();
    QMutexLocker lock(&m_snapshot->mutex);
    const std::unique_ptr<Poppler::Page> p = m_snapshot->document->page(page);
    const QString label = p ? p->label() : QString();
    return label.isEmpty() ? QString::number(page + 1) : label;
}

QString PdfDocument::imageSource(int page, int width) const
{
    if (!m_snapshot)
        return QString();
    return QStringLiteral("image://pdfpage/%1/%2/%3").arg(m_generation).arg(page).arg(qMax(1, width));
}

QVariantList PdfDocument::links(int page) const
{
    QVariantList result;
    if (!m_snapshot || page < 0 || page >= m_snapshot->pageSizes.size())
        return result;
    QMutexLocker lock(&m_snapshot->mutex);
    Poppler::Document *doc = m_snapshot->document.get();
    const QSizeF size = m_snapshot->pageSizes.at(page);
    const std::unique_ptr<Poppler::Page> p = doc->page(page);
    if (!p)
        return result;
    for (const auto &link : p->links()) {
        const QRectF a = link->linkArea().normalized();
        const QRectF r(a.x() * size.width(), a.y() * size.height(), a.width() * size.width(), a.height() * size.height());
        int target = -1;
        qreal top = -1;
        QString url;
        if (link->linkType() == Poppler::Link::Goto) {
            const auto *go = static_cast<const Poppler::LinkGoto *>(link.get());
            if (go->isExternal())
                continue;
            resolveDestination(doc, m_snapshot->pageSizes, go->destination(), &target, &top);
            if (target < 0)
                continue;
        } else if (link->linkType() == Poppler::Link::Browse) {
            url = static_cast<const Poppler::LinkBrowse *>(link.get())->url();
            if (url.isEmpty())
                continue;
        } else {
            continue;
        }
        result.append(QVariantMap{
            {QStringLiteral("x"), r.x()},
            {QStringLiteral("y"), r.y()},
            {QStringLiteral("width"), r.width()},
            {QStringLiteral("height"), r.height()},
            {QStringLiteral("page"), target},
            {QStringLiteral("top"), top},
            {QStringLiteral("url"), url},
        });
    }
    return result;
}

int PdfDocument::wordAt(const QList<PdfWord> &words, const QPointF &point)
{
    int best = -1;
    qreal bestDistance = 1e12;
    for (int i = 0; i < words.size(); ++i) {
        const QRectF &r = words.at(i).rect;
        if (r.contains(point))
            return i;
        const qreal dx = qMax(qMax(r.left() - point.x(), 0.0), point.x() - r.right());
        // being on the same line matters more than being horizontally close
        const qreal dy = qMax(qMax(r.top() - point.y(), 0.0), point.y() - r.bottom()) * 3;
        const qreal d = dx * dx + dy * dy;
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

static bool sameLine(const QRectF &a, const QRectF &b)
{
    const qreal overlap = qMin(a.bottom(), b.bottom()) - qMax(a.top(), b.top());
    return overlap > 0.5 * qMin(a.height(), b.height());
}

QVariantMap PdfDocument::selectWords(const QList<PdfWord> &words, int first, int last)
{
    QString text;
    QList<QPolygonF> polygons;
    if (first > last)
        std::swap(first, last);
    first = qMax(0, first);
    last = qMin(int(words.size()) - 1, last);
    QRectF line;
    for (int i = first; i <= last; ++i) {
        const PdfWord &w = words.at(i);
        text += w.text;
        if (line.isNull()) {
            line = w.rect;
        } else if (sameLine(line, w.rect)) {
            line = line.united(w.rect);
        } else {
            polygons.append(QPolygonF(line));
            line = w.rect;
        }
        if (i < last) {
            if (!sameLine(w.rect, words.at(i + 1).rect))
                text += QLatin1Char('\n');
            else if (w.spaceAfter)
                text += QLatin1Char(' ');
        }
    }
    if (!line.isNull())
        polygons.append(QPolygonF(line));
    return {
        {QStringLiteral("text"), text},
        {QStringLiteral("polygons"), QVariant::fromValue(polygons)},
    };
}

QVariantMap PdfDocument::selection(int page, const QPointF &from, const QPointF &to) const
{
    const QVariantMap empty{
        {QStringLiteral("text"), QString()},
        {QStringLiteral("polygons"), QVariant::fromValue(QList<QPolygonF>())},
    };
    if (!m_snapshot || QLineF(from, to).length() < 1.0)
        return empty;
    const QList<PdfWord> words = m_snapshot->words(page);
    const int a = wordAt(words, from);
    const int b = wordAt(words, to);
    if (a < 0 || b < 0)
        return empty;
    return selectWords(words, a, b);
}

QVariantMap PdfDocument::selectAll(int page) const
{
    if (!m_snapshot)
        return {};
    const QList<PdfWord> words = m_snapshot->words(page);
    return selectWords(words, 0, words.size() - 1);
}

QVariantMap PdfDocument::selectWord(int page, const QPointF &point) const
{
    if (!m_snapshot)
        return {};
    const QList<PdfWord> words = m_snapshot->words(page);
    for (int i = 0; i < words.size(); ++i) {
        if (words.at(i).rect.adjusted(-1, -1, 1, 1).contains(point))
            return selectWords(words, i, i);
    }
    return {};
}

QString PdfDocument::pageText(int page) const
{
    return selectAll(page).value(QStringLiteral("text")).toString();
}
