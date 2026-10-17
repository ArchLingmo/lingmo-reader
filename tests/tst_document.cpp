/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>

#include "pdfdocument.h"
#include "searchmodel.h"

// The Poppler backend against a PDF written by QPdfWriter
class TestDocument : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_pdf;

private Q_SLOTS:
    void initTestCase()
    {
        m_pdf = m_dir.filePath(QStringLiteral("sample.pdf"));
        QPdfWriter writer(m_pdf);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        writer.setTitle(QStringLiteral("Sample Title"));
        QPainter p(&writer);
        QFont font(QStringLiteral("DejaVu Sans"));
        font.setPointSize(14);
        p.setFont(font);
        p.drawText(QPointF(72, 100), QStringLiteral("Hello Lingmo world"));
        p.drawText(QPointF(72, 140), QStringLiteral("second line here"));

        QTextDocument html;
        html.setDefaultFont(font);
        html.setHtml(QStringLiteral("<a href=\"https://lingmo.org/\">website link</a>"));
        p.translate(72, 200);
        html.drawContents(&p);
        p.resetTransform();

        writer.newPage();
        p.drawText(QPointF(72, 100), QStringLiteral("Another page about Lingmo"));
        p.end();
    }

    void loads()
    {
        PdfDocument doc;
        QSignalSpy status(&doc, &PdfDocument::statusChanged);
        QCOMPARE(doc.load(m_pdf), int(PdfDocument::Ready));
        QVERIFY(doc.ready());
        QCOMPARE(doc.pageCount(), 2);
        QCOMPARE(doc.title(), QStringLiteral("Sample Title"));
        QVERIFY(qAbs(doc.pageSize(0).width() - 595) < 2);
        QCOMPARE(doc.pageLabel(1), QStringLiteral("2"));
        QVERIFY(status.count() >= 1);
        QVERIFY(doc.imageSource(0, 300).startsWith(QStringLiteral("image://pdfpage/")));

        const QImage img = doc.snapshot()->render(0, 300);
        QCOMPARE(img.width(), 300);
        QVERIFY(!img.isNull());
    }

    void badFiles()
    {
        PdfDocument doc;
        QCOMPARE(doc.load(m_dir.filePath(QStringLiteral("missing.pdf"))), int(PdfDocument::Error));
        QVERIFY(!doc.error().isEmpty());
        const QString junk = m_dir.filePath(QStringLiteral("junk.pdf"));
        QFile f(junk);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a pdf");
        f.close();
        QCOMPARE(doc.load(junk), int(PdfDocument::Error));
        QCOMPARE(doc.pageCount(), 0);
        doc.close();
        QCOMPARE(doc.status(), int(PdfDocument::Null));
    }

    void textAndSelection()
    {
        PdfDocument doc;
        doc.load(m_pdf);
        QVERIFY(doc.pageText(0).contains(QStringLiteral("Hello Lingmo world")));

        // drag from the start of "Hello" to the middle of "Lingmo"
        const QList<PdfWord> words = doc.snapshot()->words(0);
        QVERIFY(words.size() >= 6);
        QCOMPARE(words.at(0).text, QStringLiteral("Hello"));
        const QVariantMap sel = doc.selection(0, words.at(0).rect.topLeft() + QPointF(1, 1),
                                              words.at(1).rect.center());
        QCOMPARE(sel.value("text").toString(), QStringLiteral("Hello Lingmo"));
        QCOMPARE(sel.value("polygons").value<QList<QPolygonF>>().size(), 1);

        // across two lines: newline in the text, one rectangle per line
        const QVariantMap two = doc.selection(0, words.at(1).rect.center(), words.at(4).rect.center());
        QCOMPARE(two.value("text").toString(), QStringLiteral("Lingmo world\nsecond line"));
        QCOMPARE(two.value("polygons").value<QList<QPolygonF>>().size(), 2);

        // a click is not a selection; a double click selects one word
        QVERIFY(doc.selection(0, QPointF(80, 95), QPointF(80, 95)).value("text").toString().isEmpty());
        QCOMPARE(doc.selectWord(0, words.at(2).rect.center()).value("text").toString(), QStringLiteral("world"));
    }

    void links()
    {
        PdfDocument doc;
        doc.load(m_pdf);
        const QVariantList list = doc.links(0);
        if (list.isEmpty())
            QSKIP("QPdfWriter wrote no link annotation");
        const QVariantMap link = list.first().toMap();
        QCOMPARE(link.value("url").toString(), QStringLiteral("https://lingmo.org/"));
        QCOMPARE(link.value("page").toInt(), -1);
        QVERIFY(link.value("width").toDouble() > 10);
        QVERIFY(link.value("y").toDouble() > 150);
    }

    void searchAcrossPages()
    {
        PdfDocument doc;
        doc.load(m_pdf);
        const auto snap = doc.snapshot();
        const auto first = SearchModel::searchPage(*snap, 0, QStringLiteral("lingmo"));
        QCOMPARE(first.size(), 1);
        QCOMPARE(first.first().before.trimmed(), QStringLiteral("Hello"));
        QCOMPARE(first.first().match, QStringLiteral("Lingmo"));
        QVERIFY(first.first().after.startsWith(QStringLiteral(" world")));

        SearchModel model(&doc);
        QSignalSpy finished(&model, &SearchModel::finished);
        model.setSearchString(QStringLiteral("LINGMO"));
        QVERIFY(finished.wait(5000));
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.resultPage(0), 0);
        QCOMPARE(model.resultPage(1), 1);
        QCOMPARE(model.firstResultFrom(1), 1);
        model.setCurrentResult(1);
        QCOMPARE(model.currentPage(), 1);
        QVERIFY(!model.currentRect().isEmpty());
        QCOMPARE(model.polygonsOnPage(0).value<QList<QPolygonF>>().size(), 1);

        model.setSearchString(QStringLiteral("nothing like this"));
        QVERIFY(finished.wait(5000));
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.currentResult(), -1);
    }

    void password()
    {
        const QString qpdf = QStandardPaths::findExecutable(QStringLiteral("qpdf"));
        if (qpdf.isEmpty())
            QSKIP("qpdf not installed");
        const QString locked = m_dir.filePath(QStringLiteral("locked.pdf"));
        QCOMPARE(QProcess::execute(qpdf, {QStringLiteral("--encrypt"), QStringLiteral("secret"), QStringLiteral("secret"),
                                          QStringLiteral("256"), QStringLiteral("--"), m_pdf, locked}), 0);
        PdfDocument doc;
        QCOMPARE(doc.load(locked), int(PdfDocument::Locked));
        QVERIFY(!doc.unlock(QStringLiteral("wrong")));
        QVERIFY(doc.locked());
        QVERIFY(doc.unlock(QStringLiteral("secret")));
        QVERIFY(doc.ready());
        QCOMPARE(doc.pageCount(), 2);
        QVERIFY(doc.pageText(1).contains(QStringLiteral("Another page")));
    }
};

QTEST_MAIN(TestDocument)
#include "tst_document.moc"
