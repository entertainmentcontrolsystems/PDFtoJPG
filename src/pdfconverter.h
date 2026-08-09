#ifndef PDFCONVERTER_H
#define PDFCONVERTER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>
#include <functional>

#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif

enum class OutputFormat {
    // Raster
    JPEG,
    PNG,
    WebP,
    TIFF,
    BMP,
    // Vector
    SVG_Vector,   // PDF → SVG (preserve vector geometry, via pdftocairo)
    SVG_Traced,   // Image → SVG (raster trace, via potrace/autotrace)
    DXF           // PDF → SVG → DXF (for CAD import)
};

struct ConversionOptions {
    // Input
    QStringList inputFiles;       // PDF file paths

    // Output
    QString outputDir;            // Destination directory
    OutputFormat format = OutputFormat::JPEG;

    // Raster options
    int dpi = 150;                 // Render DPI (72-600)
    int quality = 90;              // JPEG/WebP quality (1-100)

    // Page selection
    QString pageRange;             // "1-5, 8, 11-13" or "" for all
    bool splitPages = true;        // One file per page (true) or...

    // Output naming
    bool sameFolder = false;       // Save beside source PDF
    bool overwrite = false;        // Overwrite existing files
    bool openWhenDone = true;      // Open output folder after conversion

    // Vector options
    bool traceColor = false;       // Color tracing (AutoTrace) vs B&W (Potrace)
};

struct ConversionResult {
    bool success = false;
    int filesProcessed = 0;
    int pagesRendered = 0;
    int errors = 0;
    QString errorMessage;
};

Q_DECLARE_METATYPE(ConversionResult)

/**
 * PdfConverter — raster conversion engine using QPdfDocument.
 *
 * Renders PDF pages to QImage at the requested DPI, then saves in the
 * selected raster format (JPEG, PNG, WebP, TIFF, BMP).
 */
class PdfConverter : public QObject
{
    Q_OBJECT

public:
    explicit PdfConverter(QObject *parent = nullptr);

    // Convert a single PDF file to raster images
    // Emits pageProgress and fileDone signals during conversion
    void convertToRaster(const QString &pdfPath,
                         const ConversionOptions &opts,
                         int fileIndex,
                         int totalFiles);

    // Get page count without rendering
    static int getPageCount(const QString &pdfPath);

    // Parse a page range string ("1-5, 8, 11-13") into a list of 0-indexed page numbers
    // Returns empty list if the range is invalid or means "all pages"
    static QList<int> parsePageRange(const QString &range, int totalPages);

    // Build the output filename for a given page
    static QString outputFilePath(const QString &pdfPath,
                                   const ConversionOptions &opts,
                                   int pageIndex,
                                   int totalPages);

    // Format helpers
    static QString formatExtension(OutputFormat fmt);
    static QString formatName(OutputFormat fmt);
    static bool isRaster(OutputFormat fmt);
    static bool isVector(OutputFormat fmt);

signals:
    void pageProgress(int fileIndex, int pageIndex, int totalPages, int percent);
    void fileDone(int fileIndex, bool success, int pagesWritten, const QString &message);
    void allDone(const ConversionResult &result);

public slots:
    void cancel();

private:
    bool m_cancelled = false;

    QImage renderPage(
#ifdef HAVE_QT_PDF
        class QPdfDocument *doc,
#else
        void *doc,  // unused when no Qt PDF
#endif
        int pageIndex, int dpi);
    
    // Fallback: render a PDF page using pdftocairo CLI (when Qt6::Pdf not available)
    QImage renderPageViaPdftocairo(const QString &pdfPath, int pageIndex, int dpi);
    bool saveImage(const QImage &image, const QString &path,
                   OutputFormat format, int quality);
};

#endif // PDFCONVERTER_H