#include <QMouseEvent>
#include "seizure_analyzer.h"
#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDebug>
#include <QTextStream>
#include <QDataStream>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QHeaderView>
#include <QMessageBox>
#include <QTimeZone>
#include <QSplitter>
#include <QFileDialog>
#include <QDirIterator>
#include <QGroupBox>
#include <QGridLayout>
#include "../core/seizure_processor.h"
#include "../core/rhd_reader.h"
#include "file_processor_worker.h"
#include <QDebug>
#include <QThread>
#include <QPainter>
#include <QPainterPath>
#include <QDialog>
#include <QFontMetrics>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <stdexcept>

// These GUI display constants must match the FPGA/ASIC datapath configuration.
// See fpga/halo_seizure datapath `define`s:
//   THRESHOLD_VALUE, WINDOW_TIMEOUT, TRANSITION_COUNT, CHANNELS_PER_PACKET.
// Default values match seizure_vivado/run_tests.py defaults:
static const int kCfgThresholdValue    = 150000;
static const int kCfgWindowTimeout     = 300;
static const int kCfgTransitionCount   = 50;

#include <QVector>
#include <chrono>
#include <algorithm>

SeizureAnalyzer::SeizureAnalyzer(QWidget *parent)
    : QMainWindow(parent)
    , centralWidget(nullptr)
    , updateTimer(nullptr)
    , dataDirectory("")
    , isUpdating(false)
    , dataWatcher(nullptr)
    , processingTimer(nullptr)
    , processingThread(nullptr)
    , processorWorker(nullptr)
    , isProcessing(false)
{
    // Set window icon
    setWindowIcon(QIcon(":/app_icon.png"));
    setupUI();
    
    // Create timer for batched channel updates (debounced)
    updateTimer = new QTimer(this);
    updateTimer->setSingleShot(true); // Only fire once per start
    connect(updateTimer, &QTimer::timeout, this, &SeizureAnalyzer::updateDisplay);
    
    // Set up processing timer (check for new files every 1 second)
    processingTimer = new QTimer(this);
    connect(processingTimer, &QTimer::timeout, this, &SeizureAnalyzer::processNewRhdFiles);
    
    // Set up background processing thread
    processingThread = new QThread(this);
    processorWorker = new FileProcessorWorker();
    processorWorker->moveToThread(processingThread);
    
    // Connect worker signals
    connect(processorWorker, &FileProcessorWorker::fileProcessed, 
            this, &SeizureAnalyzer::onFileProcessed, Qt::QueuedConnection);
    connect(processingThread, &QThread::finished, processorWorker, &QObject::deleteLater);
    
    processingThread->start();
    
    // Set up data directory watcher
    dataWatcher = new QFileSystemWatcher(this);
    connect(dataWatcher, &QFileSystemWatcher::directoryChanged, 
            this, &SeizureAnalyzer::onDataDirectoryChanged);
    
    setWindowTitle("HALO Reading Controller");
    setMinimumSize(800, 600);
}

SeizureAnalyzer::~SeizureAnalyzer()
{
    // Stop processing thread
    if (processorWorker) {
        processorWorker->stop();
    }
    if (processingThread) {
        processingThread->quit();
        processingThread->wait(3000); // Wait up to 3 seconds
        if (processingThread->isRunning()) {
            processingThread->terminate();
            processingThread->wait();
        }
    }
}

