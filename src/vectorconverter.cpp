#include "vectorconverter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QStandardPaths>
#include <QDebug>
#include <QPainter>
#include <QImage>

VectorConverter::VectorConverter(QObject *parent)
    : QObject(parent)
{
}

void VectorConverter::setToolsDir(const QString &dir)
{
    m_toolsDir = dir;
}

QString VectorConverter::findTool(const QString &name) const
{
    // Check toolsDir first
    if (!m_toolsDir.isEmpty()) {
        QString candidate = QDir(m_toolsDir).absoluteFilePath(name);
        if (QFileInfo::exists(candidate))
            return candidate;
#ifdef Q_OS_WIN
        candidate += ".exe";
        if (QFileInfo::exists(candidate))
            return candidate;
#endif
    }
    // Then check PATH
    QString onPath = QStandardPaths::findExecutable(name);
    return onPath;
}

bool VectorConverter::hasPdftocairo() const
{
    return !findTool("pdftocairo").isEmpty();
}

bool VectorConverter::hasPotrace() const
{
    return !findTool("potrace").isEmpty();
}

bool VectorConverter::hasAutotrace() const
{
    return !findTool("autotrace").isEmpty();
}

void VectorConverter::cancel()
{
    m_cancelled = true;
}

bool VectorConverter::runPdftocairo(const QString &pdfPath,
                                     const QString &outputPath,
                                     int firstPage, int lastPage)
{
    QString tool = findTool("pdftocairo");
    if (tool.isEmpty()) {
        qWarning() << "pdftocairo not found";
        return false;
    }

    QStringList args;
    args << "-svg";                    // SVG output (vector-preserving)
    args << "-f" << QString::number(firstPage);
    args << "-l" << QString::number(lastPage);
    // Note: -singlefile is NOT supported with -svg output.
    // pdftocairo adds .svg extension to the output path.
    args << pdfPath;
    args << outputPath;                // Output base (pdftocairo appends .svg)

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    int exitCode = proc.execute(tool, args);

    return exitCode == 0;
}

void VectorConverter::convertPdfToSvg(const QString &pdfPath,
                                        const ConversionOptions &opts,
                                        int fileIndex, int totalFiles)
{
    m_cancelled = false;

    QString tool = findTool("pdftocairo");
    if (tool.isEmpty()) {
        emit fileDone(fileIndex, false, 0,
                       "pdftocairo not found. Install Poppler and ensure it's on PATH, "
                       "or bundle it with the app.");
        return;
    }

    // Get page count via QPdfDocument
    int totalPages = PdfConverter::getPageCount(pdfPath);
    if (totalPages == 0) {
        emit fileDone(fileIndex, false, 0, "Failed to open PDF");
        return;
    }

    QList<int> pages = PdfConverter::parsePageRange(opts.pageRange, totalPages);
    if (pages.isEmpty()) {
        for (int i = 0; i < totalPages; ++i)
            pages.append(i);
    }

    QFileInfo fi(pdfPath);
    QString baseName = fi.completeBaseName();
    QString outDir = opts.sameFolder ? fi.absolutePath() : opts.outputDir;
    QDir().mkpath(outDir);

    int pagesWritten = 0;

    if (opts.splitPages) {
        // One SVG per page
        int pagesTotal = pages.size();
        for (int i = 0; i < pagesTotal; ++i) {
            if (m_cancelled) {
                emit fileDone(fileIndex, false, pagesWritten, "Cancelled");
                return;
            }

            int pageIdx = pages[i];
            int digits = qMax(2, QString::number(totalPages).length());
            QString outName = baseName + "_p" +
                QString::number(pageIdx + 1).rightJustified(digits, '0') + ".svg";
            QString outPath = QDir(outDir).absoluteFilePath(outName);

            if (!opts.overwrite && QFileInfo::exists(outPath)) {
                pagesWritten++;
                continue;
            }

            // pdftocairo -svg does NOT add .svg extension, so pass the full path
            if (runPdftocairo(pdfPath, outPath, pageIdx + 1, pageIdx + 1)) {
                pagesWritten++;
            } else {
                qWarning() << "pdftocairo failed for page" << pageIdx;
            }

            int percent = static_cast<int>((i + 1) * 100.0 / pagesTotal);
            emit pageProgress(fileIndex, i, pagesTotal, percent);
        }
    } else {
        // All pages in one SVG
        QString outPath = QDir(outDir).absoluteFilePath(baseName + ".svg");
        if (!opts.overwrite && QFileInfo::exists(outPath)) {
            emit fileDone(fileIndex, true, 0, "Skipped (exists)");
            return;
        }

        int firstPage = pages.first() + 1;
        int lastPage = pages.last() + 1;

        if (runPdftocairo(pdfPath, outPath, firstPage, lastPage)) {
            pagesWritten = pages.size();
        }
    }

    emit fileDone(fileIndex, pagesWritten > 0, pagesWritten,
                   QString("Converted %1 → %2 SVG pages")
                       .arg(fi.fileName()).arg(pagesWritten));
}

