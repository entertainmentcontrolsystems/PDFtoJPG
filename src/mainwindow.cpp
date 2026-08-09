#include "mainwindow.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QProgressBar>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDir>
#include <QDebug>
#include <QGroupBox>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QFutureWatcher>

// ── Custom drop area widget ─────────────────────────────────────────
class QDropArea : public QWidget {
public:
    explicit QDropArea(QWidget *parent = nullptr) : QWidget(parent) {
        setAcceptDrops(true);
        setMinimumHeight(100);
    }
protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        if (auto w = qobject_cast<MainWindow*>(window()))
            w->dropEvent(e);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_rasterConverter = new PdfConverter(this);
    m_vectorConverter = new VectorConverter(this);

    // Set tools directory (bundled CLI tools)
    QString appDir = QCoreApplication::applicationDirPath();
    QString toolsDir = QDir(appDir).absoluteFilePath("tools");
    m_rasterConverter->setToolsDir(toolsDir);
    m_vectorConverter->setToolsDir(toolsDir);

    buildUi();

    // Connect converter signals
    connect(m_rasterConverter, &PdfConverter::pageProgress,
            this, &MainWindow::onPageProgress);
    connect(m_rasterConverter, &PdfConverter::fileDone,
            this, &MainWindow::onFileDone);

    connect(m_vectorConverter, &VectorConverter::pageProgress,
            this, &MainWindow::onPageProgress);
    connect(m_vectorConverter, &VectorConverter::fileDone,
            this, &MainWindow::onFileDone);

