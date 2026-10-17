/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

class PdfDocument;

// image://pdfpage/<generation>/<page>/<width>: pages rendered by Poppler in
// worker threads. A request for an older generation (a closed document)
// returns an empty image.
class PageImageProvider : public QQuickAsyncImageProvider
{
public:
    explicit PageImageProvider(PdfDocument *document);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    PdfDocument *m_document;
    QThreadPool m_pool;
};
