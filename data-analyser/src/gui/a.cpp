// NOTE: This is your file with **minimal fixes**:
// 1) Keep your existing WaveformCanvas (it already draws axes + global time labels).
// 2) Make loadRawWindow() actually output BOTH waveform UV values and their GLOBAL timestamps.
// 3) Use WaveformDialog (WaveformCanvas) instead of SimpleWaveformWidget.
// 4) Fix the broken "outUv" variables you accidentally pasted (compile error).

#include <QMouseEvent>
#include "seizure_analyzer.h"
#include <QApplication>
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
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QMessageBox>
#include <QDialog>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QTimeZone>
#include <QSplitter>

static const int kCfgThresholdValue    = 25000;
static const int kCfgWindowTimeout     = 200;
static const int kCfgTransitionCount   = 30;
static const int kCfgChannelsPerPacket = 32;

#include <QVector>
#include <chrono>
#include <algorithm>

SeizureAnalyzer::SeizureAnalyzer(QWidget *parent)
    : QMainWindow(parent)
    , centralWidget(nullptr)
    , fileWatcher(nullptr)
    , updateTimer(nullptr)
{
    QString appDir = QCoreApplication::applicationDirPath();

    if (appDir.contains("build/seizure_analyzer.app/Contents/MacOS")) {
        logsDirectory = QDir(appDir).absoluteFilePath("../../../../logs");
    } else {
        logsDirectory = QDir(appDir).absoluteFilePath("logs");
    }

    setupUI();
    scanLogFiles();

    fileWatcher = new QFileSystemWatcher(this);
    fileWatcher->addPath(logsDirectory);
    connect(fileWatcher, &QFileSystemWatcher::directoryChanged, this, &SeizureAnalyzer::onFileChanged);

    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &SeizureAnalyzer::updateDisplay);
    updateTimer->start(5000);

    setWindowTitle("Halo Seizure Viewer");
    setMinimumSize(800, 600);
}

SeizureAnalyzer::~SeizureAnalyzer()
{
}

void SeizureAnalyzer::setupUI()
{
    centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    mainLayout = new QVBoxLayout(centralWidget);

    buttonLayout = new QHBoxLayout();
    reloadButton = new QPushButton("Reload Data", this);
    connect(reloadButton, &QPushButton::clicked, this, &SeizureAnalyzer::reloadData);
    buttonLayout->addWidget(reloadButton);

    buttonLayout->addWidget(new QLabel("Channels:", this));
    channelButton = new QPushButton("Select Channels", this);
    channelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    buttonLayout->addWidget(channelButton);

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
    channelList->viewport()->installEventFilter(this);
    popupLayout->addWidget(channelList);
    connect(channelList, &QListWidget::itemChanged, this, &SeizureAnalyzer::onChannelItemChanged);
    connect(channelButton, &QPushButton::clicked, this, &SeizureAnalyzer::showChannelPopup);

    buttonLayout->addStretch();

    statsLayout = new QGridLayout();
    totalSeizuresLabel = new QLabel("Total Seizures: 0", this);
    todaySeizuresLabel = new QLabel("Today: 0", this);
    monthlySeizuresLabel = new QLabel("This Month: 0", this);
    lastUpdateLabel = new QLabel("Last Update: Never", this);

    totalSeizuresLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    todaySeizuresLabel->setStyleSheet("font-size: 14px;");
    monthlySeizuresLabel->setStyleSheet("font-size: 14px;");
    lastUpdateLabel->setStyleSheet("font-size: 12px; color: gray;");

    thresholdLabel = new QLabel(QString("THR = %1 (Max ADC = 65535)").arg(kCfgThresholdValue), this);
    windowTimeoutLabel = new QLabel(QString("WINDOW_TIMEOUT = %1 samples").arg(kCfgWindowTimeout), this);
    transitionCountLabel = new QLabel(QString("TRANSITION_COUNT = %1").arg(kCfgTransitionCount), this);
    channelsPerPacketLabel = new QLabel(QString("CHANNELS = %1").arg(kCfgChannelsPerPacket), this);
    QString cfgStyle = "font-size: 11px; color: gray;";
    thresholdLabel->setStyleSheet(cfgStyle);
    windowTimeoutLabel->setStyleSheet(cfgStyle);
    transitionCountLabel->setStyleSheet(cfgStyle);
    channelsPerPacketLabel->setStyleSheet(cfgStyle);

    statsLayout->addWidget(totalSeizuresLabel,      0, 0);
    statsLayout->addWidget(todaySeizuresLabel,      0, 1);
    statsLayout->addWidget(monthlySeizuresLabel,    0, 2);
    statsLayout->addWidget(lastUpdateLabel,         0, 3);
    statsLayout->addWidget(thresholdLabel,          1, 0);
    statsLayout->addWidget(windowTimeoutLabel,      1, 1);
    statsLayout->addWidget(transitionCountLabel,    1, 2);
    statsLayout->addWidget(channelsPerPacketLabel,  1, 3);

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

    QLabel *latestLabel = new QLabel("Detections:", this);
    latestLabel->setStyleSheet("font-weight: bold;");

    latestDetectionsTable = new QTableWidget(0, 6, this);
    latestDetectionsTable->setHorizontalHeaderLabels({"Channel", "Start", "End", "Duration (s)", "File", "RAW Waveform"});
    latestDetectionsTable->horizontalHeader()->setStretchLastSection(true);
    latestDetectionsTable->setAlternatingRowColors(true);
    latestDetectionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    latestDetectionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(latestDetectionsTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SeizureAnalyzer::onDetectionSelectionChanged);

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

    tablesSplitter = new QSplitter(Qt::Vertical, this);
    tablesSplitter->addWidget(dailyContainer);
    tablesSplitter->addWidget(detectionsContainer);
    tablesSplitter->setStretchFactor(0, 1);
    tablesSplitter->setStretchFactor(1, 2);

    mainLayout->addLayout(buttonLayout);
    mainLayout->addLayout(statsLayout);
    mainLayout->addWidget(tablesSplitter);
}

