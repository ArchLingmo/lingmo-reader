/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QDir>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>

#include "pdfdocument.h"
#include "readingtext.h"

// What "Ouvir" reads from a book-like PDF: running header, page numbers and a
// footnote are skipped; hyphenation is joined; a sentence crossing the page
// break stays one sentence; abbreviations don't end sentences.
//
// LINGMO_READER_FIXTURES=<dir> also leaves the PDF there (UI checks).
class TestReadingText : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_pdf;

    static void body(QPainter &p, qreal &y, const QStringList &lines, const QFont &font, qreal indentFirst = 0)
    {
        p.setFont(font);
        for (int i = 0; i < lines.size(); ++i) {
            p.drawText(QPointF(60 + (i == 0 ? indentFirst : 0), y), lines[i]);
            y += 17;
        }
    }

private Q_SLOTS:
    void initTestCase()
    {
        const QString fixtures = qEnvironmentVariable("LINGMO_READER_FIXTURES");
        m_pdf = fixtures.isEmpty() ? m_dir.filePath(QStringLiteral("book.pdf")) : QDir(fixtures).filePath(QStringLiteral("livro.pdf"));
        QPdfWriter writer(m_pdf);
        writer.setPageSize(QPageSize(QSizeF(420, 595), QPageSize::Point));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setResolution(72);
        writer.setTitle(QStringLiteral("O Farol da Ilha"));
        writer.setAuthor(QStringLiteral("Ana Lima"));
        QPainter p(&writer);
        QFont text(QStringLiteral("DejaVu Serif"));
        text.setPointSizeF(11);
        QFont small(text);
        small.setPointSizeF(7.5);
        QFont header(text);
        header.setPointSizeF(8.5);
        header.setItalic(true);
        QFont title(text);
        title.setPointSizeF(17);
        title.setBold(true);

        auto furniture = [&](int page) {
            p.setFont(header);
            p.drawText(QPointF(60, 34), QStringLiteral("O Farol da Ilha · Capítulo 1"));
            p.setFont(header);
            p.drawText(QPointF(205, 570), QString::number(page));
        };

        // ---- page 1
        furniture(1);
        qreal y = 90;
        body(p, y, {QStringLiteral("Capítulo 1")}, title);
        y += 14;
        body(p, y, {QStringLiteral("O Dr. Silva chegou à ilha numa manhã de vento forte."),
                    QStringLiteral("Ele trazia uma mala pequena e muita paci-"),
                    QStringLiteral("ência para esperar o barco. O farol, construído"),
                    QStringLiteral("em 1890, ainda funcionava todas as noites.")},
             text, 14);
        y += 10;
        // a paragraph with a footnote marker after "faroleiro"
        p.setFont(text);
        p.drawText(QPointF(74, y), QStringLiteral("Na vila, todos conheciam o faroleiro"));
        const qreal markerX = 74 + QFontMetricsF(text).horizontalAdvance(QStringLiteral("Na vila, todos conheciam o faroleiro"));
        p.setFont(small);
        p.drawText(QPointF(markerX + 0.5, y - 5), QStringLiteral("1"));
        p.setFont(text);
        p.drawText(QPointF(markerX + 6, y), QStringLiteral(" e a sua"));
        y += 17;
        body(p, y, {QStringLiteral("cadela, que corria pela praia quando a maré"),
                    QStringLiteral("baixava e as gaivotas")}, text);
        // footnote at the bottom
        p.setFont(small);
        p.drawText(QPointF(60, 540), QStringLiteral("1 O faroleiro cuidava da lâmpada desde 1950."));
        p.drawText(QPointF(60, 550), QStringLiteral("Morreu em 2001, aos noventa anos."));

        // ---- page 2: the sentence continues
        writer.newPage();
        furniture(2);
        y = 70;
        body(p, y, {QStringLiteral("voavam baixo sobre as pedras. Era um lugar"),
                    QStringLiteral("calmo, onde nada parecia mudar.")}, text);
        y += 10;
        body(p, y, {QStringLiteral("Mas naquela noite, o farol não acendeu.")}, text, 14);

        // ---- pages 3 and 4: more furniture so it is recognised as repeated
        for (int page = 3; page <= 4; ++page) {
            writer.newPage();
            furniture(page);
            y = 70;
            body(p, y, {QStringLiteral("Página com texto comum número %1, só para o teste.").arg(page)}, text, 14);
        }
        p.end();
    }

    void sentences()
    {
        PdfDocument doc;
        QCOMPARE(doc.load(m_pdf), int(PdfDocument::Ready));
        const auto snap = doc.snapshot();
        QHash<int, QList<ReadingText::Line>> sample;
        QHash<int, QSizeF> sizes;
        for (int p = 0; p < doc.pageCount(); ++p) {
            sample.insert(p, ReadingText::lines(snap->words(p)));
            sizes.insert(p, snap->pageSizes.at(p));
        }
        const ReadingText::Furniture furniture = ReadingText::detectFurniture(sample, sizes);
        QVERIFY2(furniture.repeated.contains(ReadingText::furnitureKey(QStringLiteral("O Farol da Ilha · Capítulo 1"))),
                 qPrintable(QStringList(furniture.repeated.values()).join(QStringLiteral(" | "))));

        int paragraph = 0;
        ReadingText::Sentence carry;
        QList<ReadingText::Sentence> all;
        for (int p = 0; p < doc.pageCount(); ++p)
            all << ReadingText::pageSentences(p, snap->words(p), snap->pageSizes.at(p), furniture, &paragraph, &carry);
        QStringList texts;
        for (const auto &s : all)
            texts << s.text;
        const QString joined = texts.join(QStringLiteral(" || "));

        // headers, page numbers and the footnote are not read
        QVERIFY2(!joined.contains(QStringLiteral("O Farol da Ilha ·")), qPrintable(joined));
        QVERIFY2(!joined.contains(QStringLiteral("desde 1950")), qPrintable(joined));
        QVERIFY2(!joined.contains(QStringLiteral("noventa anos")), qPrintable(joined));
        // the footnote marker is gone from the sentence
        QVERIFY2(joined.contains(QStringLiteral("faroleiro e a sua cadela")), qPrintable(joined));
        // the heading is its own sentence
        QCOMPARE(texts.first(), QStringLiteral("Capítulo 1"));
        QVERIFY(all.first().heading);
        // abbreviation kept, hyphenation joined
        QVERIFY2(texts.contains(QStringLiteral("O Dr. Silva chegou à ilha numa manhã de vento forte.")), qPrintable(joined));
        QVERIFY2(joined.contains(QStringLiteral("muita paciência para esperar o barco.")), qPrintable(joined));
        // a sentence across the page break is one sentence, with words on both pages
        const auto it = std::find_if(all.begin(), all.end(), [](const auto &s) { return s.text.contains(QStringLiteral("gaivotas voavam")); });
        QVERIFY2(it != all.end(), qPrintable(joined));
        QSet<int> pages;
        for (const auto &w : it->words)
            pages.insert(w.page);
        QCOMPARE(pages.size(), 2);
        QVERIFY(it->text.endsWith(QStringLiteral("pedras.")));
        // paragraphs
        const auto night = std::find_if(all.begin(), all.end(), [](const auto &s) { return s.text.startsWith(QStringLiteral("Mas naquela")); });
        QVERIFY(night != all.end());
        QVERIFY(night->paragraphEnd);
        QVERIFY(night->paragraph > it->paragraph);
        // page numbers on later pages are skipped too
        QVERIFY(!texts.contains(QStringLiteral("3")) && !texts.contains(QStringLiteral("4")));

        // word geometry: "paciência" has two rectangles (two lines)
        const auto pac = std::find_if(all.begin(), all.end(), [](const auto &s) { return s.text.contains(QStringLiteral("paciência")); });
        const int at = int(pac->text.indexOf(QStringLiteral("paciência")));
        const QList<int> ws = ReadingText::wordsInRange(*pac, at, at + 9);
        QCOMPARE(ws.size(), 1);
        QCOMPARE(pac->words[ws.first()].rects.size(), 2);
        // line rects merge words of a line
        const auto rects = ReadingText::lineRects(*pac);
        QVERIFY(rects.value(0).size() >= 2 && rects.value(0).size() <= 4);
    }

    void splitter()
    {
        const QString t = QStringLiteral("Mr. Smith paid $3.50 for it. Really? Yes! \"Of course.\" The end");
        const auto spans = ReadingText::splitSentences(t, QStringLiteral("en"));
        QStringList out;
        for (const auto &s : spans)
            out << t.mid(s.first, s.second - s.first);
        QCOMPARE(out, QStringList({QStringLiteral("Mr. Smith paid $3.50 for it."), QStringLiteral("Really?"), QStringLiteral("Yes!"),
                                   QStringLiteral("\"Of course.\""), QStringLiteral("The end")}));
        // long sentences are cut at punctuation
        QString longText;
        for (int i = 0; i < 30; ++i)
            longText += QStringLiteral("palavra número %1, ").arg(i);
        longText += QStringLiteral("fim.");
        for (const auto &s : ReadingText::splitSentences(longText))
            QVERIFY(s.second - s.first <= 260);
    }
};

QTEST_MAIN(TestReadingText)
#include "tst_readingtext.moc"