void SeizureAnalyzer::setupUI()
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    mainLayout = new QVBoxLayout(centralWidget);
    
    // Configuration Parameters Group Box
    QGroupBox *paramsGroupBox = new QGroupBox(this);
    paramsGroupBox->setCheckable(false);
    paramsGroupBox->setTitle("");
    QGridLayout *paramsGroupLayout = new QGridLayout(paramsGroupBox);
    paramsGroupLayout->setSpacing(10);
    paramsGroupLayout->setContentsMargins(15, 15, 15, 15);
    
    // Threshold parameter - label left, input right
    QLabel *thresholdLabel = new QLabel("Threshold:", paramsGroupBox);
    thresholdLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    thresholdSpinBox = new QSpinBox(paramsGroupBox);
    thresholdSpinBox->setRange(0, 1000000);
    thresholdSpinBox->setValue(kCfgThresholdValue);
    thresholdSpinBox->setToolTip("NEO threshold (Max ADC = 65535)");
    thresholdSpinBox->setFixedWidth(120);
    thresholdSpinBox->setFocusPolicy(Qt::ClickFocus);
    thresholdSpinBox->setAlignment(Qt::AlignRight);
    paramsGroupLayout->addWidget(thresholdLabel, 0, 0);
    paramsGroupLayout->addWidget(thresholdSpinBox, 0, 1, 1, 1, Qt::AlignRight);
    
    // Window timeout parameter - label left, input right
    QLabel *windowTimeoutLabel = new QLabel("Window Timeout:", paramsGroupBox);
    windowTimeoutLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    windowTimeoutSpinBox = new QSpinBox(paramsGroupBox);
    windowTimeoutSpinBox->setRange(1, 10000);
    windowTimeoutSpinBox->setValue(kCfgWindowTimeout);
    windowTimeoutSpinBox->setToolTip("Window timeout in samples");
    windowTimeoutSpinBox->setFixedWidth(120);
    windowTimeoutSpinBox->setFocusPolicy(Qt::ClickFocus);
    windowTimeoutSpinBox->setAlignment(Qt::AlignRight);
    paramsGroupLayout->addWidget(windowTimeoutLabel, 1, 0);
    paramsGroupLayout->addWidget(windowTimeoutSpinBox, 1, 1, 1, 1, Qt::AlignRight);
    
    // Transition count parameter - label left, input right
    QLabel *transitionCountLabel = new QLabel("Transition Count:", paramsGroupBox);
    transitionCountLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    transitionCountSpinBox = new QSpinBox(paramsGroupBox);
    transitionCountSpinBox->setRange(1, 1000);
    transitionCountSpinBox->setValue(kCfgTransitionCount);
    transitionCountSpinBox->setToolTip("Number of detections needed to start seizure");
    transitionCountSpinBox->setFixedWidth(120);
    transitionCountSpinBox->setFocusPolicy(Qt::ClickFocus);
    transitionCountSpinBox->setAlignment(Qt::AlignRight);
    paramsGroupLayout->addWidget(transitionCountLabel, 2, 0);
    paramsGroupLayout->addWidget(transitionCountSpinBox, 2, 1, 1, 1, Qt::AlignRight);
    
    // Set column stretch so inputs align properly
    paramsGroupLayout->setColumnStretch(0, 0); // Labels column - no stretch
    paramsGroupLayout->setColumnStretch(1, 1); // Inputs column - stretch to fill
    
    // Data Folder Group Box
    QGroupBox *dataFolderGroupBox = new QGroupBox(this);
    dataFolderGroupBox->setCheckable(false);
    dataFolderGroupBox->setTitle("");
    QGridLayout *dataFolderGroupLayout = new QGridLayout(dataFolderGroupBox);
    dataFolderGroupLayout->setSpacing(10);
    dataFolderGroupLayout->setContentsMargins(15, 15, 15, 15);
    
    // Data folder - label left, input + buttons full width
    QLabel *dataFolderLabel = new QLabel("Data Folder:", dataFolderGroupBox);
    dataFolderLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    dataFolderPathEdit = new QLineEdit(dataFolderGroupBox);
    dataFolderPathEdit->setReadOnly(true);
    dataFolderPathEdit->setPlaceholderText("No folder selected");
    dataFolderPathEdit->setToolTip("Selected data folder path");
    dataFolderPathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    dataFolderButton = new QPushButton("Browse...", dataFolderGroupBox);
    dataFolderButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(dataFolderButton, &QPushButton::clicked, this, &SeizureAnalyzer::selectDataFolder);
    
    // Create two square buttons for start/stop
    startButton = new QPushButton("▶", dataFolderGroupBox); // Play symbol
    startButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    startButton->setEnabled(false); // Disabled until data folder is selected
    startButton->setToolTip("Start Processing");
    startButton->setStyleSheet("QPushButton { color: green; } QPushButton:disabled { color: gray; }");
    connect(startButton, &QPushButton::clicked, this, &SeizureAnalyzer::onStartClicked);
    
    stopButton = new QPushButton("■", dataFolderGroupBox); // Stop symbol
    stopButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    stopButton->setEnabled(false); // Disabled when not processing
    stopButton->setToolTip("Stop Processing");
    stopButton->setStyleSheet("QPushButton { color: red; } QPushButton:disabled { color: gray; }");
    connect(stopButton, &QPushButton::clicked, this, &SeizureAnalyzer::onStopClicked);
    
    // Make buttons square - get height from startButton and set width to match
    // We'll set this after the button is shown, but for now set a reasonable size
    int buttonHeight = startButton->sizeHint().height();
    startButton->setFixedSize(buttonHeight, buttonHeight);
    stopButton->setFixedSize(buttonHeight, buttonHeight);
    
    // Create horizontal layout for data folder input + buttons
    QHBoxLayout *dataFolderRowLayout = new QHBoxLayout();
    dataFolderRowLayout->setSpacing(10);
    dataFolderRowLayout->setContentsMargins(0, 0, 0, 0);
    dataFolderRowLayout->addWidget(dataFolderPathEdit, 1); // Expand to fill
    dataFolderRowLayout->addWidget(dataFolderButton);
    dataFolderRowLayout->addWidget(startButton);
    dataFolderRowLayout->addWidget(stopButton);
    
    dataFolderGroupLayout->addWidget(dataFolderLabel, 0, 0);
    dataFolderGroupLayout->addLayout(dataFolderRowLayout, 0, 1);
    
    // Processing status label - shows processed/total files
    processingStatusLabel = new QLabel("Processed 0 / 0 RHD files", dataFolderGroupBox);
    processingStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    processingStatusLabel->setStyleSheet("font-size: 11px; color: #666;");
    dataFolderGroupLayout->addWidget(processingStatusLabel, 1, 0, 1, 2); // Span both columns
    
    // Set column stretch so inputs align properly
    dataFolderGroupLayout->setColumnStretch(0, 0); // Labels column - no stretch
    dataFolderGroupLayout->setColumnStretch(1, 1); // Inputs column - stretch to fill
    
    // Single File Processing Group Box
    QGroupBox *singleFileGroupBox = new QGroupBox(this);
    singleFileGroupBox->setCheckable(false);
    singleFileGroupBox->setTitle("");
    QGridLayout *singleFileGroupLayout = new QGridLayout(singleFileGroupBox);
    singleFileGroupLayout->setSpacing(10);
    singleFileGroupLayout->setContentsMargins(15, 15, 15, 15);
    
    // Single file - label left, input + buttons full width
    QLabel *singleFileLabel = new QLabel("Select File:", singleFileGroupBox);
    singleFileLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    singleFilePathEdit = new QLineEdit(singleFileGroupBox);
    singleFilePathEdit->setReadOnly(true);
    singleFilePathEdit->setPlaceholderText("No file selected");
    singleFilePathEdit->setToolTip("Selected RHD file path");
    singleFilePathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    singleFileBrowseButton = new QPushButton("Browse...", singleFileGroupBox);
    singleFileBrowseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(singleFileBrowseButton, &QPushButton::clicked, this, &SeizureAnalyzer::onSingleFileBrowseClicked);
    
    singleFileProcessButton = new QPushButton("Process", singleFileGroupBox);
    singleFileProcessButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    singleFileProcessButton->setEnabled(false); // Disabled until file is selected
    singleFileProcessButton->setToolTip("Process selected file");
    connect(singleFileProcessButton, &QPushButton::clicked, this, &SeizureAnalyzer::onSingleFileProcessClicked);
    
    // Create horizontal layout for single file input + buttons
    QHBoxLayout *singleFileRowLayout = new QHBoxLayout();
    singleFileRowLayout->setSpacing(10);
    singleFileRowLayout->setContentsMargins(0, 0, 0, 0);
    singleFileRowLayout->addWidget(singleFilePathEdit, 1); // Expand to fill
    singleFileRowLayout->addWidget(singleFileBrowseButton);
    singleFileRowLayout->addWidget(singleFileProcessButton);
    
    singleFileGroupLayout->addWidget(singleFileLabel, 0, 0);
    singleFileGroupLayout->addLayout(singleFileRowLayout, 0, 1);
    
    // Single file processing status label
    singleFileStatusLabel = new QLabel("", singleFileGroupBox);
    singleFileStatusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    singleFileStatusLabel->setStyleSheet("font-size: 11px; color: #666;");
    singleFileGroupLayout->addWidget(singleFileStatusLabel, 1, 0, 1, 2); // Span both columns
    
    // Set column stretch so inputs align properly
    singleFileGroupLayout->setColumnStretch(0, 0); // Labels column - no stretch
    singleFileGroupLayout->setColumnStretch(1, 1); // Inputs column - stretch to fill
    
    // Add all group boxes to main layout
    mainLayout->addWidget(paramsGroupBox);
    mainLayout->addWidget(dataFolderGroupBox);
    mainLayout->addWidget(singleFileGroupBox);
    
    // 3. RELOAD DATA + SELECT CHANNEL section
    QHBoxLayout *secondButtonLayout = new QHBoxLayout();
    secondButtonLayout->setSpacing(10);
    
    reloadButton = new QPushButton("Reload Data", this);
    connect(reloadButton, &QPushButton::clicked, this, &SeizureAnalyzer::reloadData);
    secondButtonLayout->addWidget(reloadButton);
    
    channelButton = new QPushButton("Select Channels", this);
    channelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    secondButtonLayout->addWidget(channelButton);
    
    secondButtonLayout->addStretch();

    // Channel popup setup
    channelPopup = new QWidget(this, Qt::Popup);
    channelPopup->setWindowFlag(Qt::FramelessWindowHint);
    QVBoxLayout *popupLayout = new QVBoxLayout(channelPopup);
    popupLayout->setContentsMargins(4,4,4,4);
    channelList = new QListWidget(channelPopup);
    channelList->setSelectionMode(QAbstractItemView::NoSelection);
    for (int i = 0; i < 32; ++i) {
        QString channelName = QString("A-%1").arg(i, 3, 10, QChar('0'));
        QListWidgetItem *item = new QListWidgetItem(channelName, channelList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    channelList->viewport()->installEventFilter(this); // toggle without closing popup
    popupLayout->addWidget(channelList);
    connect(channelList, &QListWidget::itemChanged, this, &SeizureAnalyzer::onChannelItemChanged);
    connect(channelButton, &QPushButton::clicked, this, &SeizureAnalyzer::showChannelPopup);
    
    // 4. INFO section
    statsLayout = new QGridLayout();
    totalSeizuresLabel = new QLabel("Total Seizures: 0", this);
    todaySeizuresLabel = new QLabel("Today: 0", this);
    monthlySeizuresLabel = new QLabel("This Month: 0", this);
    lastUpdateLabel = new QLabel("Last Update: Never", this);
    
    totalSeizuresLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    todaySeizuresLabel->setStyleSheet("font-size: 14px;");
    monthlySeizuresLabel->setStyleSheet("font-size: 14px;");
    lastUpdateLabel->setStyleSheet("font-size: 12px; color: gray;");
    
    statsLayout->addWidget(totalSeizuresLabel,      0, 0);
    statsLayout->addWidget(todaySeizuresLabel,      0, 1);
    statsLayout->addWidget(monthlySeizuresLabel,    0, 2);
    statsLayout->addWidget(lastUpdateLabel,         0, 3);
    
    // Daily counts table
    QLabel *dailyLabel = new QLabel("Daily Counts:", this);
    dailyLabel->setStyleSheet("font-weight: bold;");
    
    dailyCountsTable = new QTableWidget(0, 2, this);
    dailyCountsTable->setHorizontalHeaderLabels({"Date", "Seizure Count"});
    dailyCountsTable->horizontalHeader()->setStretchLastSection(true);
    dailyCountsTable->setAlternatingRowColors(true);
    dailyCountsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dailyCountsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(dailyCountsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SeizureAnalyzer::onDailySelectionChanged);
    
    // Latest detections table (will show all for selected day, or all days if none selected)
    QLabel *latestLabel = new QLabel("Detections:", this);
    latestLabel->setStyleSheet("font-weight: bold;");
    
    latestDetectionsTable = new QTableWidget(0, 6, this);
    latestDetectionsTable->setHorizontalHeaderLabels({"Channel", "Start", "End", "Duration (s)", "File", "RAW Waveform"});
    latestDetectionsTable->horizontalHeader()->setStretchLastSection(true);
    latestDetectionsTable->setAlternatingRowColors(true);
    latestDetectionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    latestDetectionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    
    // Create containers for each table section
    QWidget *dailyContainer = new QWidget(this);
    QVBoxLayout *dailyContainerLayout = new QVBoxLayout(dailyContainer);
    dailyContainerLayout->setContentsMargins(0, 0, 0, 0);
    dailyContainerLayout->addWidget(dailyLabel);
    dailyContainerLayout->addWidget(dailyCountsTable);
    
    QWidget *detectionsContainer = new QWidget(this);
    QVBoxLayout *detectionsContainerLayout = new QVBoxLayout(detectionsContainer);
    detectionsContainerLayout->setContentsMargins(0, 0, 0, 0);
    detectionsContainerLayout->addWidget(latestLabel);
    detectionsContainerLayout->addWidget(latestDetectionsTable);
    
    // Create splitter for resizable tables
    tablesSplitter = new QSplitter(Qt::Vertical, this);
    tablesSplitter->addWidget(dailyContainer);
    tablesSplitter->addWidget(detectionsContainer);
    tablesSplitter->setStretchFactor(0, 1);
    tablesSplitter->setStretchFactor(1, 2); // Detections table gets more space by default
    
    // Add to main layout in order:
    // Group boxes already added above (paramsGroupBox and dataFolderGroupBox)
    mainLayout->addLayout(secondButtonLayout);   // 2. Reload Data + Select Channels
    mainLayout->addLayout(statsLayout);          // 3. Info
    mainLayout->addWidget(tablesSplitter);       // 4. Daily counts + Detections tables
}

void SeizureAnalyzer::reloadData()
{
    QMutexLocker locker(&processingMutex);
    // Clear existing detections
    allDetections.clear();
    dailyCounts.clear();
    locker.unlock();
    
    // Scan for detection files in the data directory
    if (!dataDirectory.isEmpty()) {
        scanDetectionFiles();
        // Also process any new RHD files
        processNewRhdFiles();
    }
    updateDisplay();
    // Update daily counts based on selected channels
    updateDailyCounts();
}

void SeizureAnalyzer::updateDisplay()
{
    // Simple flag check - if already updating, skip
    if (isUpdating) {
        return;
    }
    
    // Set flag immediately
    isUpdating = true;
    
    // Safety checks
    if (!dailyCountsTable || !latestDetectionsTable) {
        isUpdating = false;
        return;
    }
    
    // Sequential updates - one at a time
    updateSeizureCounts();
    updateLatestDetections();
    
    if (lastUpdateLabel) {
        lastUpdateLabel->setText("Last Update: " + QDateTime::currentDateTime().toString("hh:mm:ss"));
    }
    
    // Clear flag
    isUpdating = false;
}

bool SeizureAnalyzer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == channelList->viewport() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        QPoint pos = me->pos();
        QListWidgetItem *item = channelList->itemAt(pos);
        if (item) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            return true; // consume to keep popup open
                }
            }
    return QMainWindow::eventFilter(watched, event);
}

