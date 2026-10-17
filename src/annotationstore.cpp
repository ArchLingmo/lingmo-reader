/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "annotationstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

static const QStringList Colors = {
    QStringLiteral("orange"), QStringLiteral("green"), QStringLiteral("blue"),
    QStringLiteral("pink"), QStringLiteral("purple"),
};

static QString normalizedPath(const QString &path)
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

static QString validColor(const QString &color)
{
    return Colors.contains(color) ? color : Colors.first();
}

qreal AnnotationStore::Item::top() const
{
    if (kind == QLatin1String("note"))
        return position.y();
    qreal y = 1e9;
    for (const QPolygonF &p : polygons)
        y = qMin(y, p.boundingRect().top());
    return polygons.isEmpty() ? 0 : y;
}

AnnotationStore::AnnotationStore(const QString &directory, QObject *parent)
    : QAbstractListModel(parent)
    , m_directory(directory.isEmpty() ? defaultDirectory() : directory)
{
}

QString AnnotationStore::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/lingmo-reader/annotations");
}

QString AnnotationStore::sidecarFor(const QString &directory, const QString &documentPath)
{
    const QByteArray hash = QCryptographicHash::hash(normalizedPath(documentPath).toUtf8(),
                                                     QCryptographicHash::Sha1).toHex();
    return directory + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".json");
}

QString AnnotationStore::sidecarPath() const
{
    return m_documentPath.isEmpty() ? QString() : sidecarFor(m_directory, m_documentPath);
}

void AnnotationStore::setDocumentPath(const QString &path)
{
    const QString p = normalizedPath(path);
    if (p == m_documentPath)
        return;
    beginResetModel();
    m_documentPath = p;
    load();
    endResetModel();
    Q_EMIT documentPathChanged();
    Q_EMIT countChanged();
    ++m_revision;
    Q_EMIT revisionChanged();
}

int AnnotationStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant AnnotationStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();
    const Item &it = m_items.at(index.row());
    switch (role) {
    case IdRole:
        return it.id;
    case PageRole:
        return it.page;
    case KindRole:
        return it.kind;
    case QuoteRole:
        return it.quote;
    case Qt::DisplayRole:
    case NoteRole:
        return it.note;
    case ColorRole:
        return it.color;
    case PolygonsRole:
        return QVariant::fromValue(it.polygons);
    case PositionRole:
        return it.position;
    case CreatedRole:
        return it.created;
    case ModifiedRole:
        return it.modified;
    }
    return QVariant();
}

QHash<int, QByteArray> AnnotationStore::roleNames() const
{
    return {
        {IdRole, "annotationId"},
        {PageRole, "page"},
        {KindRole, "kind"},
        {QuoteRole, "quote"},
        {NoteRole, "note"},
        {ColorRole, "colorName"},
        {PolygonsRole, "polygons"},
        {PositionRole, "position"},
        {CreatedRole, "created"},
        {ModifiedRole, "modified"},
    };
}

QList<QPolygonF> AnnotationStore::polygonsFromVariant(const QVariant &value)
{
    QList<QPolygonF> result;
    if (value.metaType() == QMetaType::fromType<QList<QPolygonF>>())
        return value.value<QList<QPolygonF>>();

    auto toPoint = [](const QVariant &v, bool *ok) -> QPointF {
        *ok = true;
        if (v.metaType() == QMetaType::fromType<QPointF>() || v.metaType() == QMetaType::fromType<QPoint>())
            return v.toPointF();
        if (v.metaType() == QMetaType::fromType<QVariantMap>()) {
            const QVariantMap m = v.toMap();
            if (m.contains(QStringLiteral("x")) && m.contains(QStringLiteral("y")))
                return QPointF(m.value(QStringLiteral("x")).toReal(), m.value(QStringLiteral("y")).toReal());
        }
        const QVariantList l = v.toList();
        if (l.size() == 2)
            return QPointF(l.at(0).toReal(), l.at(1).toReal());
        *ok = false;
        return QPointF();
    };

    const QVariantList list = value.toList();
    for (const QVariant &polyVariant : list) {
        if (polyVariant.metaType() == QMetaType::fromType<QPolygonF>()) {
            result.append(polyVariant.value<QPolygonF>());
            continue;
        }
        QPolygonF poly;
        const QVariantList points = polyVariant.toList();
        for (const QVariant &pv : points) {
            bool ok = false;
            const QPointF pt = toPoint(pv, &ok);
            if (ok)
                poly.append(pt);
        }
        if (poly.size() >= 2)
            result.append(poly);
    }
    return result;
}

