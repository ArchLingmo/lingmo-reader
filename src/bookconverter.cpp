/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bookconverter.h"

#include <KZip>

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPalette>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QPageSize>
#include <QMarginsF>
#include <QTextFrame>
#include <QUrl>
#include <QXmlStreamReader>

namespace BookConverter {

namespace {

// Bump when the typesetting changes, so cached conversions are redone
constexpr int CacheVersion = 3;
constexpr qreal MarginPt = 40;

QString esc(const QString &s)
{
    return s.toHtmlEscaped();
}

void collectFiles(const KArchiveDirectory *dir, const QString &prefix, QStringList *out)
{
    for (const QString &name : dir->entries()) {
        const KArchiveEntry *e = dir->entry(name);
        if (!e)
            continue;
        if (e->isDirectory())
            collectFiles(static_cast<const KArchiveDirectory *>(e), prefix + name + QLatin1Char('/'), out);
        else
            out->append(prefix + name);
    }
}

// Every file in the archive, "dir/sub/name"
QStringList zipFiles(const KArchiveDirectory *root)
{
    QStringList out;
    collectFiles(root, QString(), &out);
    return out;
}

// Exact name first; then "./name", "/name" and a case-insensitive match
// (books packed on Windows often disagree with their own manifest)
QByteArray zipFile(const KArchiveDirectory *root, const QString &name)
{
    if (name.isEmpty())
        return {};
    QString n = name;
    while (n.startsWith(QLatin1String("./")))
        n = n.mid(2);
    while (n.startsWith(QLatin1Char('/')))
        n = n.mid(1);
    const KArchiveEntry *e = root->entry(n);
    if (!e || !e->isFile()) {
        e = nullptr;
        for (const QString &f : zipFiles(root)) {
            if (f.compare(n, Qt::CaseInsensitive) == 0) {
                e = root->entry(f);
                break;
            }
        }
    }
    if (!e || !e->isFile())
        return {};
    return static_cast<const KArchiveFile *>(e)->data();
}

// Decodes an image, scaled down while decoding when it is far bigger than a page
QImage readImage(const QByteArray &bytes, const QSize &fit = QSize(1600, 2200))
{
    QBuffer buf;
    buf.setData(bytes);
    QImageReader r(&buf);
    r.setAutoTransform(true);
    const QSize s = r.size();
    if (s.isValid() && (s.width() > fit.width() || s.height() > fit.height()))
        r.setScaledSize(s.scaled(fit, Qt::KeepAspectRatio));
    return r.read();
}

// "OEBPS/text/../images/a.jpg" -> "OEBPS/images/a.jpg"
QString resolve(const QString &baseFile, const QString &href)
{
    QString h = QUrl::fromPercentEncoding(href.section(QLatin1Char('#'), 0, 0).toUtf8());
    if (h.isEmpty())
        return baseFile;
    const QString dir = baseFile.contains(QLatin1Char('/')) ? baseFile.section(QLatin1Char('/'), 0, -2) + QLatin1Char('/') : QString();
    QString joined = h.startsWith(QLatin1Char('/')) ? h.mid(1) : dir + h;
    return QDir::cleanPath(joined);
}

QString attr(const QXmlStreamAttributes &a, const char *name)
{
    for (const QXmlStreamAttribute &x : a) {
        if (x.name() == QLatin1String(name) || x.qualifiedName() == QLatin1String(name))
            return x.value().toString();
    }
    return {};
}

QString decodeText(const QByteArray &raw)
{
    QByteArray data = raw;
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString s = utf8.decode(data);
    if (utf8.hasError())
        s = QString::fromLatin1(data);   // old Windows-1252/Latin-1 text files
    return s;
}

const QString HeadingRe = QStringLiteral(
    "^(cap[íi]tulo|chapter|parte|part|livro|book|pr[óo]logo|prologue|ep[íi]logo|epilogue|introdu[çc][ãa]o|introduction|pref[áa]cio|preface)\\b");

} // namespace

QStringList suffixes()
{
    return {QStringLiteral("epub"), QStringLiteral("txt"), QStringLiteral("text"), QStringLiteral("md"),
            QStringLiteral("docx"), QStringLiteral("odt")};
}

QString formatOf(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("text") || suffix == QLatin1String("md") || suffix == QLatin1String("txt"))
        return QStringLiteral("txt");
    if (suffixes().contains(suffix))
        return suffix;
    if (suffix == QLatin1String("pdf"))
        return {};
    // no or a wrong extension ("book.zip", a download without a name): look inside.
    // Zip magic only, so a PDF or anything else isn't opened as an archive.
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.read(4) != QByteArrayLiteral("PK\x03\x04"))
        return {};
    f.close();
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly))
        return {};
    const KArchiveDirectory *root = zip.directory();
    const QString mimetype = QString::fromLatin1(zipFile(root, QStringLiteral("mimetype"))).trimmed();
    if (mimetype == QLatin1String("application/epub+zip") || !zipFile(root, QStringLiteral("META-INF/container.xml")).isEmpty())
        return QStringLiteral("epub");
    if (mimetype == QLatin1String("application/vnd.oasis.opendocument.text"))
        return QStringLiteral("odt");
    if (root->entry(QStringLiteral("word/document.xml")))
        return QStringLiteral("docx");
    for (const QString &f : zipFiles(root)) {
        if (f.endsWith(QLatin1String(".opf"), Qt::CaseInsensitive))
            return QStringLiteral("epub");
    }
    return {};
}