void SeizureAnalyzer::reloadData()
{
    scanLogFiles();
    updateDisplay();
}

void SeizureAnalyzer::updateDisplay()
{
    updateSeizureCounts();
    updateLatestDetections();
    updateDailyCounts();
    updateChannelData();
    lastUpdateLabel->setText("Last Update: " + QDateTime::currentDateTime().toString("hh:mm:ss"));
}

void SeizureAnalyzer::onFileChanged(const QString &path)
{
    Q_UNUSED(path)
    reloadData();
}

void SeizureAnalyzer::scanLogFiles()
{
    allDetections.clear();
    dailyCounts.clear();
    monthlyCounts.clear();

    QString currentDir = QDir::currentPath();
    QString absoluteLogsDir = QDir(logsDirectory).absolutePath();

    QDir logsDir(logsDirectory);
    if (!logsDir.exists()) {
        QString errorMsg = QString("Logs directory not found!\n"
                                  "Current directory: %1\n"
                                  "Looking for: %2\n"
                                  "Absolute path: %3")
                          .arg(currentDir)
                          .arg(logsDirectory)
                          .arg(absoluteLogsDir);
        QMessageBox::warning(this, "Warning", errorMsg);
        return;
    }

    QStringList dateDirs = logsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &dateDir : dateDirs) {
        QDir dayDir(logsDir.absoluteFilePath(dateDir));
        QStringList binFiles = dayDir.entryList(QStringList() << "*.bin", QDir::Files);

        for (const QString &binFile : binFiles) {
            QString filePath = dayDir.absoluteFilePath(binFile);
            parseDetectionBin(filePath);
        }
    }
}

