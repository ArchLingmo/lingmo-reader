/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "pageimageprovider.h"
#include "pdfdocument.h"

#include <QQuickTextureFactory>
#include <QRunnable>

#include <atomic>

namespace {

class PageResponse : public QQuickImageResponse, public QRunnable
{
public:
    PageResponse(std::shared_ptr<PdfSnapshot> snapshot, int page, int width)
        : m_snapshot(std::move(snapshot))
        , m_page(page)
        , m_width(width)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        if (!m_canceled && m_snapshot)
            m_image = m_snapshot->render(m_page, m_width);
        m_snapshot.reset();
        Q_EMIT finished();
    }

    void cancel() override { m_canceled = true; }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() && !m_canceled ? QStringLiteral("page could not be rendered") : QString();
    }

private:
    std::shared_ptr<PdfSnapshot> m_snapshot;
    int m_page;
    int m_width;
    QImage m_image;
    std::atomic<bool> m_canceled = false;
};

} // namespace

PageImageProvider::PageImageProvider(PdfDocument *document)
    : m_document(document)
{
    // Poppler work is serialised per document; two threads keep thumbnails
    // flowing while a big page renders
    m_pool.setMaxThreadCount(2);
}

QQuickImageResponse *PageImageProvider::requestImageResponse(const QString &id, const QSize &)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    std::shared_ptr<PdfSnapshot> snapshot;
    int page = -1, width = 0;
    if (parts.size() >= 3) {
        const int generation = parts.at(0).toInt();
        page = parts.at(1).toInt();
        width = parts.at(2).toInt();
        // The document object lives in the GUI thread; reading the pointer is a copy
        snapshot = m_document->snapshot();
        if (snapshot && snapshot->generation != generation)
            snapshot.reset();
    }
    auto *response = new PageResponse(snapshot, page, width);
    m_pool.start(response);
    return response;
}
