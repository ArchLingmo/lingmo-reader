/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "recentfiles.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

#include <memory>

static std::unique_ptr<QSettings> openSettings(const QString &file)
{
    if (file.isEmpty())
        return std::make_unique<QSettings>();
    return std::make_unique<QSettings>(file, QSettings::IniFormat);
}

RecentFiles::RecentFiles(const QString &settingsFile, QObject *parent)
    : QAbstractListModel(parent)
    , m_file(settingsFile)
{
    load();
}

QString RecentFiles::normalize(const QString &path)
{
    if (path.isEmpty())
        return QString();
    QString local = path;
    if (local.startsWith(QLatin1String("file:")))
        local = QUrl(local).toLocalFile();
    const QFileInfo info(local);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

int RecentFiles::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant RecentFiles::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return QVariant();
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case PathRole:
        return e.path;
    case UrlRole:
        return QUrl::fromLocalFile(e.path);
    case Qt::DisplayRole:
    case NameRole:
        return QFileInfo(e.path).fileName();
    case FolderRole: {
        QString dir = QFileInfo(e.path).absolutePath();
        const QString home = QDir::homePath();
        if (dir == home)
            return QStringLiteral("~");
        if (dir.startsWith(home + QLatin1Char('/')))
            dir = QLatin1Char('~') + dir.mid(home.size());
        return dir;
    }
    case PageRole:
        return e.page;
    case PageCountRole:
        return e.pageCount;
    case OpenedRole:
        return e.opened;
    case ExistsRole:
        return QFileInfo::exists(e.path);
    }
    return QVariant();
}

QHash<int, QByteArray> RecentFiles::roleNames() const
{
    return {
        {PathRole, "path"},
        {UrlRole, "url"},
        {NameRole, "name"},
        {FolderRole, "folder"},
        {PageRole, "page"},
        {PageCountRole, "pageCount"},
        {OpenedRole, "opened"},
        {ExistsRole, "exists"},
    };
}

int RecentFiles::indexOf(const QString &path) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).path == path)
            return i;
    }
    return -1;
}

void RecentFiles::add(const QString &path, int pageCount)
{
    const QString p = normalize(path);
    if (p.isEmpty())
        return;

    Entry entry;
    entry.path = p;
    const int i = indexOf(p);
    if (i >= 0) {
        entry = m_entries.at(i);
        if (i > 0) {
            beginMoveRows(QModelIndex(), i, i, QModelIndex(), 0);
            m_entries.move(i, 0);
            endMoveRows();
        }
    } else {
        beginInsertRows(QModelIndex(), 0, 0);
        m_entries.prepend(entry);
        endInsertRows();
    }
    m_entries[0].opened = QDateTime::currentDateTime();
    if (pageCount > 0)
        m_entries[0].pageCount = pageCount;
    Q_EMIT dataChanged(index(0), index(0));

    while (m_entries.size() > MaxEntries) {
        const int last = m_entries.size() - 1;
        beginRemoveRows(QModelIndex(), last, last);
        m_entries.removeLast();
        endRemoveRows();
    }
    save();
    Q_EMIT countChanged();
}

int RecentFiles::lastPage(const QString &path) const
{
    const int i = indexOf(normalize(path));
    return i >= 0 ? m_entries.at(i).page : -1;
}

void RecentFiles::setLastPage(const QString &path, int page, int pageCount)
{
    const QString p = normalize(path);
    if (p.isEmpty() || page < 0)
        return;
    int i = indexOf(p);
    if (i < 0) {
        add(p, pageCount);
        i = 0;
    }
    Entry &e = m_entries[i];
    if (e.page == page && (pageCount <= 0 || e.pageCount == pageCount))
        return;
    e.page = page;
    if (pageCount > 0)
        e.pageCount = pageCount;
    Q_EMIT dataChanged(index(i), index(i), {PageRole, PageCountRole});
    save();
}

void RecentFiles::remove(const QString &path)
{
    const int i = indexOf(normalize(path));
    if (i < 0)
        return;
    beginRemoveRows(QModelIndex(), i, i);
    m_entries.removeAt(i);
    endRemoveRows();
    save();
    Q_EMIT countChanged();
}

void RecentFiles::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    save();
    Q_EMIT countChanged();
}

void RecentFiles::reload()
{
    beginResetModel();
    load();
    endResetModel();
    Q_EMIT countChanged();
}

QString RecentFiles::pathAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).path : QString();
}

void RecentFiles::load()
{
    m_entries.clear();
    auto settings = openSettings(m_file);
    const int n = settings->beginReadArray(QStringLiteral("Recent"));
    for (int i = 0; i < n && m_entries.size() < MaxEntries; ++i) {
        settings->setArrayIndex(i);
        Entry e;
        e.path = settings->value(QStringLiteral("path")).toString();
        if (e.path.isEmpty() || indexOf(e.path) >= 0)
            continue;
        e.page = qMax(0, settings->value(QStringLiteral("page"), 0).toInt());
        e.pageCount = settings->value(QStringLiteral("pageCount"), -1).toInt();
        e.opened = QDateTime::fromString(settings->value(QStringLiteral("opened")).toString(), Qt::ISODate);
        m_entries.append(e);
    }
    settings->endArray();
}

void RecentFiles::save() const
{
    auto settings = openSettings(m_file);
    settings->remove(QStringLiteral("Recent"));
    settings->beginWriteArray(QStringLiteral("Recent"), m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        settings->setArrayIndex(i);
        settings->setValue(QStringLiteral("path"), e.path);
        settings->setValue(QStringLiteral("page"), e.page);
        settings->setValue(QStringLiteral("pageCount"), e.pageCount);
        settings->setValue(QStringLiteral("opened"), e.opened.toString(Qt::ISODate));
    }
    settings->endArray();
    settings->sync();
}