void VectorConverter::convertPdfToDxf(const QString &pdfPath,
                                        const ConversionOptions &opts,
                                        int fileIndex, int totalFiles)
{
    m_cancelled = false;

    // Pipeline: PDF → SVG (pdftocairo) → DXF (custom parser)
    // Step 1: Convert PDF to SVG (reuse the SVG conversion)
    // Step 2: Parse SVG paths and write DXF

    QString tool = findTool("pdftocairo");
    if (tool.isEmpty()) {
        emit fileDone(fileIndex, false, 0, "pdftocairo not found");
        return;
    }

    int totalPages = PdfConverter::getPageCount(pdfPath);
    if (totalPages == 0) {
        emit fileDone(fileIndex, false, 0, "Failed to open PDF");
        return;
    }

    QList<int> pages = PdfConverter::parsePageRange(opts.pageRange, totalPages);
    if (pages.isEmpty()) {
        for (int i = 0; i < totalPages; ++i)
            pages.append(i);
    }

    QFileInfo fi(pdfPath);
    QString baseName = fi.completeBaseName();
    QString outDir = opts.sameFolder ? fi.absolutePath() : opts.outputDir;
    QDir().mkpath(outDir);

    int pagesWritten = 0;
    int pagesTotal = pages.size();

    for (int i = 0; i < pagesTotal; ++i) {
        if (m_cancelled) {
            emit fileDone(fileIndex, false, pagesWritten, "Cancelled");
            return;
        }

        int pageIdx = pages[i];

        // Generate temp SVG via pdftocairo
        QString outName = baseName;
        if (opts.splitPages) {
            int digits = qMax(2, QString::number(totalPages).length());
            outName += "_p" + QString::number(pageIdx + 1).rightJustified(digits, '0');
        }
        outName += ".dxf";
        QString dxfPath = QDir(outDir).absoluteFilePath(outName);

        if (!opts.overwrite && QFileInfo::exists(dxfPath)) {
            pagesWritten++;
            continue;
        }

        // Create temp SVG
        QString tempSvg = QDir::temp().absoluteFilePath(
            QString("ecs_pdf_%1_%2.svg").arg(fi.baseName()).arg(pageIdx));

        if (!runPdftocairo(pdfPath, tempSvg, pageIdx + 1, pageIdx + 1)) {
            qWarning() << "pdftocairo failed for page" << pageIdx;
            continue;
        }

        // Convert SVG → DXF
        if (svgToDxf(tempSvg, dxfPath)) {
            pagesWritten++;
        } else {
            qWarning() << "SVG→DXF conversion failed for" << tempSvg;
        }

        // Clean up temp SVG
        QFile::remove(tempSvg);

        int percent = static_cast<int>((i + 1) * 100.0 / pagesTotal);
        emit pageProgress(fileIndex, i, pagesTotal, percent);
    }

    emit fileDone(fileIndex, pagesWritten > 0, pagesWritten,
                   QString("Converted %1 → %2 DXF pages")
                       .arg(fi.fileName()).arg(pagesWritten));
}

void VectorConverter::convertImageToSvg(const QString &imagePath,
                                         const QString &outputPath,
                                         bool colorTrace)
{
    Q_UNUSED(colorTrace)

    // Choose tool: autotrace for color, potrace for B&W
    QString tool = colorTrace ? findTool("autotrace") : findTool("potrace");
    if (tool.isEmpty()) {
        qWarning() << "No tracing tool found";
        return;
    }

    QStringList args;
    if (colorTrace) {
        // autotrace -output-format svg input.png > output.svg
        args << "-output-format" << "svg" << imagePath;
    } else {
        // potrace -b svg -o output.svg input.pbm
        args << "-b" << "svg" << "-o" << outputPath << imagePath;
    }

    QProcess proc;
    if (colorTrace) {
        // autotrace writes to stdout
        proc.setStandardOutputFile(outputPath);
        proc.start(tool, args);
        proc.waitForFinished(30000);
    } else {
        int exitCode = proc.execute(tool, args);
        if (exitCode != 0) {
            qWarning() << "potrace failed with code" << exitCode;
        }
    }
}

