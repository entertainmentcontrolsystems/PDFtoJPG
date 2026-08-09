#ifndef VECTORCONVERTER_H
#define VECTORCONVERTER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QProcess>
#include <atomic>
#include "pdfconverter.h"

/**
 * VectorConverter — handles vector output formats.
 *
 * - PDF → SVG (vector-preserving): bundles pdftocairo CLI, called via QProcess
 * - PDF → DXF: PDF → SVG (pdftocairo) → DXF (custom SVG-to-DXF parser)
 *
 * All CLI tools are called as separate processes to avoid GPL contamination
 * of the main Qt application.
 */
class VectorConverter : public QObject
{
    Q_OBJECT

public:
    explicit VectorConverter(QObject *parent = nullptr);

    // Set the directory where bundled CLI tools live
    void setToolsDir(const QString &dir);
    QString toolsDir() const { return m_toolsDir; }

    // Check which tools are available
    bool hasPdftocairo() const;

    // PDF → SVG (vector-preserving, via pdftocairo)
    void convertPdfToSvg(const QString &pdfPath,
                         const ConversionOptions &opts,
                         int fileIndex, int totalFiles);

    // PDF → DXF (PDF → SVG via pdftocairo → DXF via custom converter)
    void convertPdfToDxf(const QString &pdfPath,
                         const ConversionOptions &opts,
                         int fileIndex, int totalFiles);

signals:
    void pageProgress(int fileIndex, int pageIndex, int totalPages, int percent);
    void fileDone(int fileIndex, bool success, int pagesWritten, int pagesFailed, const QString &message);
    void allDone(const ConversionResult &result);

public slots:
    void cancel();

private:
    std::atomic<bool> m_cancelled{false};
    QString m_toolsDir;

    // Find a tool: check toolsDir first, then PATH
    QString findTool(const QString &name) const;

    // Run pdftocairo to convert a PDF page to SVG
    // pdftocairo -svg writes to the exact output path given (no extension appended)
    bool runPdftocairo(const QString &pdfPath, const QString &outputPath,
                       int firstPage, int lastPage);

    // Convert SVG to DXF (custom C++ implementation)
    bool svgToDxf(const QString &svgPath, const QString &dxfPath);

    // Write a minimal DXF file from a list of polyline paths
    bool writeDxf(const QString &path, const QList<QList<QPointF>> &polylines,
                  const QList<bool> &closedFlags,
                  double width, double height);
};

#endif // VECTORCONVERTER_H