int AnnotationStore::insertSorted(const Item &item)
{
    int row = 0;
    const qreal top = item.top();
    while (row < m_items.size()) {
        const Item &other = m_items.at(row);
        if (other.page > item.page || (other.page == item.page && other.top() > top))
            break;
        ++row;
    }
    beginInsertRows(QModelIndex(), row, row);
    m_items.insert(row, item);
    endInsertRows();
    return row;
}

QString AnnotationStore::addHighlight(int page, const QVariant &polygons, const QString &quote,
                                      const QString &color, const QString &note)
{
    if (m_documentPath.isEmpty() || page < 0)
        return QString();
    Item it;
    it.polygons = polygonsFromVariant(polygons);
    if (it.polygons.isEmpty())
        return QString();
    it.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    it.page = page;
    it.kind = QStringLiteral("highlight");
    it.quote = quote.simplified();
    it.note = note;
    it.color = validColor(color);
    it.created = it.modified = QDateTime::currentDateTime();
    insertSorted(it);
    changed();
    return it.id;
}

QString AnnotationStore::addNote(int page, const QPointF &position, const QString &note)
{
    if (m_documentPath.isEmpty() || page < 0 || note.trimmed().isEmpty())
        return QString();
    Item it;
    it.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    it.page = page;
    it.kind = QStringLiteral("note");
    it.note = note;
    it.position = position;
    it.color = Colors.first();
    it.created = it.modified = QDateTime::currentDateTime();
    insertSorted(it);
    changed();
    return it.id;
}

int AnnotationStore::indexOf(const QString &id) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

bool AnnotationStore::setNote(const QString &id, const QString &note)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    // An empty text note has nothing left to show
    if (m_items.at(i).kind == QLatin1String("note") && note.trimmed().isEmpty())
        return remove(id);
    m_items[i].note = note;
    m_items[i].modified = QDateTime::currentDateTime();
    Q_EMIT dataChanged(index(i), index(i), {NoteRole, ModifiedRole});
    changed();
    return true;
}

bool AnnotationStore::setColor(const QString &id, const QString &color)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    m_items[i].color = validColor(color);
    m_items[i].modified = QDateTime::currentDateTime();
    Q_EMIT dataChanged(index(i), index(i), {ColorRole, ModifiedRole});
    changed();
    return true;
}

bool AnnotationStore::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    beginRemoveRows(QModelIndex(), i, i);
    m_items.removeAt(i);
    endRemoveRows();
    changed();
    return true;
}

QVariantMap AnnotationStore::toMap(const Item &it) const
{
    return {
        {QStringLiteral("annotationId"), it.id},
        {QStringLiteral("page"), it.page},
        {QStringLiteral("kind"), it.kind},
        {QStringLiteral("quote"), it.quote},
        {QStringLiteral("note"), it.note},
        {QStringLiteral("colorName"), it.color},
        {QStringLiteral("polygons"), QVariant::fromValue(it.polygons)},
        {QStringLiteral("position"), it.position},
        {QStringLiteral("top"), it.top()},
    };
}

QVariantList AnnotationStore::annotationsOnPage(int page) const
{
    QVariantList list;
    for (const Item &it : m_items) {
        if (it.page == page)
            list.append(toMap(it));
    }
    return list;
}