// ── SVG → DXF conversion ────────────────────────────────────────────

// Parse SVG path data "d" attribute and extract polyline points.
// This is a simplified parser that handles the most common path commands:
// M (moveto), L (lineto), H, V, C (cubic bezier → tessellated), Z (closepath)

struct SvgPath {
    QList<QPointF> points;
    bool closed = false;
};

static QList<SvgPath> parseSvgPathData(const QString &d)
{
    QList<SvgPath> paths;
    SvgPath current;
    bool first = true;

    QRegularExpression rx("([MLHVCSQTAZmlhvcsqtaz])|(-?\\d+\\.?\\d*(?:[eE][-+]?\\d+)?)");
    auto it = rx.globalMatch(d);

    QChar lastCmd;
    QPointF lastPoint;

    while (it.hasNext()) {
        auto m = it.next();
        QString tok = m.captured(0);

        if (tok[0].isLetter()) {
            QChar cmd = tok[0];
            lastCmd = cmd;

            if (cmd == 'M' || cmd == 'm') {
                if (!first && !current.points.isEmpty()) {
                    paths.append(current);
                    current = SvgPath();
                }
                first = false;

                // Next token is the first coordinate
                if (it.hasNext()) {
                    auto m2 = it.next();
                    double x = m2.captured(0).toDouble();
                    if (it.hasNext()) {
                        auto m3 = it.next();
                        double y = m3.captured(0).toDouble();
                        if (cmd == 'm' && !current.points.isEmpty()) {
                            x += lastPoint.x();
                            y += lastPoint.y();
                        }
                        current.points.append(QPointF(x, y));
                        lastPoint = QPointF(x, y);
                    }
                }
            } else if (cmd == 'L' || cmd == 'l') {
                if (it.hasNext()) {
                    double x = it.next().captured(0).toDouble();
                    if (it.hasNext()) {
                        double y = it.next().captured(0).toDouble();
                        if (cmd == 'l') {
                            x += lastPoint.x();
                            y += lastPoint.y();
                        }
                        current.points.append(QPointF(x, y));
                        lastPoint = QPointF(x, y);
                    }
                }
            } else if (cmd == 'H' || cmd == 'h') {
                if (it.hasNext()) {
                    double x = it.next().captured(0).toDouble();
                    if (cmd == 'h') x += lastPoint.x();
                    current.points.append(QPointF(x, lastPoint.y()));
                    lastPoint = QPointF(x, lastPoint.y());
                }
            } else if (cmd == 'V' || cmd == 'v') {
                if (it.hasNext()) {
                    double y = it.next().captured(0).toDouble();
                    if (cmd == 'v') y += lastPoint.y();
                    current.points.append(QPointF(lastPoint.x(), y));
                    lastPoint = QPointF(lastPoint.x(), y);
                }
            } else if (cmd == 'C' || cmd == 'c') {
                // Cubic bezier: 6 coordinates (3 control points + end point)
                // Tessellate into line segments
                if (cmd == 'C') {
                    for (int j = 0; j < 3 && it.hasNext(); j++) {
                        double x = it.next().captured(0).toDouble();
                        if (it.hasNext()) {
                            double y = it.next().captured(0).toDouble();
                            // Skip control points, just use the endpoint
                            if (j == 2) {
                                current.points.append(QPointF(x, y));
                                lastPoint = QPointF(x, y);
                            }
                        }
                    }
                } else {
                    for (int j = 0; j < 3 && it.hasNext(); j++) {
                        double x = it.next().captured(0).toDouble();
                        if (it.hasNext()) {
                            double y = it.next().captured(0).toDouble();
                            if (j == 2) {
                                x += lastPoint.x();
                                y += lastPoint.y();
                                current.points.append(QPointF(x, y));
                                lastPoint = QPointF(x, y);
                            }
                        }
                    }
                }
            } else if (cmd == 'Z' || cmd == 'z') {
                current.closed = true;
                paths.append(current);
                current = SvgPath();
                first = true;
            }
        }
    }

    if (!current.points.isEmpty())
        paths.append(current);

    return paths;
}

