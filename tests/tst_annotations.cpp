/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "annotationstore.h"

class TestAnnotations : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_pdf;
    QByteArray m_pdfBytes = "%PDF-1.4\n% not touched\n";

    QString store() const { return m_dir.filePath(QStringLiteral("annotations")); }

    static QVariantList jsPolygons()
    {
        // what QML hands over: lists of {x, y}
        return {
            QVariantList{QVariantMap{{"x", 10}, {"y", 100}}, QVariantMap{{"x", 200}, {"y", 100}},
                         QVariantMap{{"x", 200}, {"y", 112}}, QVariantMap{{"x", 10}, {"y", 112}}},
        };
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_pdf = m_dir.filePath(QStringLiteral("paper.pdf"));
        QFile f(m_pdf);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(m_pdfBytes);
    }

    void init()
    {
        QFile::remove(AnnotationStore::sidecarFor(store(), m_pdf));
    }

    void sidecarPathIsStable()
    {
        const QString a = AnnotationStore::sidecarFor(store(), m_pdf);
        const QString b = AnnotationStore::sidecarFor(store(), m_dir.path() + QStringLiteral("/./paper.pdf"));
        QCOMPARE(a, b);
        QVERIFY(a.startsWith(store()));
        QVERIFY(a.endsWith(QStringLiteral(".json")));
        QVERIFY(a != AnnotationStore::sidecarFor(store(), m_dir.filePath(QStringLiteral("other.pdf"))));
    }

    void polygonConversion()
    {
        QCOMPARE(AnnotationStore::polygonsFromVariant(jsPolygons()).size(), 1);
        QCOMPARE(AnnotationStore::polygonsFromVariant(jsPolygons()).first().size(), 4);
        const QList<QPolygonF> native{QPolygonF(QRectF(1, 2, 3, 4))};
        QCOMPARE(AnnotationStore::polygonsFromVariant(QVariant::fromValue(native)), native);
        const QVariantList pairs{QVariantList{QVariantList{1, 2}, QVariantList{3, 4}}};
        QCOMPARE(AnnotationStore::polygonsFromVariant(pairs).first().at(1), QPointF(3, 4));
        QVERIFY(AnnotationStore::polygonsFromVariant(QVariantList{}).isEmpty());
    }

    void addSaveReload()
    {
        QString highlight, note;
        {
            AnnotationStore s(store());
            QVERIFY(s.addNote(0, QPointF(5, 5), QStringLiteral("too early")).isEmpty()); // no document yet
            s.setDocumentPath(m_pdf);
            QSignalSpy revision(&s, &AnnotationStore::revisionChanged);
            highlight = s.addHighlight(2, jsPolygons(), QStringLiteral("  some   quoted\ntext "), QStringLiteral("green"));
            note = s.addNote(1, QPointF(50, 60), QStringLiteral("Remember this"));
            QVERIFY(!highlight.isEmpty());
            QVERIFY(!note.isEmpty());
            QCOMPARE(s.count(), 2);
            QCOMPARE(revision.count(), 2);
            // sorted by page
            QCOMPARE(s.data(s.index(0), AnnotationStore::IdRole).toString(), note);
            QCOMPARE(s.data(s.index(1), AnnotationStore::QuoteRole).toString(), QStringLiteral("some quoted text"));
            QVERIFY(QFile::exists(s.sidecarPath()));
        }

        AnnotationStore reloaded(store());
        reloaded.setDocumentPath(m_pdf);
        QCOMPARE(reloaded.count(), 2);
        const QVariantMap h = reloaded.get(highlight);
        QCOMPARE(h.value("kind").toString(), QStringLiteral("highlight"));
        QCOMPARE(h.value("page").toInt(), 2);
        QCOMPARE(h.value("colorName").toString(), QStringLiteral("green"));
        const auto polys = h.value("polygons").value<QList<QPolygonF>>();
        QCOMPARE(polys.size(), 1);
        QCOMPARE(polys.first().boundingRect(), QRectF(10, 100, 190, 12));
        QCOMPARE(reloaded.get(note).value("position").toPointF(), QPointF(50, 60));
        QCOMPARE(reloaded.annotationsOnPage(1).size(), 1);
        QCOMPARE(reloaded.annotationsOnPage(5).size(), 0);

        // the sidecar names the document; the PDF itself is untouched
        QFile side(reloaded.sidecarPath());
        QVERIFY(side.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(side.readAll()).object();
        QCOMPARE(root.value("version").toInt(), 1);
        QCOMPARE(root.value("document").toString(), m_pdf);
        QCOMPARE(root.value("annotations").toArray().size(), 2);
        QFile pdf(m_pdf);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QCOMPARE(pdf.readAll(), m_pdfBytes);
    }

    void editAndRemove()
    {
        AnnotationStore s(store());
        s.setDocumentPath(m_pdf);
        const QString h = s.addHighlight(0, jsPolygons(), QStringLiteral("quote"));
        const QString n = s.addNote(0, QPointF(1, 1), QStringLiteral("first"));
        QCOMPARE(s.get(h).value("colorName").toString(), QStringLiteral("orange")); // default
        QVERIFY(s.setNote(h, QStringLiteral("a comment")));
        QVERIFY(s.setColor(h, QStringLiteral("not-a-colour")));
        QCOMPARE(s.get(h).value("colorName").toString(), QStringLiteral("orange"));
        QVERIFY(s.setNote(n, QStringLiteral("second")));
        // emptying a text note deletes it
        QVERIFY(s.setNote(n, QStringLiteral("   ")));
        QCOMPARE(s.count(), 1);
        QVERIFY(!s.remove(QStringLiteral("nope")));

        AnnotationStore reloaded(store());
        reloaded.setDocumentPath(m_pdf);
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(reloaded.get(h).value("note").toString(), QStringLiteral("a comment"));

        // the last one gone: no sidecar left behind
        QVERIFY(reloaded.remove(h));
        QVERIFY(!QFile::exists(reloaded.sidecarPath()));
    }

    void rejectsEmptyInput()
    {
        AnnotationStore s(store());
        s.setDocumentPath(m_pdf);
        QVERIFY(s.addHighlight(0, QVariantList{}, QStringLiteral("x")).isEmpty());
        QVERIFY(s.addNote(0, QPointF(), QStringLiteral("  ")).isEmpty());
        QVERIFY(s.addNote(-1, QPointF(), QStringLiteral("x")).isEmpty());
        QCOMPARE(s.count(), 0);
    }

    void corruptSidecarIsIgnored()
    {
        QDir().mkpath(store());
        QFile f(AnnotationStore::sidecarFor(store(), m_pdf));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not json");
        f.close();
        AnnotationStore s(store());
        s.setDocumentPath(m_pdf);
        QCOMPARE(s.count(), 0);
    }

    void switchingDocuments()
    {
        AnnotationStore s(store());
        s.setDocumentPath(m_pdf);
        s.addNote(0, QPointF(1, 1), QStringLiteral("mine"));
        s.setDocumentPath(m_dir.filePath(QStringLiteral("other.pdf")));
        QCOMPARE(s.count(), 0);
        s.setDocumentPath(m_pdf);
        QCOMPARE(s.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestAnnotations)
#include "tst_annotations.moc"