bool needsConversion(const QString &path)
{
    return !formatOf(path).isEmpty();
}

// ====================================================================== EPUB

namespace {

// Inner HTML of <body>, cleaned for QTextDocument; ids get a per-document prefix
QString cleanChapterHtml(QString html, const QString &prefix, const QString &docName, Book *book, int *imageCounter,
                         const KArchiveDirectory *root, const QHash<QString, QString> &docAnchors)
{
    static const QRegularExpression bodyRe(QStringLiteral("<body[^>]*>(.*)</body>"),
                                           QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const auto bm = bodyRe.match(html);
    if (bm.hasMatch())
        html = bm.captured(1);
    static const QRegularExpression drop(QStringLiteral("<(script|style|head)[^>]*>.*?</\\1>"),
                                         QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    html.remove(drop);
    static const QRegularExpression links(QStringLiteral("<link[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    html.remove(links);
    // SVG cover pages: <svg ...><image xlink:href="cover.jpg"/></svg> -> <img>
    static const QRegularExpression svgImage(QStringLiteral("<svg[^>]*>.*?<image[^>]*(?:xlink:)?href=[\"']([^\"']+)[\"'][^>]*>.*?</svg>"),
                                             QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    html.replace(svgImage, QStringLiteral("<img src=\"\\1\"/>"));
    // ids -> anchors QTextDocument knows (<a name>), unique across the book
    static const QRegularExpression ids(QStringLiteral("<([a-zA-Z0-9]+)([^>]*?)\\sid=\"([^\"]+)\""));
    html.replace(ids, QStringLiteral("<a name=\"%1_\\3\"></a><\\1\\2 id=\"%1_\\3\"").arg(prefix));
    // internal links -> #anchor (kept as text; the PDF has no internal links)
    static const QRegularExpression hrefs(QStringLiteral("href=\"([^\"#:]*\\.x?html?)(#[^\"]*)?\""), QRegularExpression::CaseInsensitiveOption);
    QString out;
    out.reserve(html.size());
    qsizetype last = 0;
    auto it = hrefs.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        const QString target = resolve(docName, m.captured(1));
        const QString frag = m.captured(2).mid(1);
        const QString p = docAnchors.value(target);
        out += QStringLiteral("href=\"#%1\"").arg(frag.isEmpty() ? p : p + QLatin1Char('_') + frag);
        last = m.capturedEnd();
    }
    out += html.mid(last);
    html = out;
    // images: load from the archive into the document's resources
    static const QRegularExpression imgs(QStringLiteral("<img([^>]*?)src=[\"']([^\"']+)[\"']([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    out.clear();
    last = 0;
    it = imgs.globalMatch(html);
    while (it.hasNext()) {
        const auto m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        const QString file = resolve(docName, m.captured(2));
        const QByteArray bytes = zipFile(root, file);
        if (!bytes.isEmpty()) {
            const QString name = QStringLiteral("img%1").arg(++*imageCounter);
            book->images.insert(name, bytes);
            out += QStringLiteral("<img src=\"%1\"/>").arg(name);
        }
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

void parseNav(const QByteArray &xml, const QString &navName, Book *book, const QHash<QString, QString> &docAnchors)
{
    QXmlStreamReader r(xml);
    int depth = -1;
    bool inToc = false;
    int navDepth = 0;
    QString href, text;
    bool inA = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const auto n = r.name();
            if (n == QLatin1String("nav")) {
                ++navDepth;
                const QString type = attr(r.attributes(), "type");
                if (type == QLatin1String("toc") || (!inToc && book->toc.isEmpty() && type.isEmpty()))
                    inToc = true;
            } else if (inToc && n == QLatin1String("ol")) {
                ++depth;
            } else if (inToc && n == QLatin1String("a")) {
                inA = true;
                href = attr(r.attributes(), "href");
                text.clear();
            }
        } else if (r.isCharacters() && inA) {
            text += r.text();
        } else if (r.isEndElement()) {
            const auto n = r.name();
            if (inToc && n == QLatin1String("a")) {
                inA = false;
                const QString file = resolve(navName, href);
                const QString frag = href.section(QLatin1Char('#'), 1);
                const QString p = docAnchors.value(file);
                if (!p.isEmpty())
                    book->toc.append({text.simplified(), qMax(0, depth), frag.isEmpty() ? p : p + QLatin1Char('_') + frag});
            } else if (inToc && n == QLatin1String("ol")) {
                --depth;
            } else if (n == QLatin1String("nav")) {
                --navDepth;
                if (inToc && !book->toc.isEmpty())
                    return;
                inToc = false;
            }
        }
    }
}

void parseNcx(const QByteArray &xml, const QString &ncxName, Book *book, const QHash<QString, QString> &docAnchors)
{
    QXmlStreamReader r(xml);
    int depth = -1;
    QString text;
    bool inText = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            if (r.name() == QLatin1String("navPoint")) {
                ++depth;
                text.clear();
            } else if (r.name() == QLatin1String("text")) {
                inText = true;
            } else if (r.name() == QLatin1String("content")) {
                const QString src = attr(r.attributes(), "src");
                const QString file = resolve(ncxName, src);
                const QString frag = src.section(QLatin1Char('#'), 1);
                const QString p = docAnchors.value(file);
                if (!p.isEmpty())
                    book->toc.append({text.simplified(), qMax(0, depth), frag.isEmpty() ? p : p + QLatin1Char('_') + frag});
            }
        } else if (r.isCharacters() && inText) {
            text += r.text();
        } else if (r.isEndElement()) {
            if (r.name() == QLatin1String("text"))
                inText = false;
            else if (r.name() == QLatin1String("navPoint"))
                --depth;
        }
    }
}

} // namespace

Book readEpub(const QString &path, QString *error, const Progress &progress)
{
    Book book;
    book.format = QStringLiteral("epub");
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("it is not a valid EPUB (zip) file");
        return book;
    }
    const KArchiveDirectory *root = zip.directory();
    // container.xml -> the package document. The "mimetype" entry isn't needed,
    // and when container.xml is missing or broken the first .opf in the archive is used.
    QString opfName;
    {
        QXmlStreamReader r(zipFile(root, QStringLiteral("META-INF/container.xml")));
        while (!r.atEnd() && opfName.isEmpty()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("rootfile"))
                opfName = attr(r.attributes(), "full-path");
        }
    }
    QByteArray opf = zipFile(root, opfName);
    if (opf.isEmpty()) {
        for (const QString &f : zipFiles(root)) {
            if (f.endsWith(QLatin1String(".opf"), Qt::CaseInsensitive)) {
                opfName = f;
                opf = zipFile(root, f);
                break;
            }
        }
    }
    if (opf.isEmpty()) {
        *error = QObject::tr("the EPUB has no package document (content.opf)");
        return book;
    }
    struct Item { QString href, type, props; };
    QHash<QString, Item> manifest;
    QStringList manifestOrder;
    QStringList spine;
    QString ncxId, coverId;
    {
        QXmlStreamReader r(opf);
        QString current;
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                const auto n = r.name();
                if (n == QLatin1String("title") || n == QLatin1String("creator") || n == QLatin1String("language"))
                    current = n.toString();
                else if (n == QLatin1String("item")) {
                    const QString id = attr(r.attributes(), "id");
                    manifest.insert(id, {resolve(opfName, attr(r.attributes(), "href")),
                                         attr(r.attributes(), "media-type"), attr(r.attributes(), "properties")});
                    manifestOrder << id;
                }
                else if (n == QLatin1String("itemref") && attr(r.attributes(), "linear") != QLatin1String("no"))
                    spine << attr(r.attributes(), "idref");
                else if (n == QLatin1String("spine"))
                    ncxId = attr(r.attributes(), "toc");
                else if (n == QLatin1String("meta") && attr(r.attributes(), "name") == QLatin1String("cover"))
                    coverId = attr(r.attributes(), "content");
            } else if (r.isCharacters() && !current.isEmpty()) {
                const QString t = r.text().toString().simplified();
                if (current == QLatin1String("title") && book.title.isEmpty())
                    book.title = t;
                else if (current == QLatin1String("creator") && book.author.isEmpty())
                    book.author = t;
                else if (current == QLatin1String("language") && book.language.isEmpty())
                    book.language = t;
            } else if (r.isEndElement()) {
                current.clear();
            }
        }
    }
    // no usable spine: the HTML documents of the manifest, in its order
    auto isHtml = [](const Item &it) {
        return it.type.contains(QLatin1String("html")) || it.href.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
            || it.href.endsWith(QLatin1String(".xhtml"), Qt::CaseInsensitive) || it.href.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive);
    };
    spine.erase(std::remove_if(spine.begin(), spine.end(), [&](const QString &id) { return !manifest.contains(id); }), spine.end());
    if (spine.isEmpty()) {
        for (const QString &id : std::as_const(manifestOrder)) {
            if (isHtml(manifest[id]) && !manifest[id].props.split(QLatin1Char(' ')).contains(QLatin1String("nav")))
                spine << id;
        }
    }
    for (auto it = manifest.cbegin(); it != manifest.cend(); ++it) {
        if (it->props.contains(QLatin1String("cover-image")))
            coverId = it.key();
    }
    if (!coverId.isEmpty() && manifest.contains(coverId) && manifest[coverId].type.startsWith(QLatin1String("image/")))
        book.cover = zipFile(root, manifest[coverId].href);

    QHash<QString, QString> docAnchors;   // spine file -> anchor prefix
    for (int i = 0; i < spine.size(); ++i)
        docAnchors.insert(manifest.value(spine[i]).href, QStringLiteral("d%1").arg(i));

    int images = 0;
    QString html;
    for (int i = 0; i < spine.size(); ++i) {
        if (progress)
            progress(qreal(i) / spine.size());
        const Item item = manifest.value(spine[i]);
        const QByteArray raw = zipFile(root, item.href);
        if (raw.isEmpty())
            continue;
        const QString prefix = docAnchors.value(item.href);
        if (i > 0)
            html += QStringLiteral("<div style=\"page-break-before: always\"></div>");
        html += QStringLiteral("<a name=\"%1\"></a>").arg(prefix);
        html += cleanChapterHtml(decodeText(raw), prefix, item.href, &book, &images, root, docAnchors);
    }
    // table of contents: EPUB 3 nav first, then the EPUB 2 NCX
    for (auto it = manifest.cbegin(); it != manifest.cend() && book.toc.isEmpty(); ++it) {
        if (it->props.split(QLatin1Char(' ')).contains(QLatin1String("nav")))
            parseNav(zipFile(root, it->href), it->href, &book, docAnchors);
    }
    if (book.toc.isEmpty() && manifest.contains(ncxId))
        parseNcx(zipFile(root, manifest[ncxId].href), manifest[ncxId].href, &book, docAnchors);
    book.html = html;
    if (html.trimmed().isEmpty())
        *error = QObject::tr("the EPUB has no readable text");
    return book;
}

