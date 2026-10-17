/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QAbstractListModel>
#include <QPolygonF>
#include <QRectF>
#include <QTimer>

#include <atomic>
#include <memory>

class PdfDocument;
class PdfSnapshot;

// Text search over the whole document, page by page in a worker thread.
// Results arrive progressively; exposed to QML as "search".
class SearchModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchString READ searchString WRITE setSearchString NOTIFY searchStringChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int currentResult READ currentResult WRITE setCurrentResult NOTIFY currentResultChanged)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY currentResultChanged)
    Q_PROPERTY(QRectF currentRect READ currentRect NOTIFY currentResultChanged)
    Q_PROPERTY(QVariant currentPolygons READ currentPolygons NOTIFY currentResultChanged)
    // Bumped when results are added or cleared: pages re-query polygonsOnPage()
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Roles {
        PageRole = Qt::UserRole + 1,
        RectRole,
        ContextBeforeRole,
        ContextAfterRole,
        MatchRole,
    };

    struct Result {
        int page = 0;
        QRectF rect;
        QString before;
        QString match;
        QString after;
    };

    explicit SearchModel(PdfDocument *document, QObject *parent = nullptr);
    ~SearchModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchString() const { return m_searchString; }
    void setSearchString(const QString &text);
    int count() const { return m_results.size(); }
    bool running() const { return m_running; }
    int currentResult() const { return m_current; }
    void setCurrentResult(int index);
    int currentPage() const;
    QRectF currentRect() const;
    QVariant currentPolygons() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE QVariant polygonsOnPage(int page) const;
    Q_INVOKABLE int resultPage(int index) const;
    // First result on this page or after it (wrapping), -1 if none
    Q_INVOKABLE int firstResultFrom(int page) const;

    // Runs synchronously; used by the worker and the tests
    static QList<Result> searchPage(const PdfSnapshot &snapshot, int page, const QString &text);

Q_SIGNALS:
    void searchStringChanged();
    void countChanged();
    void runningChanged();
    void currentResultChanged();
    void revisionChanged();
    // All pages searched
    void finished();

private:
    void restart();
    void clear();
    void setRunning(bool running);
    void addResults(int token, const QList<Result> &results);

    PdfDocument *m_document;
    QString m_searchString;
    QList<Result> m_results;
    int m_current = -1;
    int m_revision = 0;
    bool m_running = false;
    QTimer m_delay;
    std::shared_ptr<std::atomic<int>> m_token;
};