QVariantMap AnnotationStore::get(const QString &id) const
{
    const int i = indexOf(id);
    return i >= 0 ? toMap(m_items.at(i)) : QVariantMap();
}

void AnnotationStore::changed()
{
    save();
    ++m_revision;
    Q_EMIT countChanged();
    Q_EMIT revisionChanged();
}

void AnnotationStore::load()
{
    m_items.clear();
    if (m_documentPath.isEmpty())
        return;
    QFile file(sidecarPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray array = root.value(QStringLiteral("annotations")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        Item it;
        it.id = o.value(QStringLiteral("id")).toString();
        it.page = o.value(QStringLiteral("page")).toInt(-1);
        it.kind = o.value(QStringLiteral("kind")).toString();
        if (it.id.isEmpty() || it.page < 0
            || (it.kind != QLatin1String("highlight") && it.kind != QLatin1String("note")))
            continue;
        it.quote = o.value(QStringLiteral("quote")).toString();
        it.note = o.value(QStringLiteral("note")).toString();
        it.color = validColor(o.value(QStringLiteral("color")).toString());
        for (const QJsonValue &polyValue : o.value(QStringLiteral("polygons")).toArray()) {
            QPolygonF poly;
            for (const QJsonValue &pt : polyValue.toArray()) {
                const QJsonArray xy = pt.toArray();
                if (xy.size() == 2)
                    poly.append(QPointF(xy.at(0).toDouble(), xy.at(1).toDouble()));
            }
            if (poly.size() >= 2)
                it.polygons.append(poly);
        }
        const QJsonArray pos = o.value(QStringLiteral("position")).toArray();
        if (pos.size() == 2)
            it.position = QPointF(pos.at(0).toDouble(), pos.at(1).toDouble());
        it.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODate);
        it.modified = QDateTime::fromString(o.value(QStringLiteral("modified")).toString(), Qt::ISODate);
        if (it.kind == QLatin1String("highlight") && it.polygons.isEmpty())
            continue;
        // keep the page/top order without emitting model signals (inside a reset)
        int row = 0;
        while (row < m_items.size()
               && (m_items.at(row).page < it.page
                   || (m_items.at(row).page == it.page && m_items.at(row).top() <= it.top())))
            ++row;
        m_items.insert(row, it);
    }
}

bool AnnotationStore::save()
{
    if (m_documentPath.isEmpty())
        return false;
    const QString path = sidecarPath();
    if (m_items.isEmpty()) {
        QFile::remove(path);
        return true;
    }
    QDir().mkpath(m_directory);

    QJsonArray array;
    for (const Item &it : m_items) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), it.id);
        o.insert(QStringLiteral("page"), it.page);
        o.insert(QStringLiteral("kind"), it.kind);
        if (!it.quote.isEmpty())
            o.insert(QStringLiteral("quote"), it.quote);
        o.insert(QStringLiteral("note"), it.note);
        o.insert(QStringLiteral("color"), it.color);
        if (it.kind == QLatin1String("highlight")) {
            QJsonArray polys;
            for (const QPolygonF &poly : it.polygons) {
                QJsonArray pts;
                for (const QPointF &pt : poly)
                    pts.append(QJsonArray{qRound(pt.x() * 100) / 100.0, qRound(pt.y() * 100) / 100.0});
                polys.append(pts);
            }
            o.insert(QStringLiteral("polygons"), polys);
        } else {
            o.insert(QStringLiteral("position"), QJsonArray{it.position.x(), it.position.y()});
        }
        o.insert(QStringLiteral("created"), it.created.toString(Qt::ISODate));
        o.insert(QStringLiteral("modified"), it.modified.toString(Qt::ISODate));
        array.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("document"), m_documentPath);
    root.insert(QStringLiteral("annotations"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        Q_EMIT saveFailed(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        Q_EMIT saveFailed(file.errorString());
        return false;
    }
    return true;
}
