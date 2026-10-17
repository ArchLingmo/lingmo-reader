/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>

class PdfDocument;

// Desktop glue for the QML side, exposed as "reader": files, "Open with",
// printing, the clipboard, external links and Lingmo Assistant.
class Reader : public QObject
{
    Q_OBJECT
    // Lingmo Assistant is installed (D-Bus activatable, running, or in PATH)
    Q_PROPERTY(bool assistantAvailable READ assistantAvailable NOTIFY assistantAvailableChanged)

public:
    explicit Reader(PdfDocument *document, QObject *parent = nullptr);

    bool assistantAvailable() const { return m_assistantAvailable; }

    // Local path for a file:// url (or a path), empty for anything else
    Q_INVOKABLE QString toLocalPath(const QVariant &urlOrPath) const;
    Q_INVOKABLE QUrl toUrl(const QString &path) const;
    Q_INVOKABLE bool exists(const QString &path) const;
    // A file the reader opens (PDF)
    Q_INVOKABLE bool isSupported(const QString &path) const;
    Q_INVOKABLE QString fileName(const QString &path) const;
    Q_INVOKABLE QString folder(const QString &url) const;

    // Other applications that open PDFs: [{ id, name, icon }]
    Q_INVOKABLE QVariantList openWithApplications(const QString &path) const;
    Q_INVOKABLE bool openWith(const QString &appId, const QString &path) const;
    // Shows the system print dialog and prints; currentPage is zero-based
    Q_INVOKABLE bool print(int currentPage, const QString &title);

    Q_INVOKABLE void openExternal(const QString &url) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE bool askAssistant(const QString &text) const;
    Q_INVOKABLE void refreshAssistant();

Q_SIGNALS:
    void assistantAvailableChanged();

private:
    PdfDocument *m_document;
    bool m_assistantAvailable = false;
};