bool SeizureAnalyzer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == channelList->viewport() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *me = static_cast<QMouseEvent*>(event);
        QPoint pos = me->pos();
        QListWidgetItem *item = channelList->itemAt(pos);
        if (item) {
            item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void SeizureAnalyzer::updateSeizureCounts()
{
    QList<SeizureRange> channelDetections;
    for (const SeizureRange &detection : allDetections) {
        if (!channelSelected(detection.channelIndex)) continue;
        channelDetections.append(detection);
    }

    int totalSeizures = channelDetections.size();
    int todaySeizures = 0;
    int monthlySeizures = 0;

    QDate today = QDate::currentDate();
    QString currentMonth = today.toString("yyyy-MM");

    for (const SeizureRange &detection : channelDetections) {
        if (detection.start.date() == today) {
            todaySeizures++;
        }

        QString detectionMonth = detection.start.date().toString("yyyy-MM");
        if (detectionMonth == currentMonth) {
            monthlySeizures++;
        }
    }

    totalSeizuresLabel->setText(QString("Total Seizures: %1").arg(totalSeizures));
    todaySeizuresLabel->setText(QString("Today: %1").arg(todaySeizures));
    monthlySeizuresLabel->setText(QString("This Month: %1").arg(monthlySeizures));
}

void SeizureAnalyzer::updateLatestDetections()
{
    if (!selectedDate.isValid()) {
        latestDetectionsTable->setRowCount(0);
        visibleDetections.clear();
        updateChannelData();
        return;
    }

    QList<SeizureRange> channelDetections;
    for (const SeizureRange &detection : allDetections) {
        if (!channelSelected(detection.channelIndex)) continue;
        if (selectedDate.isValid() && detection.start.date() != selectedDate) continue;
        channelDetections.append(detection);
    }
    std::sort(channelDetections.begin(), channelDetections.end(),
              [](const SeizureRange &a, const SeizureRange &b) { return a.end > b.end; });

    visibleDetections = channelDetections;

    int count = channelDetections.size();
    latestDetectionsTable->setRowCount(count);

    for (int i = 0; i < count; ++i) {
        const SeizureRange &detection = channelDetections[i];
        latestDetectionsTable->setItem(i, 0, new QTableWidgetItem(QString("A-%1").arg(detection.channelIndex, 3, 10, QChar('0'))));
        latestDetectionsTable->setItem(i, 1, new QTableWidgetItem(detection.start.toString("yyyy-MM-dd hh:mm:ss.zzz")));
        latestDetectionsTable->setItem(i, 2, new QTableWidgetItem(detection.end.toString("yyyy-MM-dd hh:mm:ss.zzz")));
        latestDetectionsTable->setItem(i, 3, new QTableWidgetItem(QString::number(detection.durationSec, 'f', 3)));
        latestDetectionsTable->setItem(i, 4, new QTableWidgetItem(QFileInfo(detection.filePath).fileName()));
        QWidget *btnContainer = new QWidget(latestDetectionsTable);
        QHBoxLayout *btnLayout = new QHBoxLayout(btnContainer);
        btnLayout->setContentsMargins(0, 0, 4, 0);
        btnLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QPushButton *btn = new QPushButton("Open", btnContainer);
        btn->setProperty("detIndex", i);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btnLayout->addWidget(btn);
        btnContainer->setLayout(btnLayout);
        connect(btn, &QPushButton::clicked, this, &SeizureAnalyzer::onOpenDetectionClicked);
        latestDetectionsTable->setCellWidget(i, 5, btnContainer);
    }
}

void SeizureAnalyzer::updateDailyCounts()
{
    dailyCounts.clear();
    for (const SeizureRange &detection : allDetections) {
        if (!channelSelected(detection.channelIndex)) continue;
        QDate date = detection.start.date();
        dailyCounts[date]++;
    }

    dailyCountsTable->setRowCount(dailyCounts.size());

    QList<QDate> dates = dailyCounts.keys();
    std::sort(dates.begin(), dates.end(), std::greater<QDate>());

    for (int i = 0; i < dates.size(); ++i) {
        QDate date = dates[i];
        int count = dailyCounts[date];

        dailyCountsTable->setItem(i, 0, new QTableWidgetItem(date.toString("yyyy-MM-dd")));
        dailyCountsTable->setItem(i, 1, new QTableWidgetItem(QString::number(count)));
    }

    if (selectedDate.isValid()) {
        for (int i = 0; i < dates.size(); ++i) {
            if (dates[i] == selectedDate) {
                dailyCountsTable->selectRow(i);
                return;
            }
        }
        selectedDate = QDate();
    }
}

void SeizureAnalyzer::onChannelItemChanged(QListWidgetItem *item)
{
    if (!item) return;
    int row = channelList->row(item);
    if (row < 0 || row >= 32) return;

    if (item->checkState() == Qt::Checked) {
        selectedChannels.insert(row);
    } else {
        selectedChannels.remove(row);
    }

    updateDisplay();
}

void SeizureAnalyzer::onDailySelectionChanged()
{
    auto sel = dailyCountsTable->selectionModel()->selectedRows();
    if (sel.isEmpty()) {
        selectedDate = QDate();
    } else {
        QModelIndex idx = sel.first();
        QString dateStr = dailyCountsTable->item(idx.row(), 0)->text();
        selectedDate = QDate::fromString(dateStr, "yyyy-MM-dd");
    }
    updateLatestDetections();
}

void SeizureAnalyzer::onDetectionSelectionChanged()
{
    // No-op
}

void SeizureAnalyzer::onOpenDetectionClicked()
{
    QObject *senderObj = sender();
    if (!senderObj) return;
    bool ok = false;
    int idx = senderObj->property("detIndex").toInt(&ok);
    if (!ok || idx < 0 || idx >= visibleDetections.size()) return;

    const SeizureRange &det = visibleDetections[idx];
    openRawForDetection(det);
}

void SeizureAnalyzer::updateChannelData()
{
    // No-op
}

void SeizureAnalyzer::parseDetectionBin(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open detection bin:" << filePath;
        return;
    }

    QFileInfo fi(filePath);
    QDate dirDate = QDate::fromString(fi.dir().dirName(), "yyyy-MM-dd");

    QDateTime fileBase;
    if (dirDate.isValid()) {
        QString fileName = fi.fileName();
        int hourStart = fileName.indexOf('_') + 1;
        int hourEnd = fileName.indexOf('_', hourStart);
        if (hourStart > 0 && hourEnd > hourStart) {
            bool ok = false;
            int hour = fileName.mid(hourStart, hourEnd - hourStart).toInt(&ok);
            if (ok && hour >= 0 && hour < 24) {
                fileBase = QDateTime(dirDate, QTime(hour, 0, 0), QTimeZone::UTC);
            } else {
                fileBase = QDateTime(dirDate, QTime(0,0), QTimeZone::UTC);
            }
        } else {
            fileBase = QDateTime(dirDate, QTime(0,0), QTimeZone::UTC);
        }
    } else {
        fileBase = fi.lastModified();
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    QMap<int, QDateTime> openStarts;

    while (!in.atEnd()) {
        quint32 word;
        in >> word;
        quint8 type = word & 0x3;
        quint8 ch = (word >> 2) & 0x1F;
        quint32 tsTicks = (word >> 7) & 0x1FFFFFF;

        if (ch == 0 || ch > 32) continue;
        QDateTime ts = fileBase.addMSecs(static_cast<qint64>(tsTicks));
        int channelIndex = ch - 1;

        if (type == 0b10) {
            openStarts[channelIndex] = ts;
        } else if (type == 0b01) {
            if (openStarts.contains(channelIndex)) {
                SeizureRange range;
                range.start = openStarts[channelIndex];
                range.end = ts;
                range.channelIndex = channelIndex;
                range.filePath = filePath;
                range.durationSec = std::max(0.0, range.start.msecsTo(range.end) / 1000.0);
                allDetections.append(range);
                openStarts.remove(channelIndex);
            }
        }
    }
}

bool SeizureAnalyzer::channelSelected(int channelIndex) const
{
    if (selectedChannels.isEmpty()) return false;
    return selectedChannels.contains(channelIndex);
}

// --- Waveform dialog and raw loader ---
// We keep WaveformCanvas (it already draws axes + global timestamp labels).

class WaveformCanvas : public QWidget {
public:
    WaveformCanvas(const QVector<float>& data,
                   int windowMs,
                   qint64 windowStartTickMs,
                   int seizureStartIndex,
                   int seizureEndIndex,
                   QWidget* parent = nullptr)
        : QWidget(parent)
        , data_(data)
        , windowMs_(windowMs)
        , windowStartTickMs_(windowStartTickMs)
        , szStart_(seizureStartIndex)
        , szEnd_(seizureEndIndex) {}

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        p.setRenderHint(QPainter::Antialiasing);

        if (data_.isEmpty()) return;

        const int w = width();
        const int h = height();
        const int n = data_.size();

        const int leftMargin = 40;
        const int rightMargin = 10;
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

        if (szStart_ >= 0 && szEnd_ > szStart_ && szEnd_ < n) {
            float x0 = plotRect.left() + (float(szStart_) / float(n - 1)) * plotRect.width();
            float x1 = plotRect.left() + (float(szEnd_)   / float(n - 1)) * plotRect.width();
            QRectF szRect(QPointF(x0, plotRect.top()), QPointF(x1, plotRect.bottom()));
            p.fillRect(szRect, QColor(255, 0, 0, 40));
        }

        QPainterPath path;
        path.moveTo(plotRect.left(), yscale(data_[0]));
        for (int i = 1; i < n; ++i) {
            float x = plotRect.left() + (float(i) / float(n - 1)) * plotRect.width();
            path.lineTo(x, yscale(data_[i]));
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
            p.drawText(plotRect.left() - 6 - tw, y + fm.ascent() / 2, label);
        };
        drawYTick(minv);
        drawYTick((minv + maxv) * 0.5f);
        drawYTick(maxv);

        p.save();
        p.translate(10, plotRect.top() + plotRect.height() / 2);
        p.rotate(-90);
        p.drawText(0, 0, "Amplitude (μV)");
        p.restore();

        if (szStart_ >= 0 && szEnd_ > szStart_ && szEnd_ < n) {
            double msStart = (double(szStart_) / double(n)) * windowMs_;
            double msEnd   = (double(szEnd_)   / double(n)) * windowMs_;
            qint64 absStartMs = qint64(windowStartTickMs_ + msStart);
            qint64 absEndMs = qint64(windowStartTickMs_ + msEnd);
            QDateTime dtStart = QDateTime::fromMSecsSinceEpoch(absStartMs, QTimeZone::UTC);
            QDateTime dtEnd = QDateTime::fromMSecsSinceEpoch(absEndMs, QTimeZone::UTC);
            QString bandLabel = QString("Seizure: %1 → %2")
                                    .arg(dtStart.toString("MM/dd hh:mm:ss.zzz"))
                                    .arg(dtEnd.toString("MM/dd hh:mm:ss.zzz"));
            int tw = fm.horizontalAdvance(bandLabel);
            p.setPen(Qt::darkRed);
            p.drawText(plotRect.left() + (plotRect.width() - tw) / 2,
                       plotRect.top() + fm.ascent() + 2,
                       bandLabel);
        }

        p.setPen(Qt::gray);
        p.drawRect(plotRect.adjusted(0, 0, -1, -1));
    }

private:
    QVector<float> data_;
    int windowMs_;
    qint64 windowStartTickMs_;
    int szStart_;
    int szEnd_;
};

