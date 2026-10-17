/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "recentfiles.h"

class TestRecentFiles : public QObject
{
    Q_OBJECT

private:
    QString touch(const QString &name)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write("%PDF-1.4\n");
        return path;
    }

    QString settingsFile() const { return m_dir.filePath(QStringLiteral("reader.conf")); }

    QTemporaryDir m_dir;

private Q_SLOTS:
    void init()
    {
        QFile::remove(settingsFile());
    }

    void addMovesToTop()
    {
        RecentFiles recent(settingsFile());
        const QString a = touch(QStringLiteral("a.pdf"));
        const QString b = touch(QStringLiteral("b.pdf"));
        QSignalSpy countSpy(&recent, &RecentFiles::countChanged);

        recent.add(a, 10);
        recent.add(b, 20);
        QCOMPARE(recent.count(), 2);
        QCOMPARE(recent.pathAt(0), b);
        QCOMPARE(recent.pathAt(1), a);

        recent.add(a);
        QCOMPARE(recent.count(), 2);
        QCOMPARE(recent.pathAt(0), a);
        // the page count isn't lost when re-adding without one
        QCOMPARE(recent.data(recent.index(0), RecentFiles::PageCountRole).toInt(), 10);
        QVERIFY(countSpy.count() >= 3);
    }

    void lastPagePersists()
    {
        const QString a = touch(QStringLiteral("book.pdf"));
        {
            RecentFiles recent(settingsFile());
            QCOMPARE(recent.lastPage(a), -1);
            recent.add(a, 120);
            QCOMPARE(recent.lastPage(a), 0);
            recent.setLastPage(a, 41, 120);
        }
        RecentFiles reloaded(settingsFile());
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(reloaded.lastPage(a), 41);
        QCOMPARE(reloaded.data(reloaded.index(0), RecentFiles::PageRole).toInt(), 41);
        QCOMPARE(reloaded.data(reloaded.index(0), RecentFiles::PageCountRole).toInt(), 120);
        QCOMPARE(reloaded.data(reloaded.index(0), RecentFiles::NameRole).toString(), QStringLiteral("book.pdf"));
        QVERIFY(reloaded.data(reloaded.index(0), RecentFiles::OpenedRole).toDateTime().isValid());
    }

    void urlsAndRelativePathsAreNormalized()
    {
        RecentFiles recent(settingsFile());
        const QString a = touch(QStringLiteral("c.pdf"));
        recent.add(QUrl::fromLocalFile(a).toString());
        recent.setLastPage(m_dir.path() + QStringLiteral("/./c.pdf"), 7);
        QCOMPARE(recent.count(), 1);
        QCOMPARE(recent.lastPage(a), 7);
    }

    void setLastPageAddsUnknownFile()
    {
        RecentFiles recent(settingsFile());
        const QString a = touch(QStringLiteral("d.pdf"));
        recent.setLastPage(a, 3, 9);
        QCOMPARE(recent.count(), 1);
        QCOMPARE(recent.lastPage(a), 3);
    }

    void removeAndClear()
    {
        const QString a = touch(QStringLiteral("e.pdf"));
        const QString b = touch(QStringLiteral("f.pdf"));
        {
            RecentFiles recent(settingsFile());
            recent.add(a);
            recent.add(b);
            recent.remove(a);
            QCOMPARE(recent.count(), 1);
            QCOMPARE(recent.lastPage(a), -1);
        }
        RecentFiles reloaded(settingsFile());
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(reloaded.pathAt(0), b);
        reloaded.clear();
        QCOMPARE(reloaded.count(), 0);
        RecentFiles empty(settingsFile());
        QCOMPARE(empty.count(), 0);
    }

    void missingFilesAreFlagged()
    {
        RecentFiles recent(settingsFile());
        const QString a = touch(QStringLiteral("gone.pdf"));
        recent.add(a);
        QVERIFY(recent.data(recent.index(0), RecentFiles::ExistsRole).toBool());
        QFile::remove(a);
        QVERIFY(!recent.data(recent.index(0), RecentFiles::ExistsRole).toBool());
        // still remembered: the drive may just be unplugged
        QCOMPARE(recent.count(), 1);
    }

    void listIsCapped()
    {
        RecentFiles recent(settingsFile());
        for (int i = 0; i < RecentFiles::MaxEntries + 5; ++i)
            recent.add(touch(QStringLiteral("n%1.pdf").arg(i)));
        QCOMPARE(recent.count(), RecentFiles::MaxEntries);
        QCOMPARE(recent.pathAt(0), m_dir.filePath(QStringLiteral("n%1.pdf").arg(RecentFiles::MaxEntries + 4)));
        RecentFiles reloaded(settingsFile());
        QCOMPARE(reloaded.count(), RecentFiles::MaxEntries);
    }
};

QTEST_GUILESS_MAIN(TestRecentFiles)
#include "tst_recentfiles.moc"