// ====================================================================== plain text

Book readText(const QString &path, QString *error)
{
    Book book;
    book.format = QStringLiteral("txt");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("the file can't be read");
        return book;
    }
    QString text = decodeText(f.readAll());
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList paragraphs = text.split(QRegularExpression(QStringLiteral("\\n\\s*\\n")), Qt::SkipEmptyParts);
    static const QRegularExpression headingRe(HeadingRe, QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression romanRe(QStringLiteral("^[IVXLC]+[.)]?\\s*$"));
    QString html;
    int n = 0;
    for (const QString &p : paragraphs) {
        const QStringList lines = p.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString one = p.simplified();
        if (one.isEmpty())
            continue;
        bool heading = lines.size() == 1 && one.size() < 80 && !one.endsWith(QLatin1Char('.')) && !one.endsWith(QLatin1Char(','))
                       && (headingRe.match(one).hasMatch() || romanRe.match(one).hasMatch()
                           || (one.size() > 3 && one == one.toUpper() && one.contains(QRegularExpression(QStringLiteral("\\p{L}"))))
                           || one.startsWith(QLatin1String("# ")));
        QString clean = one;
        if (clean.startsWith(QLatin1String("#")))
            clean = clean.mid(clean.indexOf(QLatin1Char(' ')) + 1);
        if (heading) {
            const QString anchor = QStringLiteral("h%1").arg(++n);
            if (n > 1)
                html += QStringLiteral("<div style=\"page-break-before: always\"></div>");
            html += QStringLiteral("<a name=\"%1\"></a><h2>%2</h2>").arg(anchor, esc(clean));
            book.toc.append({clean, 0, anchor});
            continue;
        }
        // verse and lists keep their lines; prose flows
        int shortLines = 0;
        for (const QString &l : lines)
            shortLines += l.trimmed().size() < 55;
        if (lines.size() > 2 && shortLines * 3 >= lines.size() * 2) {
            QStringList kept;
            for (const QString &l : lines)
                kept << esc(l.trimmed());
            html += QStringLiteral("<p>%1</p>").arg(kept.join(QStringLiteral("<br/>")));
        } else {
            html += QStringLiteral("<p>%1</p>").arg(esc(one));
        }
    }
    book.title = QFileInfo(path).completeBaseName();
    book.html = html;
    if (html.isEmpty())
        *error = QObject::tr("the file is empty");
    return book;
}

