/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <KZip>

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QUrl>
#include <QTemporaryDir>
#include <QTest>

#include "bookconverter.h"
#include "pdfdocument.h"

// EPUB (with nav, cover and images), plain text, DOCX and ODT become PDFs
// with their table of contents on the right pages.
//
// LINGMO_READER_FIXTURES=<dir> also leaves the books there (UI checks).
class TestBookConverter : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_fixtures;

    QString out(const QString &name) const
    {
        return m_fixtures.isEmpty() ? m_dir.filePath(name) : QDir(m_fixtures).filePath(name);
    }

    static QByteArray png(const QColor &color, int w, int h, const QString &label = QString())
    {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(color);
        if (!label.isEmpty()) {
            QPainter p(&img);
            p.setPen(Qt::white);
            QFont f(QStringLiteral("DejaVu Sans"));
            f.setPixelSize(h / 9);
            f.setBold(true);
            p.setFont(f);
            p.drawText(img.rect(), Qt::AlignCenter | Qt::TextWordWrap, label);
        }
        QByteArray bytes;
        QBuffer b(&bytes);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        return bytes;
    }

    static QString chapter(const QString &title, const QStringList &paras, const QString &extra = QString())
    {
        QString body = QStringLiteral("<h1 id=\"top\">%1</h1>").arg(title);
        for (const QString &p : paras)
            body += QStringLiteral("<p>%1</p>").arg(p);
        return QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>%1</title>"
                              "<link rel=\"stylesheet\" href=\"style.css\"/><style>p{color:red}</style></head><body>%2%3</body></html>")
            .arg(title, body, extra);
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_fixtures = qEnvironmentVariable("LINGMO_READER_FIXTURES");
    }

    void epub()
    {
        const QString path = out(QStringLiteral("o-jardim.epub"));
        {
            KZip zip(path);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.setCompression(KZip::NoCompression);
            zip.writeFile(QStringLiteral("mimetype"), QByteArray("application/epub+zip"));
            zip.setCompression(KZip::DeflateCompression);
            zip.writeFile(QStringLiteral("META-INF/container.xml"),
                          "<?xml version=\"1.0\"?><container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
                          "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
            zip.writeFile(QStringLiteral("OEBPS/content.opf"),
                          "<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\">"
                          "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>O Jardim Secreto</dc:title>"
                          "<dc:creator>Clara Mendes</dc:creator><dc:language>pt-BR</dc:language></metadata>"
                          "<manifest><item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" properties=\"nav\"/>"
                          "<item id=\"c1\" href=\"text/c1.xhtml\" media-type=\"application/xhtml+xml\"/>"
                          "<item id=\"c2\" href=\"text/c2.xhtml\" media-type=\"application/xhtml+xml\"/>"
                          "<item id=\"cover\" href=\"images/cover.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
                          "<item id=\"fig\" href=\"images/fig.png\" media-type=\"image/png\"/></manifest>"
                          "<spine><itemref idref=\"c1\"/><itemref idref=\"c2\"/></spine></package>");
            zip.writeFile(QStringLiteral("OEBPS/nav.xhtml"),
                          "<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\" xmlns:epub=\"http://www.idpf.org/2007/ops\">"
                          "<body><nav epub:type=\"toc\"><ol><li><a href=\"text/c1.xhtml\">Capítulo 1 — A porta</a>"
                          "<ol><li><a href=\"text/c1.xhtml#s2\">A chave</a></li></ol></li>"
                          "<li><a href=\"text/c2.xhtml#top\">Capítulo 2 — As rosas</a></li></ol></nav></body></html>");
            QStringList c1;
            for (int i = 0; i < 9; ++i)
                c1 << QStringLiteral("Mariana encontrou uma porta escondida atrás da hera. Ela empurrou devagar e a porta rangeu. "
                                     "O ar do outro lado cheirava a terra molhada e a flores antigas (parágrafo %1).").arg(i + 1);
            const QString s2 = QStringLiteral("<h2 id=\"s2\">A chave</h2><p>A chave estava enterrada perto do muro, embrulhada num pano azul.</p>"
                                              "<p><img src=\"../images/fig.png\" alt=\"fig\"/></p>");
            zip.writeFile(QStringLiteral("OEBPS/text/c1.xhtml"), chapter(QStringLiteral("Capítulo 1 — A porta"), c1, s2).toUtf8());
            zip.writeFile(QStringLiteral("OEBPS/text/c2.xhtml"),
                          chapter(QStringLiteral("Capítulo 2 — As rosas"),
                                  {QStringLiteral("As rosas tinham crescido sem ninguém para cuidar delas."),
                                   QStringLiteral("Mariana prometeu voltar todos os dias. E voltou.")}).toUtf8());
            zip.writeFile(QStringLiteral("OEBPS/images/cover.png"), png(QColor(46, 125, 50), 600, 900, QStringLiteral("O Jardim Secreto")));
            zip.writeFile(QStringLiteral("OEBPS/images/fig.png"), png(QColor(30, 136, 229), 800, 300));
            QVERIFY(zip.close());
        }
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.title, QStringLiteral("O Jardim Secreto"));
        QCOMPARE(r.author, QStringLiteral("Clara Mendes"));
        QCOMPARE(r.language, QStringLiteral("pt-BR"));
        QVERIFY(!r.coverPath.isEmpty());
        QCOMPARE(r.outline.size(), 3);
        const QVariantMap ch1 = r.outline[0].toMap(), key = r.outline[1].toMap(), ch2 = r.outline[2].toMap();
        QCOMPARE(ch1.value(QStringLiteral("title")).toString(), QStringLiteral("Capítulo 1 — A porta"));
        QCOMPARE(key.value(QStringLiteral("depth")).toInt(), 1);
        QVERIFY(ch1.value(QStringLiteral("hasChildren")).toBool());
        // cover page, then chapter 1 on page 1; chapter 2 after a page break
        QCOMPARE(ch1.value(QStringLiteral("page")).toInt(), 1);
        QVERIFY(key.value(QStringLiteral("page")).toInt() >= ch1.value(QStringLiteral("page")).toInt());
        QVERIFY(ch2.value(QStringLiteral("page")).toInt() > key.value(QStringLiteral("page")).toInt());

        // the Reader opens it: real pages, text layer, the book's title and outline
        PdfDocument doc;
        QCOMPARE(doc.load(path), int(PdfDocument::Ready));
        QCOMPARE(doc.sourceFormat(), QStringLiteral("epub"));
        QCOMPARE(doc.title(), QStringLiteral("O Jardim Secreto"));
        QCOMPARE(doc.outline().size(), 3);
        const int p2 = ch2.value(QStringLiteral("page")).toInt();
        QVERIFY(doc.pageText(p2).contains(QStringLiteral("As rosas tinham crescido")));
        QVERIFY(!doc.pageText(p2).contains(QStringLiteral("color:red")));
        // second open comes from the cache
        const BookConverter::Result again = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QCOMPARE(again.pdfPath, r.pdfPath);
        QCOMPARE(again.outline.size(), 3);
    }

    // A book packed the odd way some sites do: META-INF first, no "mimetype",
    // random names, .html with media-type text/html, single-quoted image
    // sources, a manifest whose case disagrees with the archive, many images,
    // and a file name with spaces, parentheses, commas and accents.
    void epubOddLayout()
    {
        const QString path = out(QStringLiteral("Livro Estranho (Ana Lúcia Peña) (site.sk, 1lib.sk).epub"));
        {
            KZip zip(path);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeDir(QStringLiteral("META-INF"), QString(), QString());
            zip.writeFile(QStringLiteral("zq81ab0c.html"),
                          chapter(QStringLiteral("Parte Um"), {QStringLiteral("O farol piscava sobre o mar escuro."),
                                                               QStringLiteral("Joana contava os segundos entre cada luz.")},
                                  QStringLiteral("<p><img src='k3j9x0aa.png' alt=''/></p>")).toUtf8());
            zip.writeFile(QStringLiteral("w7yy2mpd.html"),
                          chapter(QStringLiteral("Parte Dois"), {QStringLiteral("Na manhã seguinte o barco voltou ao porto.")},
                                  QStringLiteral("<p><IMG SRC=\"Images/P0002.PNG\"></p>")).toUtf8());
            for (int i = 0; i < 12; ++i)
                zip.writeFile(QStringLiteral("k3j9x0a%1.png").arg(QChar(u'a' + i)),
                              png(QColor(20 * i, 90, 160), 400 + i * 40, 300));
            zip.writeFile(QStringLiteral("images/p0002.png"), png(QColor(200, 80, 40), 2400, 3600));
            zip.writeFile(QStringLiteral("toc.ncx"),
                          "<?xml version=\"1.0\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\"><navMap>"
                          "<navPoint id=\"a\"><navLabel><text>Parte Um</text></navLabel><content src=\"zq81ab0c.html\"/></navPoint>"
                          "<navPoint id=\"b\"><navLabel><text>Parte Dois</text></navLabel><content src=\"w7yy2mpd.html#top\"/></navPoint>"
                          "</navMap></ncx>");
            zip.writeFile(QStringLiteral("content.opf"),
                          "<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\">"
                          "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>O Farol</dc:title><dc:language>pt</dc:language></metadata>"
                          "<manifest><item id=\"x1\" href=\"zq81ab0c.html\" media-type=\"text/html\"/>"
                          "<item id=\"x2\" href=\"w7yy2mpd.html\" media-type=\"text/html\"/>"
                          "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
                          "<item id=\"im\" href=\"k3j9x0aa.png\" media-type=\"image/png\"/></manifest>"
                          "<spine toc=\"ncx\"><itemref idref=\"x1\"/><itemref idref=\"x2\"/><itemref idref=\"missing\"/></spine></package>");
            zip.writeFile(QStringLiteral("META-INF/container.xml"),
                          "<?xml version=\"1.0\"?><container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
                          "<rootfiles><rootfile full-path=\"content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
            QVERIFY(zip.close());
        }
        QCOMPARE(BookConverter::formatOf(path), QStringLiteral("epub"));
        QString error;
        const BookConverter::Book book = BookConverter::readEpub(path, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(book.images.size(), 2);   // single-quoted src and the case-mismatched path
        QCOMPARE(book.toc.size(), 2);

        QList<qreal> steps;
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")),
                                                               [&](qreal f) { steps << f; });
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.title, QStringLiteral("O Farol"));
        QCOMPARE(r.outline.size(), 2);
        QVERIFY(!steps.isEmpty());
        QVERIFY(std::is_sorted(steps.cbegin(), steps.cend()));
        QVERIFY(steps.last() <= 1.0);

        // opened from a file:// URL as a drop or "Open with" (%u) hands it over
        const QString fromUrl = QUrl(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)).toLocalFile();
        QCOMPARE(fromUrl, path);
        PdfDocument doc;
        QCOMPARE(doc.load(QUrl::fromLocalFile(path).toString()), int(PdfDocument::Ready));
        QCOMPARE(doc.sourceFormat(), QStringLiteral("epub"));
        QString all;
        for (int i = 0; i < doc.pageCount(); ++i)
            all += doc.pageText(i);
        QVERIFY(all.simplified().contains(QStringLiteral("Na manhã seguinte o barco voltou ao porto.")));
    }

    // No container.xml and no spine: the first .opf, then the manifest's HTML in order.
    // Saved without an extension: recognised by its content.
    void epubNoContainerNoExtension()
    {
        const QString path = out(QStringLiteral("download"));
        {
            KZip zip(path);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("book/pkg.opf"),
                          "<?xml version=\"1.0\"?><opf:package xmlns:opf=\"http://www.idpf.org/2007/opf\"><opf:metadata/>"
                          "<opf:manifest><opf:item id=\"b\" href=\"text/b.htm\" media-type=\"application/xhtml+xml\"/>"
                          "<opf:item id=\"a\" href=\"text/a.htm\" media-type=\"application/xhtml+xml\"/></opf:manifest><opf:spine/></opf:package>");
            zip.writeFile(QStringLiteral("book/text/a.htm"), chapter(QStringLiteral("Segundo"), {QStringLiteral("Texto do segundo.")}).toUtf8());
            zip.writeFile(QStringLiteral("book/text/b.htm"), chapter(QStringLiteral("Primeiro"), {QStringLiteral("Texto do primeiro.")}).toUtf8());
            QVERIFY(zip.close());
        }
        QCOMPARE(BookConverter::formatOf(path), QStringLiteral("epub"));
        QVERIFY(BookConverter::needsConversion(path));
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        PdfDocument doc;
        QCOMPARE(doc.load(path), int(PdfDocument::Ready));
        const QString text = doc.pageText(0).simplified();
        QVERIFY2(text.indexOf(QStringLiteral("Primeiro")) >= 0, qPrintable(text));
    }

    // A broken book says why instead of opening nothing
    void epubBroken()
    {
        const QString path = out(QStringLiteral("quebrado.epub"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("PK\x03\x04 not really a zip");
        }
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QVERIFY(!r.ok());
        QVERIFY(!r.error.isEmpty());
        PdfDocument doc;
        QCOMPARE(doc.load(path), int(PdfDocument::Error));
        QVERIFY(!doc.error().isEmpty());
        QVERIFY(BookConverter::formatOf(m_dir.filePath(QStringLiteral("nothing.pdf"))).isEmpty());
    }

    // The app converts in the background: Converting, progress, then Ready
    void asyncLoad()
    {
        const QString path = out(QStringLiteral("assíncrono (teste).txt"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            for (int i = 0; i < 400; ++i)
                f.write(QStringLiteral("Parágrafo %1 de um texto longo para ver o progresso da conversão.\n\n").arg(i).toUtf8());
        }
        qputenv("XDG_CACHE_HOME", m_dir.filePath(QStringLiteral("xdgcache")).toUtf8());
        PdfDocument doc;
        doc.setAsyncConversion(true);
        QSignalSpy status(&doc, &PdfDocument::statusChanged);
        QSignalSpy progress(&doc, &PdfDocument::progressChanged);
        QCOMPARE(doc.load(path), int(PdfDocument::Converting));
        QVERIFY(doc.converting());
        QTRY_VERIFY_WITH_TIMEOUT(doc.ready(), 20000);
        QVERIFY(progress.count() > 1);
        QVERIFY(doc.pageCount() > 1);

        // closing while converting drops the result
        const QString other = out(QStringLiteral("outro.txt"));
        {
            QFile f(other);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("Um texto curto.\n");
        }
        QCOMPARE(doc.load(other), int(PdfDocument::Converting));
        doc.close();
        QTest::qWait(500);
        QCOMPARE(doc.status(), int(PdfDocument::Null));
    }

    void plainText()
    {
        const QString path = out(QStringLiteral("licao.txt"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("CAPÍTULO UM\n\nThe weather is lovely today. We walk to the market\nand buy fresh bread.\n\n"
                "Chapter 2\n\nShe said: \"See you tomorrow!\" Then she left.\n");
        f.close();
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.outline.size(), 2);
        PdfDocument doc;
        QCOMPARE(doc.load(path), int(PdfDocument::Ready));
        QVERIFY(doc.pageText(0).simplified().contains(QStringLiteral("We walk to the market and buy fresh bread.")));
    }

    void docx()
    {
        const QString path = out(QStringLiteral("relatorio.docx"));
        {
            KZip zip(path);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("[Content_Types].xml"), "<Types/>");
            zip.writeFile(QStringLiteral("docProps/core.xml"),
                          "<cp:coreProperties xmlns:cp=\"x\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                          "<dc:title>Relatório Anual</dc:title><dc:creator>Equipe Lingmo</dc:creator></cp:coreProperties>");
            zip.writeFile(QStringLiteral("word/document.xml"),
                          "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
                          "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Introdução</w:t></w:r></w:p>"
                          "<w:p><w:r><w:t xml:space=\"preserve\">Este ano foi </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>muito bom</w:t></w:r>"
                          "<w:r><w:t xml:space=\"preserve\"> para todos.</w:t></w:r></w:p>"
                          "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:br w:type=\"page\"/><w:t>Resultados</w:t></w:r></w:p>"
                          "<w:p><w:r><w:t>Crescemos vinte por cento.</w:t></w:r></w:p></w:body></w:document>");
            QVERIFY(zip.close());
        }
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.title, QStringLiteral("Relatório Anual"));
        QCOMPARE(r.outline.size(), 2);
        QVERIFY(r.outline[1].toMap().value(QStringLiteral("page")).toInt() > r.outline[0].toMap().value(QStringLiteral("page")).toInt());
        PdfDocument doc;
        QCOMPARE(doc.load(path), int(PdfDocument::Ready));
        QVERIFY(doc.pageText(0).simplified().contains(QStringLiteral("Este ano foi muito bom para todos.")));
    }

    void odt()
    {
        const QString path = out(QStringLiteral("receita.odt"));
        {
            KZip zip(path);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("mimetype"), "application/vnd.oasis.opendocument.text");
            zip.writeFile(QStringLiteral("meta.xml"),
                          "<office:document-meta xmlns:office=\"o\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:meta=\"m\">"
                          "<office:meta><dc:title>Bolo de Fubá</dc:title><meta:initial-creator>Vó Lurdes</meta:initial-creator></office:meta></office:document-meta>");
            zip.writeFile(QStringLiteral("content.xml"),
                          "<office:document-content xmlns:office=\"o\" xmlns:text=\"t\"><office:body><office:text>"
                          "<text:h text:outline-level=\"1\">Ingredientes</text:h>"
                          "<text:p>Três ovos,<text:s/>duas xícaras de fubá<text:note><text:note-body><text:p>nota escondida</text:p></text:note-body></text:note> e leite.</text:p>"
                          "<text:h text:outline-level=\"1\">Modo de fazer</text:h><text:p>Misture tudo e asse por <text:span>quarenta</text:span> minutos.</text:p>"
                          "</office:text></office:body></office:document-content>");
            QVERIFY(zip.close());
        }
        const BookConverter::Result r = BookConverter::convert(path, m_dir.filePath(QStringLiteral("cache")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.title, QStringLiteral("Bolo de Fubá"));
        QCOMPARE(r.author, QStringLiteral("Vó Lurdes"));
        QCOMPARE(r.outline.size(), 2);
        PdfDocument doc;
        QCOMPARE(doc.load(path), int(PdfDocument::Ready));
        const QString text = doc.pageText(0).simplified();
        QVERIFY2(text.contains(QStringLiteral("Misture tudo e asse por quarenta minutos.")), qPrintable(text));
        QVERIFY(!text.contains(QStringLiteral("nota escondida")));
    }
};

QTEST_MAIN(TestBookConverter)
#include "tst_bookconverter.moc"