    setWindowTitle("ECS PDF Converter");
    setMinimumSize(820, 620);
    resize(920, 700);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // ── Title ──
    auto *titleLabel = new QLabel("ECS PDF Converter");
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);

    auto *subtitleLabel = new QLabel("Convert PDF pages to images or vector formats");
    subtitleLabel->setStyleSheet("font-size: 11px; color: gray;");
    mainLayout->addWidget(subtitleLabel);

    // ── Source / Drop area ──
    auto *sourceGroup = new QGroupBox("Source");
    auto *sourceLayout = new QVBoxLayout(sourceGroup);

    m_dropArea = new QDropArea;
    auto *dropLayout = new QVBoxLayout(m_dropArea);
    dropLayout->setAlignment(Qt::AlignCenter);
    auto *dropLabel = new QLabel("Drop PDF files or a folder here\nor click \"Add Files\"");
    dropLabel->setAlignment(Qt::AlignCenter);
    dropLabel->setStyleSheet("font-size: 13px; color: gray;");
    dropLayout->addWidget(dropLabel);

    sourceLayout->addWidget(m_dropArea);

    auto *sourceBtnRow = new QHBoxLayout;
    m_sourceEdit = new QLineEdit;
    m_sourceEdit->setPlaceholderText("Source folder (for recursive scan)...");
    m_sourceBtn = new QPushButton("Browse...");
    connect(m_sourceBtn, &QPushButton::clicked, this, &MainWindow::onBrowseSource);
    sourceBtnRow->addWidget(new QLabel("Folder:"));
    sourceBtnRow->addWidget(m_sourceEdit, 1);
    sourceBtnRow->addWidget(m_sourceBtn);
    sourceLayout->addLayout(sourceBtnRow);

    auto *fileBtnRow = new QHBoxLayout;
    auto *addFilesBtn = new QPushButton("Add Files...");
    connect(addFilesBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    m_clearBtn = new QPushButton("Clear All");
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearFiles);
    fileBtnRow->addWidget(addFilesBtn);
    fileBtnRow->addWidget(m_clearBtn);
    fileBtnRow->addStretch();
    m_fileCountLabel = new QLabel("No files");
    m_fileCountLabel->setStyleSheet("color: gray; font-size: 11px;");
    fileBtnRow->addWidget(m_fileCountLabel);
    sourceLayout->addLayout(fileBtnRow);

    m_fileList = new QListWidget;
    m_fileList->setMaximumHeight(140);
    sourceLayout->addWidget(m_fileList);

    mainLayout->addWidget(sourceGroup);

    // ── Output / Options ──
    auto *outputGroup = new QGroupBox("Output");
    auto *outputLayout = new QGridLayout(outputGroup);
    outputLayout->setSpacing(10);
    outputLayout->setContentsMargins(12, 10, 12, 10);
    outputLayout->setColumnMinimumWidth(1, 280);
    outputLayout->setColumnStretch(1, 1);

    // Output folder
    outputLayout->addWidget(new QLabel("Output folder:"), 0, 0);
    m_outputEdit = new QLineEdit;
    m_outputEdit->setPlaceholderText("Leave empty to save beside source...");
    m_outputBtn = new QPushButton("Browse...");
    connect(m_outputBtn, &QPushButton::clicked, this, &MainWindow::onBrowseOutput);
    outputLayout->addWidget(m_outputEdit, 0, 1);
    outputLayout->addWidget(m_outputBtn, 0, 2);

    // Format — SVG_Traced removed (C1 fix: dead path, never implemented)
    outputLayout->addWidget(new QLabel("Format:"), 1, 0);
    m_formatCombo = new QComboBox;
    m_formatCombo->setMinimumWidth(300);
    m_formatCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_formatCombo->addItem("JPEG", static_cast<int>(OutputFormat::JPEG));
    m_formatCombo->addItem("PNG", static_cast<int>(OutputFormat::PNG));
    m_formatCombo->addItem("WebP", static_cast<int>(OutputFormat::WebP));
    m_formatCombo->addItem("TIFF", static_cast<int>(OutputFormat::TIFF));
    m_formatCombo->addItem("BMP", static_cast<int>(OutputFormat::BMP));
    m_formatCombo->insertSeparator(m_formatCombo->count());
    m_formatCombo->addItem("SVG (Vector — preserve geometry)", static_cast<int>(OutputFormat::SVG_Vector));
    m_formatCombo->addItem("DXF (CAD)", static_cast<int>(OutputFormat::DXF));
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onFormatChanged);
    outputLayout->addWidget(m_formatCombo, 1, 1, 1, 2);

    // DPI
    m_dpiLabel = new QLabel("DPI:");
    outputLayout->addWidget(m_dpiLabel, 2, 0);
    m_dpiSpin = new QSpinBox;
    m_dpiSpin->setRange(72, 600);
    m_dpiSpin->setValue(150);
    m_dpiSpin->setSingleStep(50);
    m_dpiSpin->setMinimumWidth(100);
    outputLayout->addWidget(m_dpiSpin, 2, 1);

    // Quality
    m_qualityLabel = new QLabel("Quality:");
    outputLayout->addWidget(m_qualityLabel, 3, 0);
    m_qualitySpin = new QSpinBox;
    m_qualitySpin->setRange(1, 100);
    m_qualitySpin->setValue(90);
    m_qualitySpin->setSingleStep(5);
    m_qualitySpin->setMinimumWidth(100);
    outputLayout->addWidget(m_qualitySpin, 3, 1);

    // Page range
    outputLayout->addWidget(new QLabel("Page range:"), 4, 0);
    m_pageRangeEdit = new QLineEdit;
    m_pageRangeEdit->setPlaceholderText("e.g. 1-5, 8, 11-13 (blank = all)");
    outputLayout->addWidget(m_pageRangeEdit, 4, 1, 1, 2);

    // Checkboxes
    m_splitCheck = new QCheckBox("Split multi-page PDFs (one file per page)");
    m_splitCheck->setChecked(true);
    outputLayout->addWidget(m_splitCheck, 5, 0, 1, 3);

    m_sameFolderCheck = new QCheckBox("Save to same folder as source");
    outputLayout->addWidget(m_sameFolderCheck, 6, 0, 1, 3);

    m_overwriteCheck = new QCheckBox("Overwrite existing files");
    outputLayout->addWidget(m_overwriteCheck, 7, 0, 1, 3);

    m_openWhenDoneCheck = new QCheckBox("Open output folder when done");
    m_openWhenDoneCheck->setChecked(true);
    outputLayout->addWidget(m_openWhenDoneCheck, 8, 0, 1, 3);

    mainLayout->addWidget(outputGroup);

    // ── Progress ──
    m_progressBar = new QProgressBar;
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready. Add PDFs to begin.");
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    mainLayout->addWidget(m_statusLabel);

    // ── Buttons ──
    auto *btnRow = new QHBoxLayout;
    m_convertBtn = new QPushButton("Convert");
    m_convertBtn->setStyleSheet("font-weight: bold; padding: 8px 20px;");
    connect(m_convertBtn, &QPushButton::clicked, this, &MainWindow::onConvert);
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MainWindow::onCancel);
    btnRow->addWidget(m_convertBtn);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    setCentralWidget(central);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        QString path = url.toLocalFile();
        if (path.isEmpty()) continue;

        QFileInfo fi(path);
        if (fi.isDir()) {
            m_sourceDir = path;
            m_sourceEdit->setText(path);
            scanPdfs(path);
        } else if (fi.suffix().compare("pdf", Qt::CaseInsensitive) == 0) {
            if (!m_pdfFiles.contains(path))
                m_pdfFiles.append(path);
        }
    }
    updateFileList();
}

void MainWindow::onBrowseSource()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select source folder");
    if (!dir.isEmpty()) {
        m_sourceDir = dir;
        m_sourceEdit->setText(dir);
        m_pdfFiles.clear();
        scanPdfs(dir);
        updateFileList();
    }
}