// ====================================================================== DOCX

namespace {

void readCoreProps(const QByteArray &xml, Book *book)
{
    QXmlStreamReader r(xml);
    QString current;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement())
            current = r.name().toString();
        else if (r.isCharacters() && !r.isWhitespace()) {
            const QString t = r.text().toString().simplified();
            if (current == QLatin1String("title") && book->title.isEmpty())
                book->title = t;
            else if ((current == QLatin1String("creator") || current == QLatin1String("initial-creator")) && book->author.isEmpty())
                book->author = t;
            else if (current == QLatin1String("language") && book->language.isEmpty())
                book->language = t;
        } else if (r.isEndElement())
            current.clear();
    }
}

int headingLevel(const QString &style)
{
    static const QRegularExpression re(QStringLiteral("^(heading|t[íi]tulo|ttulo|overskrift|titre|[üu]berschrift)\\s*([1-6])$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(style.simplified());
    if (m.hasMatch())
        return m.captured(2).toInt();
    if (style.compare(QLatin1String("Title"), Qt::CaseInsensitive) == 0 || style.compare(QLatin1String("Ttulo"), Qt::CaseInsensitive) == 0)
        return 0;
    return -1;
}

} // namespace

Book readDocx(const QString &path, QString *error)
{
    Book book;
    book.format = QStringLiteral("docx");
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("it is not a valid DOCX file");
        return book;
    }
    const KArchiveDirectory *root = zip.directory();
    readCoreProps(zipFile(root, QStringLiteral("docProps/core.xml")), &book);
    QXmlStreamReader r(zipFile(root, QStringLiteral("word/document.xml")));
    QString html, para, style;
    bool bold = false, italic = false, inRunProps = false, pageBreakBefore = false;
    int n = 0;
    while (!r.atEnd()) {
        r.readNext();
        const auto name = r.name();
        if (r.isStartElement()) {
            if (name == QLatin1String("p")) {
                para.clear();
                style.clear();
                pageBreakBefore = false;
            } else if (name == QLatin1String("pStyle")) {
                style = attr(r.attributes(), "w:val");
            } else if (name == QLatin1String("rPr")) {
                inRunProps = true;
            } else if (name == QLatin1String("r")) {
                bold = italic = false;
            } else if (inRunProps && name == QLatin1String("b")) {
                bold = attr(r.attributes(), "w:val") != QLatin1String("0");
            } else if (inRunProps && name == QLatin1String("i")) {
                italic = attr(r.attributes(), "w:val") != QLatin1String("0");
            } else if (name == QLatin1String("t")) {
                QString t = esc(r.readElementText());
                if (bold)
                    t = QStringLiteral("<b>") + t + QStringLiteral("</b>");
                if (italic)
                    t = QStringLiteral("<i>") + t + QStringLiteral("</i>");
                para += t;
            } else if (name == QLatin1String("tab")) {
                para += QLatin1Char(' ');
            } else if (name == QLatin1String("br")) {
                if (attr(r.attributes(), "w:type") == QLatin1String("page"))
                    pageBreakBefore = true;
                else
                    para += QStringLiteral("<br/>");
            }
        } else if (r.isEndElement()) {
            if (name == QLatin1String("rPr")) {
                inRunProps = false;
            } else if (name == QLatin1String("p")) {
                const int level = headingLevel(style);
                if (pageBreakBefore)
                    html += QStringLiteral("<div style=\"page-break-before: always\"></div>");
                if (level >= 0 && !para.trimmed().isEmpty()) {
                    const QString plain = QTextDocumentFragment::fromHtml(para).toPlainText().simplified();
                    if (level == 0 && book.title.isEmpty())
                        book.title = plain;
                    const QString anchor = QStringLiteral("h%1").arg(++n);
                    const int h = qBound(1, level, 4);
                    html += QStringLiteral("<a name=\"%1\"></a><h%2>%3</h%2>").arg(anchor).arg(h).arg(para);
                    book.toc.append({plain, qMax(0, level - 1), anchor});
                } else if (!para.trimmed().isEmpty()) {
                    html += QStringLiteral("<p>%1</p>").arg(para);
                }
            }
        }
    }
    if (book.title.isEmpty())
        book.title = QFileInfo(path).completeBaseName();
    book.html = html;
    if (html.isEmpty())
        *error = QObject::tr("the document has no text");
    return book;
}