void SeizureAnalyzer::updateSeizureCounts()
{
    if (!totalSeizuresLabel || !todaySeizuresLabel || !monthlySeizuresLabel) {
        qWarning() << "updateSeizureCounts: Labels not initialized";
        return; // Safety check
    }
    
    try {
        QMutexLocker locker(&processingMutex);
        qDebug() << "updateSeizureCounts: Processing" << allDetections.size() << "detections";
        
        QList<SeizureRange> channelDetections;
        channelDetections.reserve(allDetections.size()); // Pre-allocate to avoid reallocations
        
        // Copy data while holding lock
        for (const SeizureRange &detection : allDetections) {
            if (!channelSelected(detection.channelIndex)) continue;
            if (!detection.start.isValid()) continue; // Skip invalid dates
            channelDetections.append(detection);
        }
        locker.unlock(); // Release lock before UI updates
        
        qDebug() << "updateSeizureCounts: Filtered to" << channelDetections.size() << "channel detections";
        
        int totalSeizures = channelDetections.size();
        int todaySeizures = 0;
        int monthlySeizures = 0;
        
        QDate today = QDate::currentDate();
        if (!today.isValid()) {
            qWarning() << "updateSeizureCounts: Invalid today date";
            return;
        }
        QString currentMonth = today.toString("yyyy-MM");
        
        for (const SeizureRange &detection : channelDetections) {
            if (!detection.start.isValid()) continue;
            
            QDate detDate = detection.start.date();
            if (!detDate.isValid()) continue;
            
            if (detDate == today) {
                todaySeizures++;
            }
            
            QString detectionMonth = detDate.toString("yyyy-MM");
            if (detectionMonth == currentMonth) {
                monthlySeizures++;
            }
        }
        
        totalSeizuresLabel->setText(QString("Total Seizures: %1").arg(totalSeizures));
        todaySeizuresLabel->setText(QString("Today: %1").arg(todaySeizures));
        monthlySeizuresLabel->setText(QString("This Month: %1").arg(monthlySeizures));
        
        qDebug() << "updateSeizureCounts: Completed - Total:" << totalSeizures << "Today:" << todaySeizures << "Month:" << monthlySeizures;
    } catch (const std::exception& e) {
        qWarning() << "Exception in updateSeizureCounts():" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception in updateSeizureCounts()";
    }
}

void SeizureAnalyzer::updateDailyCounts()
{
    if (!dailyCountsTable) return;
    
    // Block signals and disable updates
    dailyCountsTable->blockSignals(true);
    dailyCountsTable->setUpdatesEnabled(false);
    
    QMutexLocker locker(&processingMutex);
    // Copy data while holding lock
    QList<SeizureRange> detectionsCopy = allDetections;
    locker.unlock(); // Release lock before processing
    
    // Count detections by date for selected channels only - use local map
    QMap<QDate, int> localDailyCounts;
    for (const SeizureRange &detection : detectionsCopy) {
        if (!channelSelected(detection.channelIndex)) continue;
        if (!detection.start.isValid()) continue;
        
        QDate date = detection.start.date();
        if (date.isValid()) {
            localDailyCounts[date]++;
        }
    }
    
    // Re-acquire lock to update dailyCounts
    locker.relock();
    dailyCounts = localDailyCounts; // Update shared data while holding lock
    // Get sorted dates (newest first)
    QList<QDate> dates = dailyCounts.keys();
    std::sort(dates.begin(), dates.end(), std::greater<QDate>());
    
    // Clear table completely
    dailyCountsTable->setRowCount(0);
    dailyCountsTable->clearContents();
    
    // Set new row count
    dailyCountsTable->setRowCount(dates.size());
    
    // Populate rows with new data
    for (int i = 0; i < dates.size(); ++i) {
        QDate date = dates[i];
        int count = dailyCounts[date];
        
        dailyCountsTable->setItem(i, 0, new QTableWidgetItem(date.toString("yyyy-MM-dd")));
        dailyCountsTable->setItem(i, 1, new QTableWidgetItem(QString::number(count)));
    }
    
    // Restore selection if valid
    if (selectedDate.isValid()) {
        for (int i = 0; i < dates.size(); ++i) {
            if (dates[i] == selectedDate) {
                dailyCountsTable->selectRow(i);
                break;
            }
        }
    }
    
    // Re-enable
    dailyCountsTable->setUpdatesEnabled(true);
    dailyCountsTable->blockSignals(false);
}

void SeizureAnalyzer::onChannelItemChanged(QListWidgetItem *item)
{
    if (!item) return;
    int row = channelList->row(item);
    if (row < 0 || row >= 32) return;

    // Just update the selection set - no automatic update
    if (item->checkState() == Qt::Checked) {
        selectedChannels.insert(row);
    } else {
        selectedChannels.remove(row);
    }
    
    // User must click "Reload Data" button to update the display
}


void SeizureAnalyzer::onDailySelectionChanged()
{
    if (!dailyCountsTable) {
        selectedDate = QDate();
        updateLatestDetections();
        return;
    }
    
    auto sel = dailyCountsTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        selectedDate = QDate();
    } else {
        QModelIndex idx = sel.first();
        
        // Validate index is valid and row is within bounds
        // QModelIndex can become invalid if table is updated (setRowCount, clear, etc.)
        if (!idx.isValid()) {
            selectedDate = QDate();
        } else {
            int row = idx.row();
            int rowCount = dailyCountsTable->rowCount();
            
            // Validate row is within bounds before accessing
            if (row >= 0 && row < rowCount) {
                QTableWidgetItem *item = dailyCountsTable->item(row, 0);
                if (item) {
                    QString dateStr = item->text();
                    QDate parsedDate = QDate::fromString(dateStr, "yyyy-MM-dd");
                    if (parsedDate.isValid()) {
                        selectedDate = parsedDate;
                    } else {
                        selectedDate = QDate();
                    }
                } else {
                    selectedDate = QDate();
                }
            } else {
                // Row out of bounds - table was likely updated
                selectedDate = QDate();
            }
        }
    }
    updateLatestDetections();
}

void SeizureAnalyzer::updateLatestDetections()
{
    if (!latestDetectionsTable) return;
    
    // SIMPLE: Block signals and disable updates
    latestDetectionsTable->blockSignals(true);
    latestDetectionsTable->setUpdatesEnabled(false);
    
    try {
        // If no day selected, clear table
        if (!selectedDate.isValid()) {
            // Simple clear - let Qt handle it
            latestDetectionsTable->setRowCount(0);
            visibleDetections.clear();
            latestDetectionsTable->setUpdatesEnabled(true);
            latestDetectionsTable->blockSignals(false);
            return;
        }

        // Collect matching detections - SIMPLE loop
        QList<SeizureRange> channelDetections;
        {
            QMutexLocker locker(&processingMutex);
            // Copy data while holding lock
            for (const SeizureRange &detection : allDetections) {
                if (!channelSelected(detection.channelIndex)) continue;
                if (detection.start.date() != selectedDate) continue;
                channelDetections.append(detection);
            }
        } // Release lock before sorting/processing
        
        // Sort by end time (newest first)
        std::sort(channelDetections.begin(), channelDetections.end(), 
                  [](const SeizureRange &a, const SeizureRange &b) {
                      return a.end > b.end;
                  });
        
        // Limit to prevent crashes - 500 rows max
        const int MAX_ROWS = 500;
        if (channelDetections.size() > MAX_ROWS) {
            qWarning() << "Too many detections (" << channelDetections.size() << "), limiting to" << MAX_ROWS;
            channelDetections = channelDetections.mid(0, MAX_ROWS);
        }
        
        visibleDetections = channelDetections;
        int count = channelDetections.size();
        
        // Clear old widgets first - disconnect and delete immediately to prevent crashes
        // Using deleteLater() can cause Qt to access widgets during setRowCount() layout updates
        int oldCount = latestDetectionsTable->rowCount();
        for (int i = 0; i < oldCount; ++i) {
            QWidget *w = latestDetectionsTable->cellWidget(i, 5);
            if (w) {
                // Disconnect all signals first to prevent signal delivery to deleted object
                QObject::disconnect(w, nullptr, nullptr, nullptr);
                latestDetectionsTable->removeCellWidget(i, 5);
                delete w; // Delete immediately - safe because we disconnected signals
            }
        }
        
        // Set row count and clear
        latestDetectionsTable->setRowCount(count);
        latestDetectionsTable->clearContents();
        
        // Populate rows - limit to prevent crashes
        for (int i = 0; i < count && i < MAX_ROWS; ++i) {
            const SeizureRange &det = channelDetections[i];
            
            // Create items
            latestDetectionsTable->setItem(i, 0, new QTableWidgetItem(QString("A-%1").arg(det.channelIndex, 3, 10, QChar('0'))));
            latestDetectionsTable->setItem(i, 1, new QTableWidgetItem(det.start.toString("yyyy-MM-dd hh:mm:ss.zzz")));
            latestDetectionsTable->setItem(i, 2, new QTableWidgetItem(det.end.toString("yyyy-MM-dd hh:mm:ss.zzz")));
            latestDetectionsTable->setItem(i, 3, new QTableWidgetItem(QString::number(det.durationSec, 'f', 3)));
            latestDetectionsTable->setItem(i, 4, new QTableWidgetItem(QFileInfo(det.filePath).fileName()));
            
            // Create button with parent
            QPushButton *btn = new QPushButton("Open", latestDetectionsTable);
            btn->setProperty("detIndex", i);
            latestDetectionsTable->setCellWidget(i, 5, btn);
            // Use default DirectConnection for button clicks (same thread)
            // QueuedConnection can cause timing issues where button is deleted before signal is processed
            connect(btn, &QPushButton::clicked, this, &SeizureAnalyzer::onOpenDetectionClicked);
        }
    } catch (...) {
        qWarning() << "Exception in updateLatestDetections()";
    }

    // Re-enable
    latestDetectionsTable->setUpdatesEnabled(true);
    latestDetectionsTable->blockSignals(false);
}


