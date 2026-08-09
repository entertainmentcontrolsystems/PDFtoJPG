#include "vectorconverter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>
#include <QPainter>
#include <QImage>
#include <cmath>

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
#ifdef Q_OS_WIN
    QString exeName = name + ".exe";
#else
    QString exeName = name;
#endif
    if (!m_toolsDir.isEmpty()) {
        QString candidate = QDir(m_toolsDir).absoluteFilePath(exeName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QStandardPaths::findExecutable(exeName);
}

bool VectorConverter::hasPdftocairo() const
{
    return !findTool("pdftocairo").isEmpty();
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
    args << "-svg";
    args << "-f" << QString::number(firstPage);
    args << "-l" << QString::number(lastPage);
    args << pdfPath;
    args << outputPath;

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

    if (!hasPdftocairo()) {
        emit fileDone(fileIndex, false, 0, 0, "pdftocairo not found");
        return;
    }

    int totalPages = PdfConverter::getPageCount(pdfPath);
    if (totalPages == 0) {
        emit fileDone(fileIndex, false, 0, 0, "Failed to open PDF");
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
    int pagesFailed = 0;

    if (opts.splitPages) {
        int pagesTotal = pages.size();
        for (int i = 0; i < pagesTotal; ++i) {
            if (m_cancelled) {
                emit fileDone(fileIndex, false, pagesWritten, pagesFailed, "Cancelled");
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

            if (runPdftocairo(pdfPath, outPath, pageIdx + 1, pageIdx + 1)) {
                pagesWritten++;
            } else {
                pagesFailed++;
            }

            int percent = static_cast<int>((i + 1) * 100.0 / pagesTotal);
            emit pageProgress(fileIndex, i, pagesTotal, percent);
        }
    } else {
        QString outPath = QDir(outDir).absoluteFilePath(baseName + ".svg");
        if (!opts.overwrite && QFileInfo::exists(outPath)) {
            emit fileDone(fileIndex, true, 0, 0, "Skipped (exists)");
            return;
        }

        int firstPage = pages.first() + 1;
        int lastPage = pages.last() + 1;

        if (runPdftocairo(pdfPath, outPath, firstPage, lastPage)) {
            pagesWritten = pages.size();
        } else {
            pagesFailed = pages.size();
        }
    }

    bool success = (pagesFailed == 0);
    QString msg = QString("Converted %1 → %2 SVG pages%3")
                      .arg(fi.fileName()).arg(pagesWritten)
                      .arg(pagesFailed > 0 ? QString(" (%1 failed)").arg(pagesFailed) : "");
    emit fileDone(fileIndex, success, pagesWritten, pagesFailed, msg);
}

void VectorConverter::convertPdfToDxf(const QString &pdfPath,
                                        const ConversionOptions &opts,
                                        int fileIndex, int totalFiles)
{
    m_cancelled = false;

    if (!hasPdftocairo()) {
        emit fileDone(fileIndex, false, 0, 0, "pdftocairo not found");
        return;
    }

    int totalPages = PdfConverter::getPageCount(pdfPath);
    if (totalPages == 0) {
        emit fileDone(fileIndex, false, 0, 0, "Failed to open PDF");
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
    int pagesFailed = 0;
    int pagesTotal = pages.size();

    for (int i = 0; i < pagesTotal; ++i) {
        if (m_cancelled) {
            emit fileDone(fileIndex, false, pagesWritten, pagesFailed, "Cancelled");
            return;
        }

        int pageIdx = pages[i];

        QString outName = baseName;
        if (totalPages > 1) {
            int digits = qMax(2, QString::number(totalPages).length());
            outName += "_p" + QString::number(pageIdx + 1).rightJustified(digits, '0');
        }
        outName += ".dxf";
        QString dxfPath = QDir(outDir).absoluteFilePath(outName);

        if (!opts.overwrite && QFileInfo::exists(dxfPath)) {
            pagesWritten++;
            continue;
        }

        QString tempSvg = QDir::temp().absoluteFilePath(
            QString("ecs_pdf_%1_%2_%3.svg").arg(fi.completeBaseName())
                                           .arg(pageIdx)
                                           .arg(QCoreApplication::applicationPid()));

        if (!runPdftocairo(pdfPath, tempSvg, pageIdx + 1, pageIdx + 1)) {
            pagesFailed++;
            QFile::remove(tempSvg);
            continue;
        }

        if (svgToDxf(tempSvg, dxfPath)) {
            pagesWritten++;
        } else {
            pagesFailed++;
        }

        QFile::remove(tempSvg);

        int percent = static_cast<int>((i + 1) * 100.0 / pagesTotal);
        emit pageProgress(fileIndex, i, pagesTotal, percent);
    }

    bool success = (pagesFailed == 0);
    QString msg = QString("Converted %1 → %2 DXF pages%3")
                      .arg(fi.fileName()).arg(pagesWritten)
                      .arg(pagesFailed > 0 ? QString(" (%1 failed)").arg(pagesFailed) : "");
    emit fileDone(fileIndex, success, pagesWritten, pagesFailed, msg);
}

// ── SVG → DXF conversion ────────────────────────────────────────────

struct SvgPath {
    QList<QPointF> points;
    bool closed = false;
};

// Tessellate a cubic bezier into line segments
static void tessellateCubic(QList<QPointF> &out,
                             QPointF p0, QPointF p1, QPointF p2, QPointF p3,
                             int steps = 16)
{
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        double u = 1.0 - t;
        double x = u*u*u * p0.x() + 3*u*u*t * p1.x() + 3*u*t*t * p2.x() + t*t*t * p3.x();
        double y = u*u*u * p0.y() + 3*u*u*t * p1.y() + 3*u*t*t * p2.y() + t*t*t * p3.y();
        out.append(QPointF(x, y));
    }
}

// Tessellate a quadratic bezier into line segments
static void tessellateQuadratic(QList<QPointF> &out,
                                 QPointF p0, QPointF p1, QPointF p2,
                                 int steps = 16)
{
    for (int i = 1; i <= steps; ++i) {
        double t = static_cast<double>(i) / steps;
        double u = 1.0 - t;
        double x = u*u * p0.x() + 2*u*t * p1.x() + t*t * p2.x();
        double y = u*u * p0.y() + 2*u*t * p1.y() + t*t * p2.y();
        out.append(QPointF(x, y));
    }
}

// Tokenize SVG path data into commands and numbers
struct SvgToken {
    bool isCommand;
    QChar cmd;
    double value;
};

static QList<SvgToken> tokenizeSvgPath(const QString &d)
{
    QList<SvgToken> tokens;
    QRegularExpression rx("([MLHVCSQTAZmlhvcsqtaz])|(-?\\d*\\.?\\d+(?:[eE][-+]?\\d+)?)");
    auto it = rx.globalMatch(d);
    while (it.hasNext()) {
        auto m = it.next();
        QString tok = m.captured(0);
        if (tok[0].isLetter()) {
            tokens.append({true, tok[0], 0.0});
        } else {
            tokens.append({false, QChar(), tok.toDouble()});
        }
    }
    return tokens;
}

// Helper: read next number from token list at given index
static double nextNum(const QList<SvgToken> &tokens, int &idx)
{
    while (idx < tokens.size() && tokens[idx].isCommand)
        idx++;
    if (idx >= tokens.size())
        return 0.0;
    return tokens[idx++].value;
}

static QList<SvgPath> parseSvgPathData(const QString &d)
{
    QList<SvgPath> paths;
    SvgPath current;
    bool first = true;

    QList<SvgToken> tokens = tokenizeSvgPath(d);
    int idx = 0;

    QPointF lastPoint;
    QPointF lastControl;

    while (idx < tokens.size()) {
        if (!tokens[idx].isCommand) {
            idx++;
            continue;
        }

        QChar cmd = tokens[idx++].cmd;

        if (cmd == 'M' || cmd == 'm') {
            if (!first && !current.points.isEmpty()) {
                paths.append(current);
                current = SvgPath();
            }
            first = false;

            double x = nextNum(tokens, idx);
            double y = nextNum(tokens, idx);
            if (cmd == 'm' && !current.points.isEmpty()) {
                x += lastPoint.x();
                y += lastPoint.y();
            }
            current.points.append(QPointF(x, y));
            lastPoint = QPointF(x, y);

            // Implicit L: subsequent coordinate pairs are lineto
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                x = nextNum(tokens, idx);
                y = nextNum(tokens, idx);
                if (cmd == 'm') { x += lastPoint.x(); y += lastPoint.y(); }
                current.points.append(QPointF(x, y));
                lastPoint = QPointF(x, y);
            }
        } else if (cmd == 'L' || cmd == 'l') {
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double x = nextNum(tokens, idx);
                double y = nextNum(tokens, idx);
                if (cmd == 'l') { x += lastPoint.x(); y += lastPoint.y(); }
                current.points.append(QPointF(x, y));
                lastPoint = QPointF(x, y);
            }
        } else if (cmd == 'H' || cmd == 'h') {
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double x = nextNum(tokens, idx);
                if (cmd == 'h') x += lastPoint.x();
                current.points.append(QPointF(x, lastPoint.y()));
                lastPoint = QPointF(x, lastPoint.y());
            }
        } else if (cmd == 'V' || cmd == 'v') {
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double y = nextNum(tokens, idx);
                if (cmd == 'v') y += lastPoint.y();
                current.points.append(QPointF(lastPoint.x(), y));
                lastPoint = QPointF(lastPoint.x(), y);
            }
        } else if (cmd == 'C' || cmd == 'c') {
            QPointF p0 = lastPoint;
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double x1 = nextNum(tokens, idx), y1 = nextNum(tokens, idx);
                double x2 = nextNum(tokens, idx), y2 = nextNum(tokens, idx);
                double x3 = nextNum(tokens, idx), y3 = nextNum(tokens, idx);
                QPointF p1(x1, y1), p2(x2, y2), p3(x3, y3);
                if (cmd == 'c') { p1 += lastPoint; p2 += lastPoint; p3 += lastPoint; }
                tessellateCubic(current.points, p0, p1, p2, p3);
                lastPoint = p3;
                lastControl = p2;
                p0 = p3;
            }
        } else if (cmd == 'S' || cmd == 's') {
            QPointF p0 = lastPoint;
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double x2 = nextNum(tokens, idx), y2 = nextNum(tokens, idx);
                double x3 = nextNum(tokens, idx), y3 = nextNum(tokens, idx);
                QPointF p2(x2, y2), p3(x3, y3);
                if (cmd == 's') { p2 += lastPoint; p3 += lastPoint; }
                QPointF p1(2*lastPoint.x() - lastControl.x(), 2*lastPoint.y() - lastControl.y());
                tessellateCubic(current.points, p0, p1, p2, p3);
                lastPoint = p3;
                lastControl = p2;
                p0 = p3;
            }
        } else if (cmd == 'Q' || cmd == 'q') {
            QPointF p0 = lastPoint;
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double x1 = nextNum(tokens, idx), y1 = nextNum(tokens, idx);
                double x2 = nextNum(tokens, idx), y2 = nextNum(tokens, idx);
                QPointF p1(x1, y1), p2(x2, y2);
                if (cmd == 'q') { p1 += lastPoint; p2 += lastPoint; }
                tessellateQuadratic(current.points, p0, p1, p2);
                lastPoint = p2;
                lastControl = p1;
                p0 = p2;
            }
        } else if (cmd == 'T' || cmd == 't') {
            QPointF p0 = lastPoint;
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                double x2 = nextNum(tokens, idx), y2 = nextNum(tokens, idx);
                QPointF p2(x2, y2);
                if (cmd == 't') p2 += lastPoint;
                QPointF p1(2*lastPoint.x() - lastControl.x(), 2*lastPoint.y() - lastControl.y());
                tessellateQuadratic(current.points, p0, p1, p2);
                lastPoint = p2;
                lastControl = p1;
                p0 = p2;
            }
        } else if (cmd == 'A' || cmd == 'a') {
            // Elliptical arc: 7 params — approximate as line to endpoint
            while (idx < tokens.size() && !tokens[idx].isCommand) {
                // Skip rx, ry, rotation, large-arc-flag, sweep-flag (5 params)
                for (int j = 0; j < 5; j++) nextNum(tokens, idx);
                double x = nextNum(tokens, idx), y = nextNum(tokens, idx);
                if (cmd == 'a') { x += lastPoint.x(); y += lastPoint.y(); }
                current.points.append(QPointF(x, y));
                lastPoint = QPointF(x, y);
            }
        } else if (cmd == 'Z' || cmd == 'z') {
            current.closed = true;
            paths.append(current);
            current = SvgPath();
            first = true;
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
    QList<bool> allClosed;
    double svgWidth = 0, svgHeight = 0;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            if (xml.name() == "svg") {
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
                    svgWidth = w.toString().remove(QRegularExpression("[a-zA-Z%]")).toDouble();
                    svgHeight = h.toString().remove(QRegularExpression("[a-zA-Z%]")).toDouble();
                }
            } else if (xml.name() == "path") {
                QString d = xml.attributes().value("d").toString();
                if (!d.isEmpty()) {
                    QList<SvgPath> paths = parseSvgPathData(d);
                    for (const auto &p : paths) {
                        if (p.points.size() >= 2) {
                            allPolylines.append(p.points);
                            allClosed.append(p.closed);
                        }
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
                allClosed.append(false);
            } else if (xml.name() == "rect") {
                double x = xml.attributes().value("x").toDouble();
                double y = xml.attributes().value("y").toDouble();
                double w = xml.attributes().value("width").toDouble();
                double h = xml.attributes().value("height").toDouble();
                QList<QPointF> rect;
                rect << QPointF(x, y) << QPointF(x+w, y) << QPointF(x+w, y+h)
                     << QPointF(x, y+h) << QPointF(x, y);
                allPolylines.append(rect);
                allClosed.append(true);
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "SVG parse error:" << xml.errorString();
        return false;
    }

    file.close();

    if (allPolylines.isEmpty()) {
        qWarning() << "No vector paths found in SVG — not writing empty DXF";
        return false;
    }

    return writeDxf(dxfPath, allPolylines, allClosed, svgWidth, svgHeight);
}

bool VectorConverter::writeDxf(const QString &path,
                                const QList<QList<QPointF>> &polylines,
                                const QList<bool> &closedFlags,
                                double width, double height)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(6);

    out << "0\nSECTION\n";
    out << "2\nHEADER\n";
    out << "9\n$ACADVER\n";
    out << "1\nAC1015\n";
    out << "9\n$INSBASE\n";
    out << "10\n0.0\n20\n0.0\n30\n0.0\n";
    out << "0\nENDSEC\n";

    out << "0\nSECTION\n2\nTABLES\n";
    out << "0\nTABLE\n2\nLAYER\n70\n1\n";
    out << "0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n";
    out << "0\nENDTAB\n0\nENDSEC\n";

    out << "0\nSECTION\n2\nBLOCKS\n0\nENDSEC\n";

    out << "0\nSECTION\n2\nENTITIES\n";

    double flipY = (height > 0) ? height : 1000.0;

    for (int i = 0; i < polylines.size(); ++i) {
        const auto &poly = polylines[i];
        if (poly.size() < 2)
            continue;

        bool closed = (i < closedFlags.size()) ? closedFlags[i] : false;

        out << "0\nLWPOLYLINE\n";
        out << "8\n0\n";
        out << "90\n" << poly.size() << "\n";
        out << "70\n" << (closed ? 1 : 0) << "\n";

        for (const QPointF &pt : poly) {
            out << "10\n" << pt.x() << "\n";
            out << "20\n" << (flipY - pt.y()) << "\n";
        }
    }

    out << "0\nENDSEC\n";
    out << "0\nEOF\n";

    file.close();
    return true;
}