// ====================================================================== ODT

Book readOdt(const QString &path, QString *error)
{
    Book book;
    book.format = QStringLiteral("odt");
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("it is not a valid ODT file");
        return book;
    }
    const KArchiveDirectory *root = zip.directory();
    readCoreProps(zipFile(root, QStringLiteral("meta.xml")), &book);
    QXmlStreamReader r(zipFile(root, QStringLiteral("content.xml")));
    QString html, para;
    int level = -1;   // -1 paragraph, else heading level
    int depth = 0;    // nesting of text:p / text:h
    int n = 0;
    bool inBody = false;
    while (!r.atEnd()) {
        r.readNext();
        const QString q = r.qualifiedName().toString();
        if (r.isStartElement()) {
            if (q == QLatin1String("office:text"))
                inBody = true;
            if (!inBody)
                continue;
            if (q == QLatin1String("text:p") || q == QLatin1String("text:h")) {
                if (depth++ == 0) {
                    para.clear();
                    level = q == QLatin1String("text:h") ? qMax(1, attr(r.attributes(), "text:outline-level").toInt()) : -1;
                }
            } else if (q == QLatin1String("text:s")) {
                para += QString(qMax(1, attr(r.attributes(), "text:c").toInt()), QLatin1Char(' '));
            } else if (q == QLatin1String("text:tab")) {
                para += QLatin1Char(' ');
            } else if (q == QLatin1String("text:line-break")) {
                para += QStringLiteral("<br/>");
            } else if (q == QLatin1String("text:note")) {
                r.skipCurrentElement();   // footnotes don't belong in the reading flow
            }
        } else if (r.isCharacters() && depth > 0) {
            para += esc(r.text().toString());
        } else if (r.isEndElement()) {
            if (q == QLatin1String("office:text"))
                inBody = false;
            if ((q == QLatin1String("text:p") || q == QLatin1String("text:h")) && depth > 0 && --depth == 0) {
                const QString plain = QTextDocumentFragment::fromHtml(para).toPlainText().simplified();
                if (plain.isEmpty())
                    continue;
                if (level > 0) {
                    const QString anchor = QStringLiteral("h%1").arg(++n);
                    html += QStringLiteral("<a name=\"%1\"></a><h%2>%3</h%2>").arg(anchor).arg(qMin(level, 4)).arg(para);
                    book.toc.append({plain, level - 1, anchor});
                } else {
                    html += QStringLiteral("<p>%1</p>").arg(para);
                }
            }
        }
    }
    if (book.title.isEmpty())
        book.title = QFileInfo(path).completeBaseName();
    book.html = html;
    if (html.isEmpty())
        *error = QObject::tr("the document has no text");
    return book;
}