bool SeizureAnalyzer::channelSelected(int channelIndex) const
{
    if (selectedChannels.isEmpty()) return false; // show nothing when none selected
    return selectedChannels.contains(channelIndex);
}

void SeizureAnalyzer::showChannelPopup()
{
    if (!channelPopup) return;
    QPoint globalPos = channelButton->mapToGlobal(QPoint(0, channelButton->height()));
    channelPopup->move(globalPos);
    channelPopup->show();
    channelPopup->raise();
    }

void SeizureAnalyzer::selectDataFolder()
{
    QString folder = QFileDialog::getExistingDirectory(
        this,
        "Select Data Folder",
        dataDirectory.isEmpty() ? QDir::homePath() : dataDirectory,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (!folder.isEmpty()) {
        // Remove old directory and subdirectories from watcher if exists
        if (!dataDirectory.isEmpty() && dataWatcher) {
            dataWatcher->removePath(dataDirectory);
            // Remove all subdirectories
            QDir oldDir(dataDirectory);
            QStringList subDirs = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& subDir : subDirs) {
                dataWatcher->removePath(oldDir.absoluteFilePath(subDir));
        }
        }
        
        // Clear all data when changing folder
        {
            QMutexLocker locker(&processingMutex);
            allDetections.clear();
            dailyCounts.clear();
            processedRhdFiles.clear();
        }
        visibleDetections.clear();
        selectedDate = QDate();
        
        dataDirectory = folder;
        
        // Update path display
        if (dataFolderPathEdit) {
            dataFolderPathEdit->setText(dataDirectory);
        }
        
        // Reset processing status label
        updateProcessingStatusLabel(0, 0, 0);

        // Add new directory and subdirectories to watcher
        if (dataWatcher) {
            dataWatcher->addPath(dataDirectory);
            QDir newDir(dataDirectory);
            QStringList subDirs = newDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString& subDir : subDirs) {
                QString subDirPath = newDir.absoluteFilePath(subDir);
                dataWatcher->addPath(subDirPath);
    }
        }
        
        // Scan for existing detection files
        scanDetectionFiles();
        updateDisplay();

        // Enable Start button now that folder is selected
        if (startButton) {
            startButton->setEnabled(true);
        }
        if (stopButton) {
            stopButton->setEnabled(false); // Stop button disabled when not processing
        }
        
        // Don't auto-start processing - user must click Start button
        // startContinuousProcessing(); // Removed - user controls via button
        
        qDebug() << "Selected data folder:" << dataDirectory;
    }
}

void SeizureAnalyzer::updateProcessingStatus(bool processing)
{
    // Processing status is tracked via isProcessing flag
    Q_UNUSED(processing);
}

void SeizureAnalyzer::updateProcessingStatusLabel(int processedCount, int processableFiles, int totalFiles)
{
    if (!processingStatusLabel) {
        return;
    }
    
    
    if (totalFiles == 0) {
        processingStatusLabel->setText("No RHD files found");
    } else if (totalFiles == 1) {
        // Only one file - it's the one being written, so nothing to process
        processingStatusLabel->setText("Processed 0 / 0 RHD files");
    } else {
        // Show processed count vs processable files (total - 1)
        processingStatusLabel->setText(QString("Processed %1 / %2 RHD files").arg(processedCount).arg(processableFiles).arg(totalFiles));
    }
}

void SeizureAnalyzer::startContinuousProcessing()
{
    if (!processingTimer) return;
    
    // Process existing files immediately
    processNewRhdFiles();
    
    // Start timer for continuous monitoring (every 1 second)
    if (!processingTimer->isActive()) {
        processingTimer->start(1000);
    }
}

void SeizureAnalyzer::stopContinuousProcessing()
{
    if (processingTimer && processingTimer->isActive()) {
        processingTimer->stop();
    }
    // Reset processing flag to allow restart
    QMutexLocker locker(&processingMutex);
    isProcessing = false;
}

void SeizureAnalyzer::onStartClicked()
{
    if (dataDirectory.isEmpty()) {
        QMessageBox::warning(this, "No Data Folder", 
                            "Please select a data folder first.");
        return;
    }
    
    if (processingTimer && !processingTimer->isActive()) {
        // Start processing
        startContinuousProcessing();
        startButton->setEnabled(false); // Disable start button
        stopButton->setEnabled(true);    // Enable stop button
        qDebug() << "Processing started";
            }
}

void SeizureAnalyzer::onStopClicked()
{
    if (processingTimer && processingTimer->isActive()) {
        // Stop processing (this already sets isProcessing = false with mutex protection)
        stopContinuousProcessing();
        startButton->setEnabled(true);  // Enable start button
        stopButton->setEnabled(false);   // Disable stop button
        qDebug() << "Processing stopped";
    }
}

void SeizureAnalyzer::onDataDirectoryChanged(const QString &path)
{
    // Directory changed, check for new RHD files and detection files
    qDebug() << "Data directory changed:" << path;
    
    // If a new subdirectory was created, add it to watcher
    QDir dir(path);
    if (dir.exists() && !dataWatcher->directories().contains(path)) {
        dataWatcher->addPath(path);
        qDebug() << "Added new subdirectory to watcher:" << path;
    }
    
    // If a directory changed, rescan all detection files recursively
    // This handles nested folder structures (e.g., data_260131_034802/data_260131_034802/)
    scanDetectionFiles();
    updateDisplay();
    
    // Process new RHD files
    processNewRhdFiles();
}

void SeizureAnalyzer::processNewRhdFiles()
{
    if (dataDirectory.isEmpty()) {
        updateProcessingStatusLabel(0, 0, 0);
        return;
    }
    
    QDir dataDir(dataDirectory);
    if (!dataDir.exists()) {
        updateProcessingStatusLabel(0, 0, 0);
        return;
    }
    
    // Find all .rhd files in the selected folder and its subdirectories
    QStringList filters;
    filters << "*.rhd";
    QFileInfoList rhdFiles;
    
    // Recursively search for .rhd files
    QDirIterator it(dataDirectory, filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        rhdFiles.append(QFileInfo(it.next()));
    }
    
    int totalFiles = rhdFiles.size();
    int processedCount;
    {
        QMutexLocker locker(&processingMutex);
        processedCount = processedRhdFiles.size();
    }
    
    // Only files that have a newer file after them are processable
    int processableFiles = totalFiles >= 2 ? totalFiles - 1 : 0;
    updateProcessingStatusLabel(processedCount, processableFiles, totalFiles);
    
    if (rhdFiles.isEmpty()) {
        return;
    }
    
    // Sort by modification time (oldest first)
    std::sort(rhdFiles.begin(), rhdFiles.end(), 
              [](const QFileInfo& a, const QFileInfo& b) {
                  return a.lastModified() < b.lastModified();
              });
    
    // Only process files if there's a newer file after them
    int filesToProcess = totalFiles >= 2 ? totalFiles - 1 : 0; // Only process if there's a newer file
    
    // Process files we haven't processed yet (only if there's a newer file after them)
    for (int i = 0; i < filesToProcess; ++i) {
        const QFileInfo& fileInfo = rhdFiles[i];
        QString filePath = fileInfo.absoluteFilePath();
    
        QMutexLocker locker(&processingMutex);
        // Skip if already processed
        if (processedRhdFiles.contains(filePath)) {
            continue;
        }
        
        // Check if result folder already exists (e.g., data_260131_034802/)
        QString baseName = fileInfo.baseName();
        QString parentPath = fileInfo.absolutePath();
        QString resultFolder = parentPath + "/" + baseName;
        if (QDir(resultFolder).exists()) {
            // Check if at least one channel file exists in the folder
            QDir dir(resultFolder);
            QStringList channelFiles = dir.entryList(QStringList() << "ch*_" + baseName + ".txt", QDir::Files);
            if (!channelFiles.isEmpty()) {
                // Already processed, mark as done
                processedRhdFiles.append(filePath);
                continue;
            }
        }
        
        // Skip if already processing
        if (isProcessing) {
            continue;
        }
        
        // Queue file for background processing (non-blocking)
        qDebug() << "Queueing RHD file for processing:" << filePath;
        isProcessing = true;
        locker.unlock(); // Release lock before invoking method
        updateProcessingStatus(true);
    
        // Process file in background thread (non-blocking)
        QMetaObject::invokeMethod(processorWorker, "processFile", Qt::QueuedConnection,
                                  Q_ARG(QString, filePath),
                                  Q_ARG(uint32_t, thresholdSpinBox->value()),
                                  Q_ARG(uint32_t, windowTimeoutSpinBox->value()),
                                  Q_ARG(uint32_t, transitionCountSpinBox->value()));
        
        // Don't remove from tracking yet - wait for processing to complete
        // The onFileProcessed slot will handle cleanup
    }
}