class WaveformDialog : public QDialog {
public:
    WaveformDialog(const QVector<float>& data,
                   int windowMs,
                   qint64 windowStartGlobalMs,
                   int seizureStartIndex,
                   int seizureEndIndex,
                   const QString& title,
                   QWidget* parent = nullptr)
        : QDialog(parent) {
        setModal(false);
        setWindowTitle(title);
        resize(1000, 500);
        QVBoxLayout *layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->addWidget(new WaveformCanvas(data, windowMs, windowStartGlobalMs,
                                             seizureStartIndex, seizureEndIndex, this));
    }
};

struct SampleData {
    float value;
    qint64 globalTimestampMs;
};

bool loadRawWindow(const QString& rawPath,
                   int channelIndex,
                   const QDateTime& detStart,
                   const QDateTime& detEnd,
                   int windowMs,
                   QVector<float>& out,
                   QVector<qint64>& outTsMs,
                   qint64& windowStartGlobalMsOut,
                   int& seizureStartIndexOut,
                   int& seizureEndIndexOut,
                   QString& error)
{
    QFileInfo fileInfo(rawPath);
    QDate fileDate = QDate::fromString(fileInfo.dir().dirName(), "yyyy-MM-dd");
    if (!fileDate.isValid()) {
        error = QString("Invalid date in path: %1").arg(fileInfo.dir().dirName());
        return false;
    }

    QString fileName = fileInfo.fileName();
    int hourStart = fileName.indexOf('_') + 1;
    int hourEnd = fileName.indexOf('_', hourStart);
    if (hourStart <= 0 || hourEnd <= hourStart) {
        error = QString("Could not extract hour from filename: %1").arg(fileName);
        return false;
    }
    bool ok = false;
    int hour = fileName.mid(hourStart, hourEnd - hourStart).toInt(&ok);
    if (!ok || hour < 0 || hour > 23) {
        error = QString("Invalid hour in filename: %1").arg(fileName);
        return false;
    }

    QDateTime baseTime = QDateTime(fileDate, QTime(hour, 0, 0), QTimeZone::UTC);
    qint64 baseTimeMs = baseTime.toMSecsSinceEpoch();

    out.clear();
    outTsMs.clear();
    seizureStartIndexOut = -1;
    seizureEndIndexOut = -1;

    QFile f(rawPath);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QString("Cannot open raw file: %1").arg(rawPath);
        return false;
    }
    QDataStream in(&f);
    in.setByteOrder(QDataStream::LittleEndian);

    char magic[8];
    if (in.readRawData(magic, 8) != 8 || memcmp(magic, "HALOLOG", 7) != 0) {
        error = "Bad magic in raw file";
        return false;
    }
    quint16 version, reserved;
    quint32 channelCount, samplesPerRecord, sampleBits, tsBits;
    in >> version >> reserved >> channelCount >> samplesPerRecord >> sampleBits >> tsBits;
    if (channelIndex < 0 || channelIndex >= int(channelCount)) {
        error = "Channel out of range in raw file";
        return false;
    }

    qint64 detCenterMs = detStart.msecsTo(detEnd) / 2 + detStart.toMSecsSinceEpoch();
    qint64 detStartMs = detStart.toMSecsSinceEpoch();
    qint64 detEndMs = detEnd.toMSecsSinceEpoch();
    qint64 halfWindowMs = windowMs / 2;
    qint64 windowStartGlobalMs = detCenterMs - halfWindowMs;
    qint64 windowEndGlobalMs = detCenterMs + halfWindowMs;
    windowStartGlobalMsOut = windowStartGlobalMs;

    const quint64 recordSize = 8 + 4 + 4 + 512 + (channelCount * samplesPerRecord * 2);
    quint64 headerSize = 8 + 2 + 2 + 4 + 4 + 4 + 4;
    quint64 fileSize = f.size();
    quint64 numRecords = (fileSize - headerSize) / recordSize;

    QVector<SampleData> allSamples;

    qint64 firstRecordMs = -1;
    qint64 lastRecordMs = -1;

    f.seek(headerSize);
    for (quint64 rec = 0; rec < numRecords; ++rec) {
        quint64 unixTimeNs; quint32 seq; quint32 payload;
        in >> unixTimeNs >> seq >> payload;
        if (in.status() != QDataStream::Ok) break;

        Q_UNUSED(unixTimeNs);

        QVector<quint32> ts(samplesPerRecord);
        for (quint32 i = 0; i < samplesPerRecord; ++i) in >> ts[i];
        if (in.status() != QDataStream::Ok) break;

        QVector<quint16> wave(channelCount * samplesPerRecord);
        for (quint32 i = 0; i < channelCount * samplesPerRecord; ++i) in >> wave[i];
        if (in.status() != QDataStream::Ok) break;

        for (quint32 i = 0; i < samplesPerRecord; ++i) {
            qint64 sampleGlobalMs = baseTimeMs + qint64(ts[i]);

            if (firstRecordMs < 0) firstRecordMs = sampleGlobalMs;
            lastRecordMs = sampleGlobalMs;

            if (sampleGlobalMs < windowStartGlobalMs || sampleGlobalMs > windowEndGlobalMs) {
                continue;
            }

            int code = int(wave[channelIndex * samplesPerRecord + i]);
            float uv = float(code - 32768) * 0.195f;

            SampleData sample;
            sample.value = uv;
            sample.globalTimestampMs = sampleGlobalMs;
            allSamples.append(sample);
        }
    }

    if (allSamples.isEmpty()) {
        QTimeZone utc = QTimeZone::UTC;
        QString firstRecStr = (firstRecordMs >= 0) ?
            QDateTime::fromMSecsSinceEpoch(firstRecordMs, utc).toString("yyyy-MM-dd hh:mm:ss.zzz") : "none";
        QString lastRecStr = (lastRecordMs >= 0) ?
            QDateTime::fromMSecsSinceEpoch(lastRecordMs, utc).toString("yyyy-MM-dd hh:mm:ss.zzz") : "none";
        error = QString("No samples found in window\n\nDetection: %1 to %2\nWindow: %3 to %4\nFile records: %5 to %6\n\nRecords in file: %7")
                .arg(QDateTime::fromMSecsSinceEpoch(detStartMs, utc).toString("yyyy-MM-dd hh:mm:ss.zzz"))
                .arg(QDateTime::fromMSecsSinceEpoch(detEndMs, utc).toString("yyyy-MM-dd hh:mm:ss.zzz"))
                .arg(QDateTime::fromMSecsSinceEpoch(windowStartGlobalMs, utc).toString("yyyy-MM-dd hh:mm:ss.zzz"))
                .arg(QDateTime::fromMSecsSinceEpoch(windowEndGlobalMs, utc).toString("yyyy-MM-dd hh:mm:ss.zzz"))
                .arg(firstRecStr)
                .arg(lastRecStr)
                .arg(numRecords);
        return false;
    }

    std::sort(allSamples.begin(), allSamples.end(),
              [](const SampleData& a, const SampleData& b) { return a.globalTimestampMs < b.globalTimestampMs; });

    // Compute seizure indices in the FINAL sorted window
    seizureStartIndexOut = -1;
    seizureEndIndexOut = -1;
    for (int i = 0; i < allSamples.size(); ++i) {
        if (allSamples[i].globalTimestampMs >= detStartMs && seizureStartIndexOut < 0) {
            seizureStartIndexOut = i;
        }
        if (allSamples[i].globalTimestampMs <= detEndMs) {
            seizureEndIndexOut = i;
        }
    }

    // Output both arrays
    out.reserve(allSamples.size());
    outTsMs.reserve(allSamples.size());
    for (const SampleData& s : allSamples) {
        out.append(s.value);
        outTsMs.append(s.globalTimestampMs);
    }

    return true;
}

