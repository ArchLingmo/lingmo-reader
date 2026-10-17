/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "searchmodel.h"
#include "pdfdocument.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

static constexpr int ContextWords = 5;

SearchModel::SearchModel(PdfDocument *document, QObject *parent)
    : QAbstractListModel(parent)
    , m_document(document)
    , m_token(std::make_shared<std::atomic<int>>(0))
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(220);
    connect(&m_delay, &QTimer::timeout, this, &SearchModel::restart);
    if (m_document)
        connect(m_document, &PdfDocument::statusChanged, this, &SearchModel::restart);
}

SearchModel::~SearchModel()
{
    ++*m_token;
}

int SearchModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_results.size();
}

QVariant SearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_results.size())
        return QVariant();
    const Result &r = m_results.at(index.row());
    switch (role) {
    case PageRole:
        return r.page;
    case RectRole:
        return r.rect;
    case ContextBeforeRole:
        return r.before;
    case ContextAfterRole:
        return r.after;
    case Qt::DisplayRole:
    case MatchRole:
        return r.match;
    }
    return QVariant();
}

QHash<int, QByteArray> SearchModel::roleNames() const
{
    return {
        {PageRole, "page"},
        {RectRole, "rect"},
        {ContextBeforeRole, "contextBefore"},
        {ContextAfterRole, "contextAfter"},
        {MatchRole, "match"},
    };
}

void SearchModel::setSearchString(const QString &text)
{
    if (text == m_searchString)
        return;
    m_searchString = text;
    Q_EMIT searchStringChanged();
    // Typing: wait for a pause, but clear the old results at once
    clear();
    m_delay.start();
}

void SearchModel::setCurrentResult(int index)
{
    if (index < -1 || index >= m_results.size())
        index = -1;
    if (index == m_current)
        return;
    m_current = index;
    Q_EMIT currentResultChanged();
}

int SearchModel::currentPage() const
{
    return m_current >= 0 ? m_results.at(m_current).page : -1;
}

QRectF SearchModel::currentRect() const
{
    return m_current >= 0 ? m_results.at(m_current).rect : QRectF();
}

QVariant SearchModel::currentPolygons() const
{
    QList<QPolygonF> list;
    if (m_current >= 0)
        list.append(QPolygonF(m_results.at(m_current).rect));
    return QVariant::fromValue(list);
}

QVariant SearchModel::polygonsOnPage(int page) const
{
    QList<QPolygonF> list;
    for (const Result &r : m_results) {
        if (r.page == page)
            list.append(QPolygonF(r.rect));
        else if (r.page > page)
            break;
    }
    return QVariant::fromValue(list);
}

int SearchModel::resultPage(int index) const
{
    return index >= 0 && index < m_results.size() ? m_results.at(index).page : -1;
}

int SearchModel::firstResultFrom(int page) const
{
    for (int i = 0; i < m_results.size(); ++i) {
        if (m_results.at(i).page >= page)
            return i;
    }
    return m_results.isEmpty() ? -1 : 0;
}

void SearchModel::clear()
{
    ++*m_token;
    setRunning(false);
    if (!m_results.isEmpty()) {
        beginResetModel();
        m_results.clear();
        endResetModel();
        Q_EMIT countChanged();
    }
    if (m_current != -1) {
        m_current = -1;
        Q_EMIT currentResultChanged();
    }
    ++m_revision;
    Q_EMIT revisionChanged();
}

void SearchModel::setRunning(bool running)
{
    if (running == m_running)
        return;
    m_running = running;
    Q_EMIT runningChanged();
}

void SearchModel::restart()
{
    m_delay.stop();
    clear();
    const QString text = m_searchString.trimmed();
    const std::shared_ptr<PdfSnapshot> snapshot = m_document ? m_document->snapshot() : nullptr;
    if (text.isEmpty() || !snapshot)
        return;

    const int token = ++*m_token;
    setRunning(true);
    QPointer<SearchModel> guard(this);
    std::shared_ptr<std::atomic<int>> current = m_token;
    QThreadPool::globalInstance()->start([snapshot, text, token, current, guard] {
        const int pages = snapshot->pageSizes.size();
        for (int page = 0; page < pages; ++page) {
            if (current->load() != token)
                return;
            QList<Result> found = searchPage(*snapshot, page, text);
            if (found.isEmpty())
                continue;
            QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, token, found] {
                if (guard)
                    guard->addResults(token, found);
            }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, token, current] {
            if (guard && current->load() == token) {
                guard->setRunning(false);
                Q_EMIT guard->finished();
            }
        }, Qt::QueuedConnection);
    });
}

void SearchModel::addResults(int token, const QList<Result> &results)
{
    if (token != m_token->load() || results.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_results.size(), m_results.size() + results.size() - 1);
    m_results.append(results);
    endInsertRows();
    Q_EMIT countChanged();
    ++m_revision;
    Q_EMIT revisionChanged();
}

QList<SearchModel::Result> SearchModel::searchPage(const PdfSnapshot &snapshot, int page, const QString &text)
{
    QList<Result> results;
    const QList<QRectF> rects = snapshot.search(page, text);
    if (rects.isEmpty())
        return results;
    const QList<PdfWord> words = snapshot.words(page);

    auto join = [&words](int from, int to) {
        QString s;
        for (int i = from; i <= to; ++i) {
            s += words.at(i).text;
            if (i == to)
                break;
            const QRectF &a = words.at(i).rect;
            const QRectF &b = words.at(i + 1).rect;
            const bool newLine = qMin(a.bottom(), b.bottom()) - qMax(a.top(), b.top()) <= 0.5 * qMin(a.height(), b.height());
            if (words.at(i).spaceAfter || newLine)
                s += QLatin1Char(' ');
        }
        return s;
    };

    for (const QRectF &rect : rects) {
        Result r;
        r.page = page;
        r.rect = rect;
        r.match = text;
        int first = -1, last = -1;
        for (int i = 0; i < words.size(); ++i) {
            if (words.at(i).rect.intersects(rect.adjusted(0.5, 0.5, -0.5, -0.5))) {
                if (first < 0)
                    first = i;
                last = i;
            }
        }
        if (first >= 0) {
            if (first > 0)
                r.before = join(qMax(0, first - ContextWords), first - 1) + QLatin1Char(' ');
            if (last < words.size() - 1)
                r.after = QLatin1Char(' ') + join(last + 1, qMin(int(words.size()) - 1, last + ContextWords));
            // the matched text as written in the document, minus the surrounding word parts
            const QString matched = join(first, last);
            const int at = matched.indexOf(text, 0, Qt::CaseInsensitive);
            if (at >= 0) {
                r.before += matched.left(at);
                r.match = matched.mid(at, text.size());
                r.after.prepend(matched.mid(at + text.size()));
            }
        }
        results.append(r);
    }
    return results;
}