void SeizureAnalyzer::onFileProcessed(const QString& filePath, bool success, const QString& error)
{
    // This slot is called from the background thread via queued connection
    // It runs on the main thread, so it's safe to update UI
    
    // Check if this is a single file processing request
    if (!currentSingleFilePath.isEmpty() && filePath == currentSingleFilePath) {
        // This is single file processing
        onSingleFileProcessed(filePath, success, error);
        currentSingleFilePath.clear(); // Clear the tracking variable
        return;
    }
    
    // Otherwise, this is continuous processing
    {
        QMutexLocker locker(&processingMutex);
        isProcessing = false;
        if (success) {
            processedRhdFiles.append(filePath);
        }
    }
    updateProcessingStatus(false);
    
    if (success) {
        qDebug() << "Successfully processed:" << filePath;
        
        // Rescan detection files to pick up new results
        scanDetectionFiles();
        updateDisplay();
            } else {
        qWarning() << "Failed to process" << filePath << ":" << error;
    }
    
    // Update processing status label
    if (!dataDirectory.isEmpty()) {
        QDir dataDir(dataDirectory);
        if (dataDir.exists()) {
            QStringList filters;
            filters << "*.rhd";
            QDirIterator it(dataDirectory, filters, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            int totalFiles = 0;
            while (it.hasNext()) {
                it.next();
                totalFiles++;
    }
            int processedCount;
            {
                QMutexLocker locker(&processingMutex);
                processedCount = processedRhdFiles.size();
            }
            int processableFiles = totalFiles > 1 ? totalFiles - 1 : totalFiles;
            updateProcessingStatusLabel(processedCount, processableFiles, totalFiles);
        }
    }
}

void SeizureAnalyzer::onSingleFileBrowseClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "Select RHD File",
        dataDirectory.isEmpty() ? QDir::homePath() : dataDirectory,
        "RHD Files (*.rhd);;All Files (*)",
        nullptr,
        QFileDialog::DontResolveSymlinks
    );
    
    if (!filePath.isEmpty()) {
        singleFilePathEdit->setText(filePath);
        singleFileProcessButton->setEnabled(true);
        singleFileStatusLabel->setText("");
        singleFileStatusLabel->setStyleSheet("font-size: 11px; color: #666;");
    }
}

void SeizureAnalyzer::onSingleFileProcessClicked()
{
    QString filePath = singleFilePathEdit->text();
    if (filePath.isEmpty()) {
        QMessageBox::warning(this, "No File Selected", "Please select a file first.");
        return;
    }
    
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        QMessageBox::warning(this, "Invalid File", "The selected file does not exist.");
        return;
    }
    
    // Disable button and show processing status
    singleFileProcessButton->setEnabled(false);
    singleFileBrowseButton->setEnabled(false);
    singleFileStatusLabel->setText("Processing...");
    singleFileStatusLabel->setStyleSheet("font-size: 11px; color: white;");
    QApplication::processEvents(); // Update UI immediately
    
    // Store the file path to identify it in the completion handler
    currentSingleFilePath = filePath;
    
    // Process file in background thread
    QMetaObject::invokeMethod(processorWorker, "processFile", Qt::QueuedConnection,
                              Q_ARG(QString, filePath),
                              Q_ARG(uint32_t, thresholdSpinBox->value()),
                              Q_ARG(uint32_t, windowTimeoutSpinBox->value()),
                              Q_ARG(uint32_t, transitionCountSpinBox->value()));
}

void SeizureAnalyzer::onSingleFileProcessed(const QString& filePath, bool success, const QString& error)
{
    // Re-enable buttons
    singleFileProcessButton->setEnabled(true);
    singleFileBrowseButton->setEnabled(true);
    
    if (success) {
        singleFileStatusLabel->setText("Done");
        singleFileStatusLabel->setStyleSheet("font-size: 11px; color: #00aa00;");
        
        // Rescan detection files to pick up new results
        scanDetectionFiles();
        updateDisplay();
    } else {
        singleFileStatusLabel->setText("Error: " + error);
        singleFileStatusLabel->setStyleSheet("font-size: 11px; color: #cc0000;");
    }
}

