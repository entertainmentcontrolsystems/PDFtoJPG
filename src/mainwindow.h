#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringList>
#include <QFuture>
#include <QFutureWatcher>

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

friend class QDropArea;

private slots:
    void onBrowseSource();
    void onBrowseOutput();
    void onAddFiles();
    void onClearFiles();
    void onConvert();
    void onCancel();
    void onFormatChanged(int index);

    void onPageProgress(int fileIndex, int pageIndex, int totalPages, int percent);
    void onFileDone(int fileIndex, bool success, int pagesWritten, const QString &message);
    void onAllDone(const ConversionResult &result);

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
    QCheckBox *m_splitCheck;
    QCheckBox *m_sameFolderCheck;
    QCheckBox *m_overwriteCheck;
    QCheckBox *m_openWhenDoneCheck;
    QLineEdit *m_pageRangeEdit;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QPushButton *m_convertBtn;
    QPushButton *m_cancelBtn;
    QPushButton *m_clearBtn;
};

#endif // MAINWINDOW_H