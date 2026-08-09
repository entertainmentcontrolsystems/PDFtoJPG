#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QFutureWatcher>  // full header, not just forward decl

#include "pdfconverter.h"
#include "vectorconverter.h"

class QListWidget;
class QProgressBar;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QDropArea;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onBrowseSource();
    void onBrowseOutput();
    void onAddFiles();
    void onClearFiles();
    void onConvert();
    void onCancel();
    void onFormatChanged(int index);

    void onPageProgress(int fileIndex, int pageIndex, int totalPages, int percent);
    void onFileDone(int fileIndex, bool success, int pagesWritten, int pagesFailed, const QString &message);
    void onAllDone(const ConversionResult &result);
    void updateEstimate();

private:
    void buildUi();
    void scanPdfs(const QString &path);
    void updateFileList();
    void setControlsEnabled(bool enabled);

    // ── State ──
    QStringList m_pdfFiles;
    QString m_sourceDir;
    QString m_outputDir;

    PdfConverter *m_rasterConverter;
    VectorConverter *m_vectorConverter;
    bool m_converting = false;
    int m_totalFilesConverted = 0;
    int m_totalErrors = 0;
    int m_totalPagesRendered = 0;
    int m_totalPagesFailed = 0;

    QFutureWatcher<void> *m_futureWatcher = nullptr;

    // ── UI elements ──
    QDropArea *m_dropArea;
    QListWidget *m_fileList;
    QLabel *m_fileCountLabel;
    QLineEdit *m_sourceEdit;
    QLineEdit *m_outputEdit;
    QPushButton *m_sourceBtn;
    QPushButton *m_outputBtn;
    QComboBox *m_formatCombo;
    QSpinBox *m_dpiSpin;
    QSpinBox *m_qualitySpin;
    QLabel *m_dpiLabel;
    QLabel *m_qualityLabel;
    QCheckBox *m_splitCheck;
    QCheckBox *m_sameFolderCheck;
    QCheckBox *m_overwriteCheck;
    QCheckBox *m_openWhenDoneCheck;
    QLineEdit *m_pageRangeEdit;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QLabel *m_estimateLabel;
    QPushButton *m_convertBtn;
    QPushButton *m_cancelBtn;
    QPushButton *m_clearBtn;

    friend class QDropArea;
};

#endif // MAINWINDOW_H