void MainWindow::onAddFiles()
{
    auto files = QFileDialog::getOpenFileNames(this, "Select PDF files",
        m_sourceDir, "PDF files (*.pdf)");
    for (const QString &f : files) {
        if (!m_pdfFiles.contains(f))
            m_pdfFiles.append(f);
    }
    if (!files.isEmpty() && m_outputEdit->text().isEmpty() && !m_pdfFiles.isEmpty()) {
        QFileInfo fi(m_pdfFiles.first());
        m_outputEdit->setText(QDir(fi.absolutePath()).absoluteFilePath(fi.baseName() + "_output"));
    }
    updateFileList();
}

void MainWindow::onBrowseOutput()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select output folder");
    if (!dir.isEmpty())
        m_outputEdit->setText(dir);
}

void MainWindow::onClearFiles()
{
    m_pdfFiles.clear();
    updateFileList();
}

void MainWindow::onFormatChanged(int index)
{
    auto fmt = static_cast<OutputFormat>(m_formatCombo->currentData().toInt());

    // M4 fix: Hide DPI/Quality for vector formats instead of just disabling
    bool isRaster = PdfConverter::isRaster(fmt);
    m_dpiSpin->setVisible(isRaster);
    m_dpiLabel->setVisible(isRaster);
    m_qualitySpin->setVisible(isRaster && (fmt == OutputFormat::JPEG || fmt == OutputFormat::WebP));
    m_qualityLabel->setVisible(isRaster && (fmt == OutputFormat::JPEG || fmt == OutputFormat::WebP));
}

void MainWindow::scanPdfs(const QString &path)
{
    QDir dir(path);
    auto entries = dir.entryInfoList(QStringList() << "*.pdf", QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &fi : entries) {
        QString absPath = fi.absoluteFilePath();
        if (!m_pdfFiles.contains(absPath))
            m_pdfFiles.append(absPath);
    }

    auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
    for (const QFileInfo &subdir : subdirs) {
        scanPdfs(subdir.absoluteFilePath());
    }
}

void MainWindow::updateFileList()
{
    m_fileList->clear();
    for (const QString &f : m_pdfFiles) {
        QFileInfo fi(f);
        m_fileList->addItem(fi.fileName() + "  —  " + fi.absolutePath());
    }
    int count = m_pdfFiles.size();
    m_fileCountLabel->setText(count == 0 ? "No files" :
                              QString("%1 file%2").arg(count).arg(count == 1 ? "" : "s"));
}

void MainWindow::onConvert()
{
    if (m_pdfFiles.isEmpty()) {
        QMessageBox::warning(this, "No files", "Add PDF files first.");
        return;
    }

    auto fmt = static_cast<OutputFormat>(m_formatCombo->currentData().toInt());

    ConversionOptions opts;
    opts.inputFiles = m_pdfFiles;
    opts.format = fmt;
    opts.dpi = m_dpiSpin->value();
    opts.quality = m_qualitySpin->value();
    opts.pageRange = m_pageRangeEdit->text();
    opts.splitPages = m_splitCheck->isChecked();
    opts.sameFolder = m_sameFolderCheck->isChecked();
    opts.overwrite = m_overwriteCheck->isChecked();
    opts.openWhenDone = m_openWhenDoneCheck->isChecked();

    if (!opts.sameFolder) {
        opts.outputDir = m_outputEdit->text().trimmed();
        if (opts.outputDir.isEmpty()) {
            QMessageBox::warning(this, "No output folder",
                "Set an output folder or check \"Save to same folder as source\".");
            return;
        }
        if (!QDir().mkpath(opts.outputDir)) {
            QMessageBox::critical(this, "Error",
                "Failed to create output directory: " + opts.outputDir);
            return;
        }
    }

    // Check vector tool availability
    if (fmt == OutputFormat::SVG_Vector || fmt == OutputFormat::DXF) {
        if (!m_vectorConverter->hasPdftocairo()) {
            QMessageBox::warning(this, "Tool Not Found",
                "pdftocairo (from Poppler) was not found.\n\n"
                "This tool is required for vector PDF → SVG/DXF conversion.\n\n"
                "Install Poppler:\n"
                "  macOS: brew install poppler\n"
                "  Windows: download from poppler-windows releases on GitHub\n\n"
                "Or bundle it in the 'tools/' folder next to the app.");
            return;
        }
    }

    m_converting = true;
    m_totalFilesConverted = 0;
    m_totalErrors = 0;
    m_totalPagesRendered = 0;
    m_totalPagesFailed = 0;
    setControlsEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, m_pdfFiles.size() * 100);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Converting...");

    // C2 fix: Run conversion in a worker thread using QtConcurrent
    // Capture copies for the lambda
    int totalFiles = m_pdfFiles.size();
    PdfConverter *raster = m_rasterConverter;
    VectorConverter *vector = m_vectorConverter;

    m_futureWatcher = new QFutureWatcher<void>(this);
    connect(m_futureWatcher, &QFutureWatcher<void>::finished, this, [this]() {
        ConversionResult result;
        result.success = (m_totalErrors == 0);
        result.filesProcessed = m_totalFilesConverted;
        result.pagesRendered = m_totalPagesRendered;
        result.pagesFailed = m_totalPagesFailed;
        result.errors = m_totalErrors;
        onAllDone(result);
        m_futureWatcher->deleteLater();
        m_futureWatcher = nullptr;
    });

    auto future = QtConcurrent::run([=]() {
        for (int i = 0; i < totalFiles; ++i) {
            const QString &pdfPath = m_pdfFiles[i];

            if (PdfConverter::isRaster(fmt)) {
                raster->convertToRaster(pdfPath, opts, i, totalFiles);
            } else if (fmt == OutputFormat::SVG_Vector) {
                vector->convertPdfToSvg(pdfPath, opts, i, totalFiles);
            } else if (fmt == OutputFormat::DXF) {
                vector->convertPdfToDxf(pdfPath, opts, i, totalFiles);
            }
        }
    });

    m_futureWatcher->setFuture(future);
}