// ====================================================================== typesetting

bool typeset(const Book &book, const QString &pdfPath, QVariantList *outline, QString *error, QSizeF pagePoints,
             const Progress &progress)
{
    QSaveFile out(pdfPath);
    if (!out.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("the cache folder can't be written");
        return false;
    }
    QPdfWriter writer(&out);
    writer.setResolution(72);   // 1 device pixel = 1 point
    writer.setPageSize(QPageSize(pagePoints, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setTitle(book.title);
    writer.setCreator(QStringLiteral("Lingmo Documents"));

    QTextDocument doc;
    doc.documentLayout()->setPaintDevice(&writer);
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSizeF(11);
    doc.setDefaultFont(font);
    doc.setDefaultStyleSheet(QStringLiteral(
        "body { line-height: 140%; }"
        "p { margin-top: 0; margin-bottom: 7px; text-align: justify; }"
        "h1 { font-size: 20pt; margin-top: 24px; margin-bottom: 14px; }"
        "h2 { font-size: 16pt; margin-top: 22px; margin-bottom: 12px; }"
        "h3 { font-size: 13pt; margin-top: 16px; margin-bottom: 8px; }"
        "h4 { font-size: 11pt; margin-top: 12px; margin-bottom: 6px; }"
        "blockquote { margin-left: 18px; }"));
    const qreal contentWidth = pagePoints.width() - 2 * MarginPt;
    for (auto it = book.images.cbegin(); it != book.images.cend(); ++it) {
        QImage img = readImage(it.value());
        if (img.isNull())
            continue;
        // fit the text block (and keep images from growing past the page)
        const qreal maxW = contentWidth;
        const qreal maxH = pagePoints.height() - 2 * MarginPt - 20;
        const qreal s = std::min({1.0, maxW / img.width(), maxH / img.height()});
        if (s < 1.0)
            img = img.scaled(int(img.width() * s), int(img.height() * s), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        doc.addResource(QTextDocument::ImageResource, QUrl(it.key()), img);
    }
    QString html = book.html;
    if (!book.cover.isEmpty()) {
        QImage cover = readImage(book.cover);
        if (!cover.isNull()) {
            const qreal s = std::min({1.0, contentWidth / cover.width(), (pagePoints.height() - 2 * MarginPt - 4) / cover.height()});
            doc.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("cover")),
                            cover.scaled(int(cover.width() * s), int(cover.height() * s), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            html = QStringLiteral("<p align=\"center\"><img src=\"cover\"/></p><div style=\"page-break-before: always\"></div>") + html;
        }
    }
    doc.setHtml(QStringLiteral("<html><body>%1</body></html>").arg(html));
    QTextFrameFormat ff = doc.rootFrame()->frameFormat();
    ff.setMargin(MarginPt);
    doc.rootFrame()->setFrameFormat(ff);
    doc.setPageSize(pagePoints);

    // TOC anchors -> page and top
    QHash<QString, QPair<int, qreal>> anchorPos;
    const qreal pageH = pagePoints.height();
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        const QRectF br = doc.documentLayout()->blockBoundingRect(b);
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            for (const QString &name : frag.charFormat().anchorNames()) {
                if (!anchorPos.contains(name)) {
                    const int page = int(br.top() / pageH);
                    anchorPos.insert(name, {page, br.top() - page * pageH});
                }
            }
        }
    }
    if (outline) {
        outline->clear();
        for (int i = 0; i < book.toc.size(); ++i) {
            const Chapter &c = book.toc[i];
            const auto pos = anchorPos.value(c.anchor, {-1, -1});
            const bool children = i + 1 < book.toc.size() && book.toc[i + 1].depth > c.depth;
            outline->append(QVariantMap{{QStringLiteral("title"), c.title.isEmpty() ? QObject::tr("Chapter %1").arg(i + 1) : c.title},
                                        {QStringLiteral("depth"), c.depth},
                                        {QStringLiteral("page"), pos.first},
                                        {QStringLiteral("top"), pos.second},
                                        {QStringLiteral("hasChildren"), children}});
        }
    }
    // page by page (what QTextDocument::print does), reporting progress on long books
    {
        QPainter painter(&writer);
        const int pages = doc.pageCount();
        const QRectF body(QPointF(0, 0), pagePoints);
        for (int i = 0; i < pages; ++i) {
            if (i > 0)
                writer.newPage();
            painter.save();
            painter.translate(body.left(), body.top() - i * pageH);
            QAbstractTextDocumentLayout::PaintContext ctx;
            ctx.clip = QRectF(0, i * pageH, pagePoints.width(), pageH);
            ctx.palette.setColor(QPalette::Text, Qt::black);
            painter.setClipRect(ctx.clip);
            doc.documentLayout()->draw(&painter, ctx);
            painter.restore();
            if (progress && (i % 8 == 0))
                progress(qreal(i + 1) / pages);
        }
    }
    if (!out.commit()) {
        *error = QObject::tr("the converted document can't be saved");
        return false;
    }
    return true;
}

// ====================================================================== cache

Result convert(const QString &path, const QString &cacheDir, const Progress &progress)
{
    Result res;
    const QFileInfo fi(path);
    if (!fi.isFile() || !fi.isReadable()) {
        res.error = QObject::tr("the file can't be read");
        return res;
    }
    res.format = formatOf(path);
    if (res.format.isEmpty())
        res.format = QStringLiteral("txt");
    const QString dir = cacheDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/lingmo-reader/books")
        : cacheDir;
    QDir().mkpath(dir);
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(fi.absoluteFilePath().toUtf8());
    h.addData(QByteArray::number(fi.size()));
    h.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
    h.addData(QByteArray::number(CacheVersion));
    const QString base = dir + QLatin1Char('/') + QString::fromLatin1(h.result().toHex());
    res.pdfPath = base + QStringLiteral(".pdf");

    QFile metaFile(base + QStringLiteral(".json"));
    if (QFileInfo::exists(res.pdfPath) && metaFile.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(metaFile.readAll()).object();
        res.title = o.value(QStringLiteral("title")).toString();
        res.author = o.value(QStringLiteral("author")).toString();
        res.language = o.value(QStringLiteral("language")).toString();
        res.outline = o.value(QStringLiteral("outline")).toArray().toVariantList();
        if (QFileInfo::exists(base + QStringLiteral(".cover")))
            res.coverPath = base + QStringLiteral(".cover");
        return res;
    }
    metaFile.close();

    // reading the book is the first fifth of the work, typesetting the rest
    const Progress reading = progress ? Progress([&](qreal f) { progress(0.2 * f); }) : Progress();
    Book book;
    if (res.format == QLatin1String("epub"))
        book = readEpub(path, &res.error, reading);
    else if (res.format == QLatin1String("docx"))
        book = readDocx(path, &res.error);
    else if (res.format == QLatin1String("odt"))
        book = readOdt(path, &res.error);
    else
        book = readText(path, &res.error);
    if (!res.error.isEmpty())
        return res;
    res.title = book.title;
    res.author = book.author;
    res.language = book.language;
    if (progress)
        progress(0.2);
    if (!typeset(book, res.pdfPath, &res.outline, &res.error, QSizeF(420, 595),
                 progress ? Progress([&](qreal f) { progress(0.2 + 0.8 * f); }) : Progress()))
        return res;
    if (!book.cover.isEmpty()) {
        QFile c(base + QStringLiteral(".cover"));
        if (c.open(QIODevice::WriteOnly)) {
            c.write(book.cover);
            res.coverPath = c.fileName();
        }
    }
    QSaveFile m(base + QStringLiteral(".json"));
    if (m.open(QIODevice::WriteOnly)) {
        m.write(QJsonDocument(QJsonObject{{QStringLiteral("title"), res.title},
                                          {QStringLiteral("author"), res.author},
                                          {QStringLiteral("language"), res.language},
                                          {QStringLiteral("source"), fi.absoluteFilePath()},
                                          {QStringLiteral("outline"), QJsonArray::fromVariantList(res.outline)}})
                    .toJson());
        m.commit();
    }
    return res;
}

} // namespace BookConverter
