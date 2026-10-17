/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>
#include <QUrl>

#include "annotationstore.h"
#include "pageimageprovider.h"
#include "pdfdocument.h"
#include "reader.h"
#include "recentfiles.h"
#include "searchmodel.h"

// The Lingmo platform theme doesn't always hand the icon theme over (the dock
// works around it too): use the one chosen in Lingmo Settings.
static void applyIconTheme()
{
    const QString current = QIcon::themeName();
    if (!current.isEmpty() && current != QLatin1String("hicolor"))
        return;
    QSettings theme(QStringLiteral("lingmoos"), QStringLiteral("theme"));
    const bool dark = theme.value(QStringLiteral("DarkMode"), false).toBool();
    const QStringList candidates = {
        theme.value(dark ? QStringLiteral("DarkIconTheme") : QStringLiteral("IconTheme")).toString(),
        dark ? QStringLiteral("Crule-dark") : QStringLiteral("Crule"),
        QStringLiteral("breeze"),
    };
    for (const QString &name : candidates) {
        if (!name.isEmpty()
            && !QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                       QStringLiteral("icons/%1/index.theme").arg(name)).isEmpty()) {
            QIcon::setThemeName(name);
            break;
        }
    }
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
}

int main(int argc, char *argv[])
{
    // QApplication (not QGuiApplication): the print dialog is a widget
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("lingmoos"));
    app.setApplicationName(QStringLiteral("lingmo-reader"));
    app.setApplicationVersion(QStringLiteral(LINGMO_READER_VERSION));
    app.setDesktopFileName(QStringLiteral("lingmo-reader"));

    QTranslator translator;
    const QString translations = qEnvironmentVariable("LINGMO_READER_TRANSLATIONS", QStringLiteral(TRANSLATIONS_DIR));
    if (translator.load(QLocale(), QStringLiteral("lingmo-reader"), QStringLiteral("_"), translations))
        app.installTranslator(&translator);
    app.setApplicationDisplayName(QCoreApplication::translate("main", "Documents"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Lingmo document reader"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption pageOption(QStringLiteral("page"),
                                  QCoreApplication::translate("main", "Open at this page (1 is the first)"),
                                  QStringLiteral("number"));
    parser.addOption(pageOption);
    parser.addPositionalArgument(QStringLiteral("file"), QCoreApplication::translate("main", "PDF file to open"));
    parser.process(app);

    applyIconTheme();
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("application-pdf")));

    QString initialFile;
    if (!parser.positionalArguments().isEmpty()) {
        const QString arg = parser.positionalArguments().constFirst();
        const QUrl url = QUrl::fromUserInput(arg, QDir::currentPath(), QUrl::AssumeLocalFile);
        initialFile = url.isLocalFile() ? url.toLocalFile() : arg;
    }
    const int initialPage = parser.isSet(pageOption) ? parser.value(pageOption).toInt() - 1 : -1;

    PdfDocument document;
    SearchModel search(&document);
    Reader reader(&document);
    RecentFiles recentFiles;
    AnnotationStore annotations;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("pdfpage"), new PageImageProvider(&document));
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("pdf"), &document);
    ctx->setContextProperty(QStringLiteral("search"), &search);
    ctx->setContextProperty(QStringLiteral("reader"), &reader);
    ctx->setContextProperty(QStringLiteral("recentFiles"), &recentFiles);
    ctx->setContextProperty(QStringLiteral("annotations"), &annotations);
    ctx->setContextProperty(QStringLiteral("initialFile"), initialFile);
    ctx->setContextProperty(QStringLiteral("initialPage"), initialPage);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