void SeizureAnalyzer::openRawForDetection(const SeizureRange& detection)
{
    QString dateStr = detection.start.date().toString("yyyy-MM-dd");
    QString hourStr = detection.start.time().toString("HH");
    QDir dayDir(QDir(logsDirectory).absoluteFilePath(dateStr));
    QString rawPath = dayDir.absoluteFilePath(QString("hour_%1_raw.log").arg(hourStr));

    if (!QFile::exists(rawPath)) {
        QMessageBox::warning(this, "Raw file missing", QString("Raw log not found:\n%1").arg(rawPath));
        return;
    }

    const int windowMs = 60000;

    QVector<float> waveformData;
    QVector<qint64> waveformTsMs; // NEW: global timestamps per sample (kept for future)
    qint64 windowStartGlobalMs = 0;
    int seizureStartIndex = -1;
    int seizureEndIndex = -1;
    QString error;

    if (!loadRawWindow(rawPath, detection.channelIndex, detection.start, detection.end,
                       windowMs, waveformData, waveformTsMs,
                       windowStartGlobalMs, seizureStartIndex, seizureEndIndex, error)) {
        QMessageBox::warning(this, "Error loading waveform", error);
        return;
    }

    // IMPORTANT: we no longer pad/shift; padding would desync indices/time.
    QString fileName = QFileInfo(rawPath).fileName();
    QString title = QString("%1 (%2)").arg(dateStr, fileName);

    // SHOW AXES + GLOBAL TIME (WaveformCanvas already does this using windowStartGlobalMs)
    WaveformDialog *dlg = new WaveformDialog(waveformData, windowMs, windowStartGlobalMs,
                                             seizureStartIndex, seizureEndIndex, title, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void SeizureAnalyzer::showChannelPopup()
{
    if (!channelPopup) return;
    QPoint globalPos = channelButton->mapToGlobal(QPoint(0, channelButton->height()));
    channelPopup->move(globalPos);
    channelPopup->show();
    channelPopup->raise();
}
