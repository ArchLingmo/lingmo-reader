/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointF>
#include <QPolygonF>

// Highlights and text notes of one document. The PDF is never modified: they
// live in a JSON sidecar, <directory>/<sha1 of the document path>.json
// (by default ~/.local/share/lingmo-reader/annotations).
class AnnotationStore : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString documentPath READ documentPath WRITE setDocumentPath NOTIFY documentPathChanged)
    Q_PROPERTY(QString sidecarPath READ sidecarPath NOTIFY documentPathChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Bumped on every change: page overlays re-query annotationsOnPage()
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        PageRole,
        KindRole,      // "highlight" or "note"
        QuoteRole,     // highlighted text
        NoteRole,      // the user's text
        ColorRole,     // colour name: orange, green, blue, pink, purple
        PolygonsRole,  // QList<QPolygonF> in page points (highlights)
        PositionRole,  // QPointF in page points (notes)
        CreatedRole,
        ModifiedRole,
    };

    explicit AnnotationStore(const QString &directory = QString(), QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString documentPath() const { return m_documentPath; }
    void setDocumentPath(const QString &path);
    QString sidecarPath() const;
    int count() const { return m_items.size(); }
    int revision() const { return m_revision; }
    QString directory() const { return m_directory; }

    // polygons: PdfSelection.geometry, a list of point lists or of QPolygonF
    Q_INVOKABLE QString addHighlight(int page, const QVariant &polygons, const QString &quote,
                                     const QString &color = QString(), const QString &note = QString());
    Q_INVOKABLE QString addNote(int page, const QPointF &position, const QString &note);
    Q_INVOKABLE bool setNote(const QString &id, const QString &note);
    Q_INVOKABLE bool setColor(const QString &id, const QString &color);
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QVariantList annotationsOnPage(int page) const;
    Q_INVOKABLE QVariantMap get(const QString &id) const;
    Q_INVOKABLE int indexOf(const QString &id) const;

    static QString defaultDirectory();
    static QString sidecarFor(const QString &directory, const QString &documentPath);
    static QList<QPolygonF> polygonsFromVariant(const QVariant &value);

Q_SIGNALS:
    void documentPathChanged();
    void countChanged();
    void revisionChanged();
    // Writing the sidecar failed
    void saveFailed(const QString &message);

private:
    struct Item {
        QString id;
        int page = 0;
        QString kind;
        QString quote;
        QString note;
        QString color;
        QList<QPolygonF> polygons;
        QPointF position;
        QDateTime created;
        QDateTime modified;
        qreal top() const;
    };

    QVariantMap toMap(const Item &item) const;
    int insertSorted(const Item &item);
    void load();
    bool save();
    void changed();

    QString m_directory;
    QString m_documentPath;
    QList<Item> m_items;
    int m_revision = 0;
};
