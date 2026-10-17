/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reader.h"
#include "pdfdocument.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QWindow>
#include <QMimeDatabase>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QStandardPaths>

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KService>

static const QString AssistantService = QStringLiteral("com.lingmo.Assistant");
static const QString AssistantPath = QStringLiteral("/Assistant");
static const QString AssistantInterface = QStringLiteral("com.lingmo.Assistant");
static const QString AssistantBinary = QStringLiteral("lingmo-assistant");
static const QString SelfDesktopId = QStringLiteral("lingmo-reader.desktop");

Reader::Reader(PdfDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    refreshAssistant();
}

QString Reader::toLocalPath(const QVariant &urlOrPath) const
{
    if (urlOrPath.metaType() == QMetaType::fromType<QUrl>()) {
        const QUrl url = urlOrPath.toUrl();
        return url.isLocalFile() ? url.toLocalFile() : QString();
    }
    const QString s = urlOrPath.toString();
    if (s.startsWith(QLatin1String("file:")))
        return QUrl(s).toLocalFile();
    if (s.contains(QLatin1String("://")))
        return QString();
    return s.isEmpty() ? QString() : QFileInfo(s).absoluteFilePath();
}

QUrl Reader::toUrl(const QString &path) const
{
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

bool Reader::exists(const QString &path) const
{
    return !path.isEmpty() && QFileInfo(path).isFile();
}

bool Reader::isSupported(const QString &path) const
{
    if (path.isEmpty())
        return false;
    const QMimeType mime = QMimeDatabase().mimeTypeForFile(path);
    return mime.inherits(QStringLiteral("application/pdf"))
        || path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive);
}

QString Reader::fileName(const QString &path) const
{
    return QFileInfo(path).fileName();
}

QString Reader::folder(const QString &path) const
{
    QString dir = QFileInfo(path).absolutePath();
    const QString home = QDir::homePath();
    if (dir == home)
        return QStringLiteral("~");
    if (dir.startsWith(home + QLatin1Char('/')))
        dir = QLatin1Char('~') + dir.mid(home.size());
    return dir;
}

QVariantList Reader::openWithApplications(const QString &path) const
{
    QString mime = QStringLiteral("application/pdf");
    if (!path.isEmpty())
        mime = QMimeDatabase().mimeTypeForFile(path).name();
    QVariantList apps;
    const KService::List services = KApplicationTrader::queryByMimeType(mime);
    for (const KService::Ptr &service : services) {
        if (service->storageId() == SelfDesktopId || service->noDisplay())
            continue;
        apps.append(QVariantMap{
            {QStringLiteral("id"), service->storageId()},
            {QStringLiteral("name"), service->name()},
            {QStringLiteral("icon"), service->icon()},
        });
    }
    return apps;
}

bool Reader::openWith(const QString &appId, const QString &path) const
{
    const KService::Ptr service = KService::serviceByStorageId(appId);
    if (!service || path.isEmpty())
        return false;
    auto *job = new KIO::ApplicationLauncherJob(service);
    job->setUrls({QUrl::fromLocalFile(path)});
    job->start();
    return true;
}

bool Reader::print(int currentPage, const QString &title)
{
    const std::shared_ptr<PdfSnapshot> doc = m_document ? m_document->snapshot() : nullptr;
    if (!doc || doc->pageSizes.isEmpty())
        return false;
    const int pageCount = doc->pageSizes.size();

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(title.isEmpty() ? QFileInfo(m_document->path()).fileName() : title);
    printer.setFromTo(1, pageCount);

    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(tr("Print"));
    dialog.setOptions(QAbstractPrintDialog::PrintToFile | QAbstractPrintDialog::PrintPageRange
                      | QAbstractPrintDialog::PrintCollateCopies | QAbstractPrintDialog::PrintCurrentPage);
    dialog.setMinMax(1, pageCount);
    // Centred over the reader window, not at the screen corner
    QWindow *parentWindow = QGuiApplication::focusWindow();
    dialog.winId();
    if (parentWindow && dialog.windowHandle())
        dialog.windowHandle()->setTransientParent(parentWindow);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    int from = 0;
    int to = pageCount - 1;
    switch (printer.printRange()) {
    case QPrinter::PageRange:
        from = qBound(0, printer.fromPage() - 1, pageCount - 1);
        to = qBound(from, printer.toPage() - 1, pageCount - 1);
        break;
    case QPrinter::CurrentPage:
        from = to = qBound(0, currentPage, pageCount - 1);
        break;
    default:
        break;
    }

    QPainter painter;
    if (!painter.begin(&printer))
        return false;

    const QRectF paint = printer.pageLayout().paintRectPixels(printer.resolution());
    const QRectF area(0, 0, paint.width(), paint.height());
    // 300 dpi is plenty for paper and keeps big pages from eating memory
    const double dpi = qMin(300, printer.resolution());

    for (int page = from; page <= to; ++page) {
        if (page > from)
            printer.newPage();
        const QSizeF points = doc->pageSizes.at(page);
        if (points.isEmpty())
            continue;
        const qreal scale = qMin(area.width() / points.width(), area.height() / points.height());
        const QSizeF target = points * scale;
        const QImage image = doc->renderAtDpi(page, dpi);
        const QRectF rect(area.x() + (area.width() - target.width()) / 2,
                          area.y() + (area.height() - target.height()) / 2,
                          target.width(), target.height());
        painter.drawImage(rect, image);
    }
    painter.end();
    return true;
}

void Reader::openExternal(const QString &url) const
{
    const QUrl u(url);
    if (u.isValid())
        QDesktopServices::openUrl(u);
}

void Reader::copyText(const QString &text) const
{
    if (!text.isEmpty())
        QGuiApplication::clipboard()->setText(text);
}

void Reader::refreshAssistant()
{
    bool available = !QStandardPaths::findExecutable(AssistantBinary).isEmpty();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!available && bus.isConnected() && bus.interface()) {
        available = bus.interface()->isServiceRegistered(AssistantService).value();
        if (!available) {
            const QDBusReply<QStringList> names = bus.interface()->activatableServiceNames();
            available = names.isValid() && names.value().contains(AssistantService);
        }
    }
    if (available != m_assistantAvailable) {
        m_assistantAvailable = available;
        Q_EMIT assistantAvailableChanged();
    }
}

bool Reader::askAssistant(const QString &text) const
{
    if (!m_assistantAvailable || text.trimmed().isEmpty())
        return false;

    QDBusConnection bus = QDBusConnection::sessionBus();
    const bool canActivate = bus.isConnected() && bus.interface()
        && (bus.interface()->isServiceRegistered(AssistantService).value()
            || bus.interface()->activatableServiceNames().value().contains(AssistantService));
    if (canActivate) {
        QDBusMessage msg = QDBusMessage::createMethodCall(AssistantService, AssistantPath,
                                                          AssistantInterface, QStringLiteral("ask"));
        msg << text;
        // Activation can take a moment; fall back to the binary if the call fails
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, 25000), qApp);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, qApp, [text](QDBusPendingCallWatcher *w) {
            if (w->isError() && !QStandardPaths::findExecutable(AssistantBinary).isEmpty())
                QProcess::startDetached(AssistantBinary, {QStringLiteral("--ask"), text});
            w->deleteLater();
        });
        return true;
    }
    // Not running and no D-Bus activation file: the binary hands the question over itself
    return QProcess::startDetached(AssistantBinary, {QStringLiteral("--ask"), text});
}