void SeizureAnalyzer::scanDetectionFiles()
{
    if (dataDirectory.isEmpty()) {
        qDebug() << "scanDetectionFiles: dataDirectory is empty";
        return;
    }
    
    QDir dataDir(dataDirectory);
    if (!dataDir.exists()) {
        qDebug() << "scanDetectionFiles: dataDirectory does not exist:" << dataDirectory;
        return;
    }
    
    qDebug() << "scanDetectionFiles: Starting scan of" << dataDirectory;
    
    try {
        QMutexLocker locker(&processingMutex);
        // Clear existing data before rescanning to avoid duplicates
        allDetections.clear();
        dailyCounts.clear();
        locker.unlock();
        qDebug() << "scanDetectionFiles: Cleared existing data";
        
        // First, collect all file paths into a list (safer than iterating directly)
        QStringList filePaths;
        QDirIterator it(dataDirectory, QStringList() << "ch*_*.txt", QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        
        qDebug() << "scanDetectionFiles: Collecting file paths...";
        while (it.hasNext()) {
            QString filePath = it.next();
            if (!filePath.isEmpty() && filePath.length() <= 4096) {
                filePaths.append(filePath);
            }
        }
        
        qDebug() << "scanDetectionFiles: Found" << filePaths.size() << "detection files";
        
        // Now process each file
        int processedCount = 0;
        for (const QString& filePath : filePaths) {
            try {
                // Validate file path
                if (filePath.isEmpty() || filePath.length() > 4096) {
                    qWarning() << "Invalid file path length:" << filePath.length();
                    continue;
                }
                
                QFileInfo fileInfo(filePath);
                
                // Skip if file doesn't exist or isn't readable
                if (!fileInfo.exists() || !fileInfo.isReadable()) {
                    continue;
                }
                
                QString fileName = fileInfo.fileName();
                
                // Validate filename
                if (fileName.isEmpty() || fileName.length() > 256) {
                    qWarning() << "Invalid filename:" << fileName;
                    continue;
                }
                
                // Extract channel number from filename: ch0_data_260131_034802.txt -> 0
                QString baseName = fileInfo.baseName(); // ch0_data_260131_034802
                
                // Validate baseName
                if (baseName.isEmpty() || baseName.length() > 256) {
                    continue;
                }
                
                if (!baseName.startsWith("ch")) {
                    continue;
                }
                
                int underscorePos = baseName.indexOf('_');
                if (underscorePos < 2 || underscorePos >= baseName.length()) {
                    continue;
                }

                // Safely extract channel index
                int channelIndex = -1;
                if (underscorePos > 2) {
                    QString channelStr = baseName.mid(2, underscorePos - 2);
                    bool ok = false;
                    channelIndex = channelStr.toInt(&ok);
                    if (!ok || channelIndex < 0 || channelIndex >= 32) {
                        continue;
                    }
                } else {
                    continue;
                }

                parseChannelDetectionFile(filePath, channelIndex);
                processedCount++;
                
                // Log progress every 10 files
                if (processedCount % 10 == 0) {
                    qDebug() << "scanDetectionFiles: Processed" << processedCount << "files";
                }
            } catch (const std::exception& e) {
                qWarning() << "Exception processing file" << filePath << ":" << e.what();
            } catch (...) {
                qWarning() << "Unknown exception processing file:" << filePath;
            }
        }
        
        {
            QMutexLocker locker(&processingMutex);
            qDebug() << "scanDetectionFiles: Completed. Processed" << processedCount << "files, found" << allDetections.size() << "detections";
        }
    } catch (const std::exception& e) {
        qWarning() << "Exception in scanDetectionFiles():" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception in scanDetectionFiles()";
    }
}

void SeizureAnalyzer::parseChannelDetectionFile(const QString& filePath, int channelIndex)
{
    qDebug() << "parseChannelDetectionFile: Processing" << filePath << "channel" << channelIndex;
    
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open detection file:" << filePath;
        return;
    }
    
    try {
        QFileInfo fileInfo(filePath);
        
        // Extract RHD file name from the channel filename: ch0_data_260131_034802.txt -> data_260131_034802
        QString baseName = fileInfo.baseName(); // ch0_data_260131_034802
        
        // Validate baseName
        if (baseName.isEmpty() || baseName.length() > 256) {
            file.close();
            return;
        }
        
        int underscorePos = baseName.indexOf('_');
        if (underscorePos < 0 || underscorePos >= baseName.length() - 1) {
            file.close();
            return;
        }
        
        QString rhdFileName = baseName.mid(underscorePos + 1); // data_260131_034802
        
        // Validate rhdFileName
        if (rhdFileName.isEmpty() || rhdFileName.length() > 256) {
            file.close();
            return;
        }
        
        // Try to find the RHD file - check in the same directory, parent directory, and dataDirectory root
        QString rhdFilePath;
        QDir currentDir = fileInfo.dir();
        
        // Check same directory
        QString testPath = currentDir.absoluteFilePath(rhdFileName + ".rhd");
        if (QFile::exists(testPath)) {
            rhdFilePath = testPath;
        } else {
            // Check parent directory
            testPath = currentDir.absoluteFilePath("../" + rhdFileName + ".rhd");
            if (QFile::exists(testPath)) {
                rhdFilePath = testPath;
            } else {
                // Check dataDirectory root
                if (!dataDirectory.isEmpty()) {
                    testPath = dataDirectory + "/" + rhdFileName + ".rhd";
                    if (QFile::exists(testPath)) {
                        rhdFilePath = testPath;
                    } else {
                        // Use file modification time as fallback
                        rhdFilePath = filePath; // Will use fileInfo.lastModified() below
                    }
                } else {
                    // Use file modification time as fallback
                    rhdFilePath = filePath; // Will use fileInfo.lastModified() below
                }
            }
        }

        // Get RHD file modification time as base timestamp
        QFileInfo rhdFileInfo(rhdFilePath);
        QDateTime rhdBaseTime = rhdFileInfo.exists() ? rhdFileInfo.lastModified() : fileInfo.lastModified();
        
        if (!rhdBaseTime.isValid()) {
            qWarning() << "Invalid base time for file:" << filePath;
            file.close();
            return;
        }
        
        QTextStream in(&file);
        
        int lineNumber = 0;
        const int MAX_LINES = 1000000; // Safety limit
        
        while (!in.atEnd() && lineNumber < MAX_LINES) {
            lineNumber++;
            QString line = in.readLine();
            
            // Validate line
            if (line.length() > 1024) {
                qWarning() << "Line too long in file:" << filePath << "line:" << lineNumber;
                continue;
            }
            
            line = line.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            
            // Parse line: start, end or start,
            QStringList parts = line.split(",", Qt::SkipEmptyParts);
            if (parts.isEmpty() || parts.size() > 10) {
                continue;
            }
            
            // Validate first part
            if (parts[0].isEmpty() || parts[0].length() > 64) {
                continue;
            }
            
            bool ok;
            qint64 startMs = parts[0].trimmed().toLongLong(&ok);
            if (!ok || startMs < 0 || startMs > 86400000000LL) { // Max ~1000 days in ms
                continue;
            }

            // Convert relative timestamp (ms from file start) to absolute QDateTime
            QDateTime startTime = rhdBaseTime.addMSecs(startMs);
            
            if (!startTime.isValid()) {
                qWarning() << "Invalid start time calculated for file:" << filePath;
                continue;
            }
            
            SeizureRange range;
            range.start = startTime;
            range.channelIndex = channelIndex;
            // Always store the .txt detection file path (not the RHD file path)
            // This allows us to find the RHD file later when opening plots
            // Ensure filePath is valid and not too long
            if (filePath.length() > 4096) {
                qWarning() << "File path too long:" << filePath;
                continue;
            }
            range.filePath = filePath; // Always store the .txt file path
            
            // Check if there's an END timestamp
            if (parts.size() >= 2 && !parts[1].trimmed().isEmpty()) {
                qint64 endMs = parts[1].trimmed().toLongLong(&ok);
                if (ok) {
                    QDateTime endTime = rhdBaseTime.addMSecs(endMs);
                    if (endTime.isValid() && endTime >= startTime) {
                        range.end = endTime;
                        range.durationSec = startTime.msecsTo(endTime) / 1000.0;
                    } else {
                        // Invalid end time, use start time
                        range.end = startTime;
                        range.durationSec = 0.0;
                    }
                } else {
                    // Invalid end timestamp, treat as ongoing seizure
                    range.end = startTime; // Use start time as placeholder
                    range.durationSec = 0.0;
                }
            } else {
                // No END timestamp - ongoing seizure (recording ended during seizure)
                range.end = startTime; // Use start time as placeholder
                range.durationSec = 0.0;
            }
            
            // Append to shared data structure with mutex protection
            {
                QMutexLocker locker(&processingMutex);
                allDetections.append(range);
            }
        }
    } catch (...) {
        qWarning() << "Exception in parseChannelDetectionFile() for:" << filePath;
    }
    
    file.close();
}

// --- Waveform visualization classes ---

struct DetectionRegion {
    int startIndex;
    int endIndex;
    bool isHighlighted;
};

class WaveformCanvas : public QWidget {
    public:
        WaveformCanvas(const QVector<float>& data,
                       int windowMs,
                       qint64 windowStartTickMs,
                   const QList<DetectionRegion>& regions,
                       QWidget* parent = nullptr)
            : QWidget(parent)
            , data_(data)
            , windowMs_(windowMs)
            , windowStartTickMs_(windowStartTickMs)
        , regions_(regions) {}
    
    protected:
        void paintEvent(QPaintEvent *) override {
            QPainter p(this);
            p.fillRect(rect(), Qt::white);
            p.setRenderHint(QPainter::Antialiasing);
    
            if (data_.isEmpty()) return;
    
            const int w = width();
            const int h = height();
            const int n = data_.size();
    
        const int leftMargin = 80;  // Increased for Y-axis labels
        const int rightMargin = 80;  // Increased for timestamp labels
        const int topMargin = 10;
        const int bottomMargin = 30;
    
            QRect plotRect(leftMargin, topMargin,
                           w - leftMargin - rightMargin,
                           h - topMargin - bottomMargin);
    
            p.setPen(Qt::black);
        p.drawLine(plotRect.left(), plotRect.bottom(), plotRect.right(), plotRect.bottom());
        p.drawLine(plotRect.left(), plotRect.top(), plotRect.left(), plotRect.bottom());

            float minv = data_.first();
            float maxv = data_.first();
            for (float v : data_) { minv = std::min(minv, v); maxv = std::max(maxv, v); }
            if (maxv - minv < 1e-3f) { maxv = minv + 1.0f; }
    
            auto yscale = [&](float v) {
                return plotRect.bottom() - ((v - minv) / (maxv - minv)) * plotRect.height();
            };
    
        // Draw all detection regions
        for (const DetectionRegion& region : regions_) {
            if (region.startIndex >= 0 && region.endIndex > region.startIndex && region.endIndex < n) {
                float x0 = plotRect.left() + (float(region.startIndex) / float(n - 1)) * plotRect.width();
                float x1 = plotRect.left() + (float(region.endIndex)   / float(n - 1)) * plotRect.width();
                QRectF szRect(QPointF(x0, plotRect.top()), QPointF(x1, plotRect.bottom()));
                // Highlighted region is more opaque (120), others are more transparent (40)
                int alpha = region.isHighlighted ? 120 : 40;
                p.fillRect(szRect, QColor(255, 0, 0, alpha));
            }
        }

            // Downsample for performance - only draw points that will be visible
            // For a 1000px wide plot, we only need ~2000 points max
            int maxPoints = plotRect.width() * 2;  // 2x oversampling for smooth curves
            int step = std::max(1, n / maxPoints);
            
            QPainterPath path;
            path.moveTo(plotRect.left(), yscale(data_[0]));
            for (int i = step; i < n; i += step) {
                float x = plotRect.left() + (float(i) / float(n - 1)) * plotRect.width();
                path.lineTo(x, yscale(data_[i]));
            }
            // Always include the last point
            if ((n - 1) % step != 0) {
                float x = plotRect.left() + (float(n - 1) / float(n - 1)) * plotRect.width();
                path.lineTo(x, yscale(data_[n - 1]));
            }
            p.setPen(QPen(Qt::blue, 1.2));
            p.drawPath(path);
    
        // Global timestamp labels (left/mid/right)
            p.setPen(Qt::black);
        QFontMetrics fm(p.font());
            int yAxis = plotRect.bottom();
    
        auto drawTick = [&](double msOffset, int x) {
            qint64 absMs = windowStartTickMs_ + qint64(msOffset);
            QDateTime dt = QDateTime::fromMSecsSinceEpoch(absMs, QTimeZone::UTC);
            QString label = dt.toString("MM/dd hh:mm:ss.zzz");
            p.drawLine(x, yAxis, x, yAxis + 4);
            int tw = fm.horizontalAdvance(label);
            p.drawText(x - tw / 2, yAxis + 4 + fm.ascent(), label);
        };

        drawTick(0.0, plotRect.left());
        drawTick(windowMs_ / 2.0, plotRect.left() + plotRect.width() / 2);
        drawTick(windowMs_, plotRect.right());
    
            auto drawYTick = [&](float v) {
                int y = int(yscale(v));
            p.drawLine(plotRect.left() - 4, y, plotRect.left(), y);
                QString label = QString::number(v, 'f', 0) + " μV";
            int tw = fm.horizontalAdvance(label);
            p.drawText(plotRect.left() - 8 - tw, y + fm.ascent() / 2, label);
            };
            drawYTick(minv);
            drawYTick((minv + maxv) * 0.5f);
            drawYTick(maxv);
    
            p.save();
        p.translate(10, plotRect.top() + plotRect.height() / 2);
            p.rotate(-90);
        p.drawText(0, 0, "Amplitude (μV)");
            p.restore();
    
        p.setPen(Qt::gray);
            p.drawRect(plotRect.adjusted(0, 0, -1, -1));
        }
    
