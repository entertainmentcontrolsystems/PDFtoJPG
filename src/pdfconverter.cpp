#include "pdfconverter.h"

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>

PdfConverter::PdfConverter(QObject *parent)
    : QObject(parent)
{
}

QString PdfConverter::findTool(const QString &name, const QString &toolsDir)
{
#ifdef Q_OS_WIN
    QString exeName = name + ".exe";
#else
    QString exeName = name;
#endif

    // Check toolsDir first
    if (!toolsDir.isEmpty()) {
        QString candidate = QDir(toolsDir).absoluteFilePath(exeName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    // Then check PATH
    return QStandardPaths::findExecutable(exeName);
}

int PdfConverter::getPageCount(const QString &pdfPath)
{
#ifdef HAVE_QT_PDF
    QPdfDocument doc;
    if (doc.load(pdfPath) != QPdfDocument::Error::None)
        return 0;
    return doc.pageCount();
#else
    // Fallback: use pdfinfo (from Poppler) to get page count
    // Note: getPageCount is static, so we can't use m_toolsDir here.
    // We check the app's tools/ directory as a convention.
    QString toolsDir = QCoreApplication::applicationDirPath() + "/tools";
    QString tool = findTool("pdfinfo", toolsDir);
    if (tool.isEmpty()) {
        qWarning() << "pdfinfo not found";
        return 0;
    }
    QProcess proc;
    proc.start(tool, QStringList() << pdfPath);
    if (!proc.waitForFinished(10000))
        return 0;
    QString output = proc.readAllStandardOutput();
    QRegularExpression re("Pages:\\s+(\\d+)");
    auto m = re.match(output);
    if (m.hasMatch())
        return m.captured(1).toInt();
    return 0;
#endif
}

QList<int> PdfConverter::parsePageRange(const QString &range, int totalPages)
{
    QList<int> pages;

    if (range.trimmed().isEmpty())
        return pages;  // empty = all pages (caller handles)

    // Parse "1-5, 8, 11-13" into individual 0-indexed page numbers
    const auto parts = range.split(',', Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        QString trimmed = part.trimmed();
        int dash = trimmed.indexOf('-');
        if (dash >= 0) {
            bool ok1, ok2;
            int start = trimmed.left(dash).trimmed().toInt(&ok1);
            int end = trimmed.mid(dash + 1).trimmed().toInt(&ok2);
            if (!ok1 || !ok2) continue;
            for (int p = start; p <= end && p <= totalPages; ++p) {
                if (p >= 1)
                    pages.append(p - 1);  // 1-indexed → 0-indexed
            }
        } else {
            bool ok;
            int p = trimmed.toInt(&ok);
            if (ok && p >= 1 && p <= totalPages)
                pages.append(p - 1);
        }
    }

    return pages;
}

QString PdfConverter::outputFilePath(const QString &pdfPath,
                                     const ConversionOptions &opts,
                                     int pageIndex,
                                     int totalPages)
{
    QFileInfo fi(pdfPath);
    QString baseName = fi.completeBaseName();
    QString ext = formatExtension(opts.format);

    // Determine output directory
    QString outDir;
    if (opts.sameFolder) {
        outDir = fi.absolutePath();
    } else {
        outDir = opts.outputDir;
        // Mirror subfolder structure if source is recursive
        // (handled by caller for batch mode)
    }

    QString filename;
    if (totalPages == 1 || !opts.splitPages) {
        filename = baseName + "." + ext;
    } else {
        int digits = qMax(2, QString::number(totalPages).length());
        filename = baseName + "_p" + QString::number(pageIndex + 1).rightJustified(digits, '0') + "." + ext;
    }

    return QDir(outDir).absoluteFilePath(filename);
}

QString PdfConverter::formatExtension(OutputFormat fmt)
{
    switch (fmt) {
    case OutputFormat::JPEG:      return "jpg";
    case OutputFormat::PNG:       return "png";
    case OutputFormat::WebP:      return "webp";
    case OutputFormat::TIFF:      return "tiff";
    case OutputFormat::BMP:       return "bmp";
    case OutputFormat::SVG_Vector: return "svg";
    case OutputFormat::SVG_Traced:  return "svg";
    case OutputFormat::DXF:       return "dxf";
    }
    return "jpg";
}

QString PdfConverter::formatName(OutputFormat fmt)
{
    switch (fmt) {
    case OutputFormat::JPEG:      return "JPEG";
    case OutputFormat::PNG:       return "PNG";
    case OutputFormat::WebP:      return "WebP";
    case OutputFormat::TIFF:      return "TIFF";
    case OutputFormat::BMP:       return "BMP";
    case OutputFormat::SVG_Vector: return "SVG (Vector)";
    case OutputFormat::SVG_Traced:  return "SVG (Traced)";
    case OutputFormat::DXF:       return "DXF (CAD)";
    }
    return "JPEG";
}

bool PdfConverter::isRaster(OutputFormat fmt)
{
    return fmt == OutputFormat::JPEG ||
           fmt == OutputFormat::PNG ||
           fmt == OutputFormat::WebP ||
           fmt == OutputFormat::TIFF ||
           fmt == OutputFormat::BMP;
}

bool PdfConverter::isVector(OutputFormat fmt)
{
    return !isRaster(fmt);
}

QImage PdfConverter::renderPage(
#ifdef HAVE_QT_PDF
    QPdfDocument *doc,
#else
    void *doc,
#endif
    int pageIndex, int dpi)
{
#ifdef HAVE_QT_PDF
    // Get page size in points (1/72 inch)
    QSizeF pageSize = doc->pagePointSize(pageIndex);
    if (pageSize.isEmpty())
        return QImage();

    // Calculate pixel dimensions at target DPI
    double scale = static_cast<double>(dpi) / 72.0;
    int widthPx = static_cast<int>(pageSize.width() * scale);
    int heightPx = static_cast<int>(pageSize.height() * scale);

    // Render
    return doc->render(pageIndex, QSize(widthPx, heightPx));
#else
    Q_UNUSED(doc)
    Q_UNUSED(pageIndex)
    Q_UNUSED(dpi)
    return QImage();
#endif
}

// Render a PDF page using pdftocairo (fallback when Qt6::Pdf not available)
// Renders to PNG via pdftocairo, then loads the PNG as a QImage
QImage PdfConverter::renderPageViaPdftocairo(const QString &pdfPath, int pageIndex, int dpi)
{
    QString tool = findTool("pdftocairo", m_toolsDir);
    if (tool.isEmpty()) {
        qWarning() << "pdftocairo not found";
        return QImage();
    }

    // Render to a temp file
    QString tempBase = QDir::temp().absoluteFilePath(
        QString("ecs_pdf_%1_%2").arg(QFileInfo(pdfPath).baseName()).arg(pageIndex));

    QProcess proc;
    QStringList args;
    args << "-png"                    // PNG output
         << "-r" << QString::number(dpi)   // DPI
         << "-f" << QString::number(pageIndex + 1)  // 1-indexed
         << "-l" << QString::number(pageIndex + 1)
         << "-singlefile"
         << pdfPath
         << tempBase;                // output base (pdftocairo appends .png)

    proc.start(tool, args);
    if (!proc.waitForFinished(60000)) {
        qWarning() << "pdftocairo timed out";
        return QImage();
    }

    if (proc.exitCode() != 0) {
        qWarning() << "pdftocairo failed, exit code" << proc.exitCode()
                  << ":" << proc.readAllStandardError();
        return QImage();
    }

    QString pngPath = tempBase + ".png";
    QImage image;
    if (image.load(pngPath)) {
        QFile::remove(pngPath);
        return image;
    }
    qWarning() << "Failed to load rendered PNG:" << pngPath;
    QFile::remove(pngPath);
    return QImage();
}

bool PdfConverter::saveImage(const QImage &image, const QString &path,
                              OutputFormat format, int quality)
{
    // Ensure parent directory exists
    QDir().mkpath(QFileInfo(path).absolutePath());

    const char *fmtStr = nullptr;
    switch (format) {
    case OutputFormat::JPEG:  fmtStr = "JPEG"; break;
    case OutputFormat::PNG:    fmtStr = "PNG";  break;
    case OutputFormat::WebP:   fmtStr = "WEBP"; break;
    case OutputFormat::TIFF:   fmtStr = "TIFF"; break;
    case OutputFormat::BMP:    fmtStr = "BMP";  break;
    default: return false;
    }

    // Convert to RGB for formats that don't support alpha
    QImage toSave = image;
    if (format == OutputFormat::JPEG || format == OutputFormat::BMP) {
        if (toSave.hasAlphaChannel())
            toSave = toSave.convertToFormat(QImage::Format_RGB888);
    }

    return toSave.save(path, fmtStr, quality);
}

void PdfConverter::convertToRaster(const QString &pdfPath,
                                    const ConversionOptions &opts,
                                    int fileIndex,
                                    int totalFiles)
{
    m_cancelled = false;

#ifdef HAVE_QT_PDF
    QPdfDocument doc;
    if (doc.load(pdfPath) != QPdfDocument::Error::None) {
        emit fileDone(fileIndex, false, 0, "Failed to open PDF");
        return;
    }

    int totalPages = doc.pageCount();
#else
    int totalPages = getPageCount(pdfPath);
#endif
    if (totalPages == 0) {
        emit fileDone(fileIndex, false, 0, "PDF has no pages");
        return;
    }

    // Determine which pages to convert
    QList<int> pages = parsePageRange(opts.pageRange, totalPages);
    if (pages.isEmpty()) {
        for (int i = 0; i < totalPages; ++i)
            pages.append(i);
    }

    int pagesWritten = 0;
    int pagesTotal = pages.size();

    for (int i = 0; i < pagesTotal; ++i) {
        if (m_cancelled) {
            emit fileDone(fileIndex, false, pagesWritten, "Cancelled");
            return;
        }

        int pageIdx = pages[i];

        // Check overwrite
        QString outPath = outputFilePath(pdfPath, opts, pageIdx, totalPages);
        if (!opts.overwrite && QFileInfo::exists(outPath)) {
            pagesWritten++;
            continue;
        }

        // Render
        QImage image;
#ifdef HAVE_QT_PDF
        image = renderPage(&doc, pageIdx, opts.dpi);
#else
        image = renderPageViaPdftocairo(pdfPath, pageIdx, opts.dpi);
#endif
        if (image.isNull()) {
            qWarning() << "Failed to render page" << pageIdx << "of" << pdfPath;
            continue;
        }

        // Save
        if (!saveImage(image, outPath, opts.format, opts.quality)) {
            qWarning() << "Failed to save" << outPath;
            continue;
        }

        pagesWritten++;

        // Progress: percent within this file
        int percent = static_cast<int>((i + 1) * 100.0 / pagesTotal);
        emit pageProgress(fileIndex, i, pagesTotal, percent);
    }

    QString msg = QString("Converted %1 → %2 pages")
                      .arg(QFileInfo(pdfPath).fileName())
                      .arg(pagesWritten);
    emit fileDone(fileIndex, true, pagesWritten, msg);
}

void PdfConverter::cancel()
{
    m_cancelled = true;
}