void MainWindow::onCancel()
{
    m_rasterConverter->cancel();
    m_vectorConverter->cancel();
    m_statusLabel->setText("Cancelling...");
}

void MainWindow::onPageProgress(int fileIndex, int pageIndex, int totalPages, int percent)
{
    if (fileIndex < 0 || fileIndex >= m_pdfFiles.size())
        return;  // L1 fix: bounds check

    int overall = fileIndex * 100 + percent;
    m_progressBar->setValue(overall);

    QString fileName = QFileInfo(m_pdfFiles[fileIndex]).fileName();
    m_statusLabel->setText(QString("[%1/%2] %3 — page %4/%5 (%6%)")
        .arg(fileIndex + 1)
        .arg(m_pdfFiles.size())
        .arg(fileName)
        .arg(pageIndex + 1)
        .arg(totalPages)
        .arg(percent));
}

void MainWindow::onFileDone(int fileIndex, bool success, int pagesWritten, int pagesFailed, const QString &message)
{
    if (success)
        m_totalFilesConverted++;
    else
        m_totalErrors++;
    m_totalPagesRendered += pagesWritten;
    m_totalPagesFailed += pagesFailed;

    // Update file list item
    if (fileIndex >= 0 && fileIndex < m_fileList->count()) {
        QString text = m_fileList->item(fileIndex)->text();
        m_fileList->item(fileIndex)->setText(text + "  →  " + (success ? "✓" : "✗") + " " + message);
    }
}

void MainWindow::onAllDone(const ConversionResult &result)
{
    m_converting = false;
    setControlsEnabled(true);
    m_progressBar->setVisible(false);

    QString summary;
    if (result.errors == 0 && result.pagesFailed == 0) {
        summary = QString("Done — %1 file(s) converted, %2 pages written.")
                      .arg(result.filesProcessed)
                      .arg(result.pagesRendered);
        m_statusLabel->setText(summary);
        m_statusLabel->setStyleSheet("color: green; font-size: 11px;");
    } else {
        summary = QString("Done — %1 file(s) converted, %2 pages, %3 failed, %4 error(s).")
                      .arg(result.filesProcessed)
                      .arg(result.pagesRendered)
                      .arg(result.pagesFailed)
                      .arg(result.errors);
        m_statusLabel->setText(summary);
        m_statusLabel->setStyleSheet("color: darkorange; font-size: 11px;");
    }

    // Open output folder
    if (result.errors == 0 && result.pagesFailed == 0 && m_openWhenDoneCheck->isChecked()) {
        if (!m_pdfFiles.isEmpty()) {
            QString outDir = m_sameFolderCheck->isChecked()
                ? QFileInfo(m_pdfFiles.first()).absolutePath()
                : m_outputEdit->text();
            QDesktopServices::openUrl(QUrl::fromLocalFile(outDir));
        }
    }
}

void MainWindow::setControlsEnabled(bool enabled)
{
    m_convertBtn->setEnabled(enabled);
    m_cancelBtn->setEnabled(!enabled);
    m_clearBtn->setEnabled(enabled);
    m_sourceBtn->setEnabled(enabled);
    m_outputBtn->setEnabled(enabled);
    m_formatCombo->setEnabled(enabled);
    m_dpiSpin->setEnabled(enabled);
    m_qualitySpin->setEnabled(enabled);
    m_splitCheck->setEnabled(enabled);
    m_sameFolderCheck->setEnabled(enabled);
    m_overwriteCheck->setEnabled(enabled);
    m_openWhenDoneCheck->setEnabled(enabled);
    m_pageRangeEdit->setEnabled(enabled);
    m_sourceEdit->setEnabled(enabled);
    m_outputEdit->setEnabled(enabled);
}