    private:
        QVector<float> data_;
        int windowMs_;
        qint64 windowStartTickMs_;
    QList<DetectionRegion> regions_;
};

class WaveformDialog : public QDialog {
    public:
        WaveformDialog(const QVector<float>& data,
            int windowMs,
            qint64 windowStartGlobalMs,
                   const QList<DetectionRegion>& regions,
            const QString& title,
            QWidget* parent = nullptr)
        : QDialog(parent) {
        setModal(false);
        setWindowTitle(title);
        resize(1000, 500);
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->addWidget(new WaveformCanvas(data, windowMs, windowStartGlobalMs,
                                             regions, this));
    }
};

// Load full RHD file for visualization
bool loadRhdFile(const QString& rhdPath,
    int channelIndex,
    QVector<float>& out,
    QVector<qint64>& outTsMs,
                 qint64& fileStartGlobalMsOut,
    QString& error)
{
    try {
        // Verify file exists and is readable before attempting to read
        QFileInfo fileCheck(rhdPath);
        if (!fileCheck.exists()) {
            error = QString("RHD file does not exist: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        if (!fileCheck.isReadable()) {
            error = QString("RHD file is not readable: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        // Convert path to std::string - simple conversion that worked before
        std::string filePathStr = rhdPath.toStdString();
        qDebug() << "loadRhdFile: Attempting to read RHD file:" << rhdPath;
        
        // Read RHD file
        RhdReader::RhdData rhdData;
        if (!RhdReader::readFile(filePathStr, rhdData)) {
            error = QString("Failed to read RHD file: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        qDebug() << "loadRhdFile: Read RHD file successfully. Channels:" << rhdData.num_channels << "Samples:" << rhdData.num_samples;

        if (channelIndex < 0 || channelIndex >= int(rhdData.num_channels)) {
            error = QString("Channel index %1 out of range (0-%2)").arg(channelIndex).arg(rhdData.num_channels - 1);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }

        // Check if amplifier_data is valid
        if (rhdData.amplifier_data.empty()) {
            error = QString("No amplifier data in RHD file: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        if (channelIndex >= int(rhdData.amplifier_data.size())) {
            error = QString("Channel index %1 exceeds amplifier_data size (%2)").arg(channelIndex).arg(rhdData.amplifier_data.size());
            qWarning() << "loadRhdFile:" << error;
            return false;
        }

        // Get file modification time as base timestamp
        QFileInfo fileInfo(rhdPath);
        if (!fileInfo.exists()) {
            error = QString("RHD file does not exist: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        QDateTime baseTime = fileInfo.lastModified();
        if (!baseTime.isValid()) {
            error = QString("Invalid file modification time for: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        qint64 baseTimeMs = baseTime.toMSecsSinceEpoch();
        fileStartGlobalMsOut = baseTimeMs;
        
        // Calculate sample period in milliseconds
        if (rhdData.sample_rate <= 0 || rhdData.sample_rate > 1000000) {
            error = QString("Invalid sample rate: %1 Hz").arg(rhdData.sample_rate);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        double samplePeriodMs = 1000.0 / rhdData.sample_rate;

        out.clear();
        outTsMs.clear();
        
        // Extract channel data and convert to microvolts
        const auto& channelData = rhdData.amplifier_data[channelIndex];
        
        // Check if channel data is valid
        if (channelData.empty()) {
            error = QString("Channel %1 has no data in RHD file: %2").arg(channelIndex).arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        if (channelData.size() != rhdData.num_samples) {
            qWarning() << "loadRhdFile: Channel data size mismatch. Expected:" << rhdData.num_samples << "Got:" << channelData.size();
        }
        
        // Convert ADC codes to microvolts (Intan ADC: 0.195 μV per LSB, offset at 32768)
        uint32_t numSamples = std::min(static_cast<uint32_t>(channelData.size()), rhdData.num_samples);
        qDebug() << "loadRhdFile: Processing" << numSamples << "samples for channel" << channelIndex;
        
        for (uint32_t i = 0; i < numSamples; ++i) {
            if (i >= channelData.size()) {
                qWarning() << "loadRhdFile: Index" << i << "out of bounds for channel data (size:" << channelData.size() << ")";
                break;
            }
            
            uint16_t adcCode = channelData[i];
            float uv = float(int(adcCode) - 32768) * 0.195f;
            
            qint64 sampleGlobalMs = baseTimeMs + qint64(i * samplePeriodMs);
            
            out.append(uv);
            outTsMs.append(sampleGlobalMs);
        }
        
        if (out.isEmpty()) {
            error = QString("No samples found in file: %1").arg(rhdPath);
            qWarning() << "loadRhdFile:" << error;
            return false;
        }
        
        qDebug() << "loadRhdFile: Successfully loaded" << out.size() << "samples";
        return true;
    } catch (const std::exception& e) {
        error = QString("Exception loading RHD file: %1").arg(e.what());
        qCritical() << "loadRhdFile:" << error;
        return false;
    } catch (...) {
        error = QString("Unknown exception loading RHD file: %1").arg(rhdPath);
        qCritical() << "loadRhdFile:" << error;
        return false;
    }
}

void SeizureAnalyzer::onOpenDetectionClicked()
{
    QObject *senderObj = sender();
    if (!senderObj) {
        qWarning() << "onOpenDetectionClicked: No sender object";
        return;
    }
    
    // Get the button's row in the table to ensure we're using current data
    // This is safer than storing an index that could become stale
    QWidget *btnWidget = qobject_cast<QWidget*>(senderObj);
    if (!btnWidget) {
        qWarning() << "onOpenDetectionClicked: Sender is not a widget";
        return;
    }
    
    // Find which row this button is in
    int row = -1;
    for (int i = 0; i < latestDetectionsTable->rowCount(); ++i) {
        QWidget *cellWidget = latestDetectionsTable->cellWidget(i, 5);
        if (cellWidget == btnWidget) {
            row = i;
            break;
        }
    }
    
    // Validate row and get detection data
    if (row < 0 || row >= visibleDetections.size()) {
        qWarning() << "onOpenDetectionClicked: Row" << row << "out of range (size:" << visibleDetections.size() << ")";
        return;
    }
    
    // Get detection data immediately while we know the index is valid
    // Make a copy to avoid issues if visibleDetections is updated
    SeizureRange det = visibleDetections[row];
    
    qDebug() << "onOpenDetectionClicked: Opening detection at row" << row;
    openRawForDetection(det);
}

void SeizureAnalyzer::openRawForDetection(const SeizureRange& detection)
{
    qDebug() << "openRawForDetection: Starting for channel" << detection.channelIndex << "file:" << detection.filePath;
    
    // Find the RHD file path from the detection file path
    QString rhdPath = detection.filePath;
    QString rhdFileName;
    
    // If detection.filePath is a channel file, find the corresponding RHD file
    QFileInfo detFileInfo(detection.filePath);
    if (detFileInfo.suffix() == "txt") {
        // Extract RHD filename from channel filename: ch0_data_260131_034802.txt -> data_260131_034802.rhd
        QString baseName = detFileInfo.baseName(); // ch0_data_260131_034802
        int underscorePos = baseName.indexOf('_');
        if (underscorePos >= 0) {
            rhdFileName = baseName.mid(underscorePos + 1); // data_260131_034802
            qDebug() << "openRawForDetection: Extracted RHD filename:" << rhdFileName;
            
            // Try to find the RHD file in multiple locations
            QDir currentDir = detFileInfo.dir();
            
            // Try parent directory first (most common case)
            QString testPath = currentDir.absoluteFilePath("../" + rhdFileName + ".rhd");
            // Canonicalize to resolve ../ and ./
            testPath = QFileInfo(testPath).canonicalFilePath();
            if (testPath.isEmpty()) {
                testPath = QFileInfo(currentDir.absoluteFilePath("../" + rhdFileName + ".rhd")).absoluteFilePath();
            }
            qDebug() << "openRawForDetection: Trying parent dir:" << testPath;
            if (QFile::exists(testPath)) {
                rhdPath = testPath;
                qDebug() << "openRawForDetection: Found RHD file in parent directory";
            } else {
                // Try same directory
                testPath = currentDir.absoluteFilePath(rhdFileName + ".rhd");
                // Canonicalize to resolve ../ and ./
                testPath = QFileInfo(testPath).canonicalFilePath();
                if (testPath.isEmpty()) {
                    testPath = QFileInfo(currentDir.absoluteFilePath(rhdFileName + ".rhd")).absoluteFilePath();
                }
                qDebug() << "openRawForDetection: Trying same dir:" << testPath;
                if (QFile::exists(testPath)) {
                    rhdPath = testPath;
                    qDebug() << "openRawForDetection: Found RHD file in same directory";
                } else if (!dataDirectory.isEmpty()) {
                    // Try data directory root
                    testPath = dataDirectory + "/" + rhdFileName + ".rhd";
                    qDebug() << "openRawForDetection: Trying data root:" << testPath;
                    if (QFile::exists(testPath)) {
                        rhdPath = testPath;
                        qDebug() << "openRawForDetection: Found RHD file in data root";
                    } else {
                        // Try recursive search in data directory
                        QDirIterator it(dataDirectory, QStringList() << rhdFileName + ".rhd", 
                                       QDir::Files, QDirIterator::Subdirectories);
                        if (it.hasNext()) {
                            rhdPath = it.next();
                            qDebug() << "openRawForDetection: Found RHD file via recursive search:" << rhdPath;
                        }
                    }
                }
            }
        } else {
            qWarning() << "openRawForDetection: Could not extract RHD filename from:" << baseName;
        }
    } else {
        // Already an RHD file - canonicalize the path to resolve ../ and ./
        QString canonicalPath = QFileInfo(rhdPath).canonicalFilePath();
        if (!canonicalPath.isEmpty()) {
            rhdPath = canonicalPath;
        } else {
            // If canonical path fails, try absolute path
            rhdPath = QFileInfo(rhdPath).absoluteFilePath();
        }
        rhdFileName = detFileInfo.baseName();
        qDebug() << "openRawForDetection: Detection file is already RHD file, canonicalized to:" << rhdPath;
    }
    
    // Canonicalize the final path to resolve any ../ or ./
    QString canonicalPath = QFileInfo(rhdPath).canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        rhdPath = canonicalPath;
    } else {
        // If canonical path fails, try absolute path
        rhdPath = QFileInfo(rhdPath).absoluteFilePath();
    }
    
    if (!QFile::exists(rhdPath)) {
        QString errorMsg = QString("RHD file not found:\n%1\n\nTried locations:\n- Parent directory\n- Same directory\n- Data root: %2").arg(rhdPath).arg(dataDirectory);
        qWarning() << "openRawForDetection:" << errorMsg;
        QMessageBox::warning(this, "RHD file missing", errorMsg);
        return;
    }
    
    qDebug() << "openRawForDetection: Using RHD file:" << rhdPath;

    // Load full file (1 minute of data)
    QVector<float> waveformData;
    QVector<qint64> waveformTsMs;
    qint64 fileStartGlobalMs = 0;
    QString error;

    qDebug() << "openRawForDetection: Loading RHD file...";
    qDebug() << "openRawForDetection: DEBUG - About to call loadRhdFile";
    bool loadResult = loadRhdFile(rhdPath, detection.channelIndex, waveformData, waveformTsMs,
                     fileStartGlobalMs, error);
    qDebug() << "openRawForDetection: DEBUG - loadRhdFile returned:" << loadResult;
    if (!loadResult) {
        qWarning() << "openRawForDetection: Failed to load RHD file:" << error;
        QMessageBox::warning(this, "Error loading waveform", error);
        return;
    }
    
    qDebug() << "openRawForDetection: DEBUG - loadRhdFile succeeded, checking data...";
    qDebug() << "openRawForDetection: DEBUG - waveformData.isEmpty():" << waveformData.isEmpty();
    qDebug() << "openRawForDetection: DEBUG - waveformTsMs.isEmpty():" << waveformTsMs.isEmpty();
    
    // Validate data before using it
    if (waveformData.isEmpty() || waveformTsMs.isEmpty()) {
        qWarning() << "openRawForDetection: Empty data after loading RHD file";
        QMessageBox::warning(this, "Error", "No data loaded from RHD file");
        return;
    }
    
    qDebug() << "openRawForDetection: DEBUG - waveformData.size():" << waveformData.size();
    qDebug() << "openRawForDetection: DEBUG - waveformTsMs.size():" << waveformTsMs.size();
    
    if (waveformData.size() != waveformTsMs.size()) {
        qWarning() << "openRawForDetection: Data size mismatch. waveformData:" << waveformData.size() << "waveformTsMs:" << waveformTsMs.size();
        QMessageBox::warning(this, "Error", "Data size mismatch after loading RHD file");
        return;
    }
    
    qDebug() << "openRawForDetection: Loaded" << waveformData.size() << "samples";
    qDebug() << "openRawForDetection: DEBUG - About to find file detections";

    // Find all detections from the same RHD file and channel
    // Copy allDetections while holding lock to avoid race conditions
    QList<SeizureRange> allDetectionsCopy;
    {
        QMutexLocker locker(&processingMutex);
        allDetectionsCopy = allDetections;
    }
    
    qDebug() << "openRawForDetection: DEBUG - allDetections.size():" << allDetectionsCopy.size();
    qDebug() << "openRawForDetection: DEBUG - rhdFileName:" << rhdFileName;
    QList<SeizureRange> fileDetections;
    qDebug() << "openRawForDetection: DEBUG - Starting loop through allDetections";
    for (const SeizureRange& det : allDetectionsCopy) {
        // Check if this detection is from the same RHD file
        QFileInfo detFileInfo2(det.filePath);
        QString detRhdFileName;
        if (detFileInfo2.suffix() == "txt") {
            QString baseName = detFileInfo2.baseName();
            int underscorePos = baseName.indexOf('_');
            if (underscorePos >= 0) {
                detRhdFileName = baseName.mid(underscorePos + 1);
            }
        } else {
            detRhdFileName = detFileInfo2.baseName();
        }
        
        // Match same RHD file and same channel
        if (detRhdFileName == rhdFileName && det.channelIndex == detection.channelIndex) {
            fileDetections.append(det);
        }
    }

    // Calculate window duration from data (1 minute = 60000 ms)
    qDebug() << "openRawForDetection: DEBUG - About to calculate windowMs";
    int windowMs = 60000; // Default to 1 minute
    if (!waveformTsMs.isEmpty() && waveformTsMs.size() > 1) {
        qDebug() << "openRawForDetection: DEBUG - Getting first timestamp";
        qint64 firstTs = waveformTsMs.first();
        qDebug() << "openRawForDetection: DEBUG - Getting last timestamp";
        qint64 lastTs = waveformTsMs.last();
        qDebug() << "openRawForDetection: DEBUG - firstTs:" << firstTs << "lastTs:" << lastTs;
        if (lastTs >= firstTs) {
            windowMs = static_cast<int>(lastTs - firstTs + 1);
            if (windowMs <= 0) windowMs = 60000; // Safety fallback
        }
    }
    qDebug() << "openRawForDetection: DEBUG - windowMs calculated:" << windowMs;

    // Convert detection times to sample indices
    qDebug() << "openRawForDetection: DEBUG - About to convert detection times";
    QList<DetectionRegion> regions;
    qDebug() << "openRawForDetection: DEBUG - Getting detection start time";
    qint64 detStartMs = detection.start.toMSecsSinceEpoch();
    qDebug() << "openRawForDetection: DEBUG - Getting detection end time";
    qint64 detEndMs = detection.end.toMSecsSinceEpoch();
    qDebug() << "openRawForDetection: DEBUG - detStartMs:" << detStartMs << "detEndMs:" << detEndMs;
    
    for (const SeizureRange& det : fileDetections) {
        qint64 detStartMs2 = det.start.toMSecsSinceEpoch();
        qint64 detEndMs2 = det.end.toMSecsSinceEpoch();
        
        // Find sample indices
        int startIdx = -1;
        int endIdx = -1;
        
        for (int i = 0; i < waveformTsMs.size(); ++i) {
            if (startIdx < 0 && waveformTsMs[i] >= detStartMs2) {
                startIdx = i;
            }
            if (waveformTsMs[i] <= detEndMs2) {
                endIdx = i;
            }
        }
        
        if (startIdx >= 0 && endIdx > startIdx) {
            DetectionRegion region;
            region.startIndex = startIdx;
            region.endIndex = endIdx;
            // Highlight the clicked detection
            region.isHighlighted = (detStartMs2 == detStartMs && detEndMs2 == detEndMs);
            regions.append(region);
        }
    }

    qDebug() << "openRawForDetection: DEBUG - About to create dialog";
    QString fileName = QFileInfo(rhdPath).fileName();
    qDebug() << "openRawForDetection: DEBUG - fileName:" << fileName;
    QString title = QString("Channel %1 - %2 (%3 detections)").arg(detection.channelIndex).arg(fileName).arg(regions.size());
    qDebug() << "openRawForDetection: DEBUG - title:" << title;

    qDebug() << "openRawForDetection: Creating waveform dialog with title:" << title;
    qDebug() << "openRawForDetection: DEBUG - About to call new WaveformDialog";
    qDebug() << "openRawForDetection: DEBUG - waveformData.size():" << waveformData.size() << "windowMs:" << windowMs << "fileStartGlobalMs:" << fileStartGlobalMs << "regions.size():" << regions.size();
    WaveformDialog *dlg = new WaveformDialog(waveformData, windowMs, fileStartGlobalMs,
                                             regions, title, this);
    qDebug() << "openRawForDetection: DEBUG - WaveformDialog created successfully";
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    qDebug() << "openRawForDetection: DEBUG - Set WA_DeleteOnClose";
    dlg->setWindowFlags(dlg->windowFlags() | Qt::Window);
    qDebug() << "openRawForDetection: DEBUG - Set window flags";
    dlg->show();
    qDebug() << "openRawForDetection: DEBUG - Called show()";
    dlg->raise();
    qDebug() << "openRawForDetection: DEBUG - Called raise()";
    dlg->activateWindow();
    qDebug() << "openRawForDetection: DEBUG - Called activateWindow()";
    qDebug() << "openRawForDetection: Waveform dialog shown";
}