bool VectorConverter::svgToDxf(const QString &svgPath, const QString &dxfPath)
{
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QXmlStreamReader xml(&file);
    QList<QList<QPointF>> allPolylines;
    double svgWidth = 0, svgHeight = 0;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            if (xml.name() == "svg") {
                // Get viewBox or width/height
                QStringView w = xml.attributes().value("width");
                QStringView h = xml.attributes().value("height");
                QStringView vb = xml.attributes().value("viewBox");
                if (!vb.isEmpty()) {
                    QStringList parts = vb.toString().split(' ');
                    if (parts.size() >= 4) {
                        svgWidth = parts[2].toDouble();
                        svgHeight = parts[3].toDouble();
                    }
                } else {
                    svgWidth = w.toString().remove("pt").toDouble();
                    svgHeight = h.toString().remove("pt").toDouble();
                }
            } else if (xml.name() == "path") {
                QString d = xml.attributes().value("d").toString();
                if (!d.isEmpty()) {
                    QList<SvgPath> paths = parseSvgPathData(d);
                    for (const auto &p : paths) {
                        if (p.points.size() >= 2)
                            allPolylines.append(p.points);
                    }
                }
            } else if (xml.name() == "line") {
                double x1 = xml.attributes().value("x1").toDouble();
                double y1 = xml.attributes().value("y1").toDouble();
                double x2 = xml.attributes().value("x2").toDouble();
                double y2 = xml.attributes().value("y2").toDouble();
                QList<QPointF> line;
                line.append(QPointF(x1, y1));
                line.append(QPointF(x2, y2));
                allPolylines.append(line);
            } else if (xml.name() == "rect") {
                double x = xml.attributes().value("x").toDouble();
                double y = xml.attributes().value("y").toDouble();
                double w = xml.attributes().value("width").toDouble();
                double h = xml.attributes().value("height").toDouble();
                QList<QPointF> rect;
                rect << QPointF(x, y) << QPointF(x+w, y) << QPointF(x+w, y+h)
                     << QPointF(x, y+h) << QPointF(x, y);
                allPolylines.append(rect);
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "SVG parse error:" << xml.errorString();
        return false;
    }

    file.close();

    if (allPolylines.isEmpty()) {
        // No vector content found — write empty DXF
        qWarning() << "No vector paths found in SVG";
    }

    return writeDxf(dxfPath, allPolylines, svgWidth, svgHeight);
}

bool VectorConverter::writeDxf(const QString &path,
                                const QList<QList<QPointF>> &polylines,
                                double width, double height)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);

    // DXF R2000 minimal file structure
    // Sections: HEADER, TABLES, BLOCKS, ENTITIES, EOF

    // ── HEADER section ──────────────────────────────────────
    out << "0\nSECTION\n";
    out << "2\nHEADER\n";
    out << "9\n$ACADVER\n";
    out << "1\nAC1015\n";        // AutoCAD 2000
    out << "9\n$INSBASE\n";
    out << "10\n0.0\n20\n0.0\n30\n0.0\n";
    out << "0\nENDSEC\n";

    // ── TABLES section (minimal) ────────────────────────────
    out << "0\nSECTION\n2\nTABLES\n";
    out << "0\nTABLE\n2\nLAYER\n70\n1\n";
    out << "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n";
    out << "0\nENDTAB\n0\nENDSEC\n";

    // ── BLOCKS section (empty) ──────────────────────────────
    out << "0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n";

    // ── ENTITIES section ────────────────────────────────────
    out << "0\nSECTION\n2\nENTITIES\n";

    // Flip Y axis: SVG is top-down, DXF is bottom-up
    double flipY = (height > 0) ? height : 1000.0;

    for (const auto &poly : polylines) {
        if (poly.size() < 2)
            continue;

        // Write as LWPOLYLINE
        out << "0\nLWPOLYLINE\n";
        out << "8\n0\n";          // Layer 0
        out << "90\n" << poly.size() << "\n";    // Vertex count
        out << "70\n0\n";         // Open polyline

        for (const QPointF &pt : poly) {
            out << "10\n" << pt.x() << "\n";
            out << "20\n" << (flipY - pt.y()) << "\n";
        }
    }

    out << "0\nENDSEC\n";

    // ── EOF ─────────────────────────────────────────────────
    out << "0\nEOF\n";

    file.close();
    return true;
}