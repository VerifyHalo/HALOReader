#ifndef SEIZURE_ANALYZER_H
#define SEIZURE_ANALYZER_H

#include <QMainWindow>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QDate>
#include <QFileSystemWatcher>
#include <QPushButton>
#include <QListWidget>
#include <QSpinBox>
#include <QLineEdit>
#include <QSet>
#include <QWidget>
#include <QSplitter>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QMutex>

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
QT_END_NAMESPACE

struct SeizureRange {
    QDateTime start;
    QDateTime end;
    int channelIndex; // 0-31
    QString filePath;
    double durationSec;
};

class SeizureAnalyzer : public QMainWindow
{
    Q_OBJECT

public:
    SeizureAnalyzer(QWidget *parent = nullptr);
    ~SeizureAnalyzer();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void reloadData();
    void updateDisplay();
    void onChannelItemChanged(QListWidgetItem *item);
    void showChannelPopup();
    void onDailySelectionChanged();
    void selectDataFolder();
    void onDataDirectoryChanged(const QString &path);
    void processNewRhdFiles();
    void onFileProcessed(const QString& filePath, bool success, const QString& error);

private:
    void setupUI();
    void updateSeizureCounts();
    void updateLatestDetections();
    void updateDailyCounts();
    
    // UI Components
    QWidget *centralWidget;
    QVBoxLayout *mainLayout;
    QHBoxLayout *buttonLayout;
    QGridLayout *statsLayout;
    QGridLayout *dailyLayout;
    
    // Channel selection
    QPushButton *channelButton;
    QListWidget *channelList;
    QWidget *channelPopup;
    
    QPushButton *reloadButton;
    QPushButton *dataFolderButton;
    QLineEdit *dataFolderPathEdit;
    QPushButton *startButton;
    QPushButton *stopButton;
    QLabel *processingStatusLabel;
    QLineEdit *singleFilePathEdit;
    QPushButton *singleFileBrowseButton;
    QPushButton *singleFileProcessButton;
    QLabel *singleFileStatusLabel;
    QLabel *totalSeizuresLabel;
    QLabel *todaySeizuresLabel;
    QLabel *monthlySeizuresLabel;
    QLabel *lastUpdateLabel;
    QSpinBox *thresholdSpinBox;
    QSpinBox *windowTimeoutSpinBox;
    QSpinBox *transitionCountSpinBox;
    
    QTableWidget *latestDetectionsTable;
    QTableWidget *dailyCountsTable;
    QSplitter *tablesSplitter;
    
    // Data
    QList<SeizureRange> allDetections;
    QMap<QDate, int> dailyCounts;
    QTimer *updateTimer;
    
    QString dataDirectory;
    QSet<int> selectedChannels; // 0-based channel indices
    QDate selectedDate;
    QList<SeizureRange> visibleDetections;
    
    // Simple flag to prevent re-entrant calls
    bool isUpdating;
    
    // Data processing
    QFileSystemWatcher *dataWatcher;
    QStringList processedRhdFiles; // Track which files we've already processed
    QTimer *processingTimer; // Timer for continuous processing
    QThread *processingThread; // Background thread for file processing
    class FileProcessorWorker *processorWorker; // Worker object for background processing
    QMutex processingMutex; // Protect shared data
    bool isProcessing; // Track if currently processing a file
    QString currentSingleFilePath; // Track which file is being processed in single file mode
    
    bool channelSelected(int channelIndex) const;
    void updateProcessingStatus(bool processing);
    void updateProcessingStatusLabel(int processedCount, int processableFiles, int totalFiles);
    void startContinuousProcessing();
    void stopContinuousProcessing();
    void onStartClicked(); // Handle start button click
    void onStopClicked();  // Handle stop button click
    void onSingleFileBrowseClicked(); // Handle single file browse button click
    void onSingleFileProcessClicked(); // Handle single file process button click
    void onSingleFileProcessed(const QString& filePath, bool success, const QString& error); // Handle single file processing completion
    void onOpenDetectionClicked(); // Handle Open button click for visualization
    void openRawForDetection(const SeizureRange& detection); // Open waveform visualization for a detection
    void scanDetectionFiles(); // Scan for per-channel detection files
    void parseChannelDetectionFile(const QString& filePath, int channelIndex); // Parse a single channel detection file
};

#endif // SEIZURE_ANALYZER_H
