/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>

// Recently opened documents, newest first, with the page each one was left on.
// Stored in the app's QSettings ([Recent] group), or in the given ini file.
class RecentFiles : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        PathRole = Qt::UserRole + 1,
        UrlRole,
        NameRole,
        FolderRole,
        PageRole,       // zero-based page the reader was on
        PageCountRole,
        OpenedRole,     // QDateTime of the last opening
        ExistsRole,
    };

    static constexpr int MaxEntries = 30;

    explicit RecentFiles(const QString &settingsFile = QString(), QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_entries.size(); }

    // Moves (or adds) the file to the top
    Q_INVOKABLE void add(const QString &path, int pageCount = -1);
    // Zero-based page the file was left on, -1 when unknown
    Q_INVOKABLE int lastPage(const QString &path) const;
    Q_INVOKABLE void setLastPage(const QString &path, int page, int pageCount = -1);
    Q_INVOKABLE void remove(const QString &path);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString pathAt(int row) const;

    static QString normalize(const QString &path);

Q_SIGNALS:
    void countChanged();

private:
    struct Entry {
        QString path;
        int page = 0;
        int pageCount = -1;
        QDateTime opened;
    };

    int indexOf(const QString &path) const;
    void load();
    void save() const;

    QString m_file;
    QList<Entry> m_entries;
};
