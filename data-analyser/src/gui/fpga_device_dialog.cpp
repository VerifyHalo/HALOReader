#include "fpga_device_dialog.h"
#include "../core/ok_frontpanel.h"
#include <QApplication>
#include <QFileInfo>
#include <QDir>
#include <filesystem>
#include <iostream>

FpgaDeviceDialog::FpgaDeviceDialog(QWidget *parent)
    : QDialog(parent)
    , deviceList(nullptr)
    , refreshButton(nullptr)
    , selectButton(nullptr)
    , statusLabel(nullptr)
    , progressBar(nullptr)
    , fpga_configured_(false)
{
    setWindowTitle("Select FPGA Device");
    setMinimumSize(500, 300);
    setModal(true);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(5);
    mainLayout->setContentsMargins(20, 15, 20, 15);
    
    // Title
    QLabel *titleLabel = new QLabel("Select FPGA Device", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    mainLayout->addWidget(titleLabel);
    
    // Status label - closer to title, default color
    statusLabel = new QLabel("Scanning for devices...", this);
    statusLabel->setStyleSheet("font-size: 12px;");
    mainLayout->addSpacing(5); // Small gap after title
    mainLayout->addWidget(statusLabel);
    
    // Device list
    QLabel *listLabel = new QLabel("Available Devices:", this);
    listLabel->setStyleSheet("font-weight: bold;");
    mainLayout->addSpacing(5); // Small gap before list
    mainLayout->addWidget(listLabel);
    
    deviceList = new QListWidget(this);
    deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    deviceList->setMaximumHeight(150);
    mainLayout->addWidget(deviceList);
    
    // Progress bar (hidden initially)
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 0); // Indeterminate
    progressBar->setVisible(false);
    mainLayout->addWidget(progressBar);
    
    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    refreshButton = new QPushButton("Refresh", this);
    connect(refreshButton, &QPushButton::clicked, this, &FpgaDeviceDialog::onRefreshClicked);
    buttonLayout->addWidget(refreshButton);
    
    selectButton = new QPushButton("Configure", this);
    selectButton->setDefault(true);
    selectButton->setEnabled(false);
    connect(selectButton, &QPushButton::clicked, this, &FpgaDeviceDialog::onSelectClicked);
    buttonLayout->addWidget(selectButton);
    

    mainLayout->addLayout(buttonLayout);
    
    // Connect selection change
    connect(deviceList, &QListWidget::itemSelectionChanged, this, [this]() {
        selectButton->setEnabled(deviceList->currentRow() >= 0);
    });
    
    // Initial scan
    scanDevices();
}

FpgaDeviceDialog::~FpgaDeviceDialog()
{
}

void FpgaDeviceDialog::scanDevices()
{
    device_serials_.clear();
    deviceList->clear();
    selectButton->setEnabled(false);
    
    statusLabel->setText("Scanning for devices...");
    statusLabel->setStyleSheet("font-size: 12px;");
    
    OkFrontPanel frontPanel;
    int count = frontPanel.getDeviceCount();
    
    if (count <= 0) {
        statusLabel->setText("No Opal Kelly devices found. Please connect a device and click Refresh.");
        statusLabel->setStyleSheet("font-size: 12px;");
        return;
    }
    
    // Get all device serials
    for (int i = 0; i < count; ++i) {
        std::string serial = frontPanel.getDeviceListSerial(i);
        device_serials_.push_back(serial);
        
        // Try to open to verify it's accessible
        OkFrontPanel testPanel;
        int rc = testPanel.openBySerial(serial);
        QString status = (rc == OkFrontPanel::NoError) ? " (Ready)" : " (Error: " + QString::fromStdString(OkFrontPanel::getErrorString(rc)) + ")";
        
        QString itemText = QString("Device %1: %2%3").arg(i).arg(QString::fromStdString(serial)).arg(status);
        deviceList->addItem(itemText);
    }
    
    if (device_serials_.empty()) {
        statusLabel->setText("No devices found.");
        statusLabel->setStyleSheet("font-size: 12px;");
    } else {
        statusLabel->setText(QString("Found %1 device(s). Select one to configure.").arg(count));
        statusLabel->setStyleSheet("font-size: 12px;");
    }
}

void FpgaDeviceDialog::onRefreshClicked()
{
    scanDevices();
}

void FpgaDeviceDialog::onSelectClicked()
{
    int currentRow = deviceList->currentRow();
    if (currentRow < 0 || currentRow >= static_cast<int>(device_serials_.size())) {
        QMessageBox::warning(this, "No Selection", "Please select a device from the list.");
        return;
    }
    
    selected_serial_ = device_serials_[currentRow];
    
    // Show progress
    progressBar->setVisible(true);
    statusLabel->setText("Configuring FPGA...");
    statusLabel->setStyleSheet("font-size: 12px;");
    selectButton->setEnabled(false);
    refreshButton->setEnabled(false);
    QApplication::processEvents();
    
    // Configure FPGA
    fpga_configured_ = configureFpga(selected_serial_);
    
    progressBar->setVisible(false);
    
    if (fpga_configured_) {
        statusLabel->setText("FPGA configured successfully!");
        statusLabel->setStyleSheet("font-size: 12px;");
        accept(); // Close dialog with success
    } else {
        statusLabel->setText("Failed to configure FPGA. Please try again.");
        statusLabel->setStyleSheet("font-size: 12px;");
        selectButton->setEnabled(true);
        refreshButton->setEnabled(true);
        
        QMessageBox::critical(this, "Configuration Failed", 
                             "Failed to configure FPGA with bitstream.\n\n"
                             "Please ensure:\n"
                             "- The device is properly connected\n"
                             "- The bitstream file exists at: lib/detection.bit\n"
                             "- The device is not in use by another application");
    }
}

bool FpgaDeviceDialog::configureFpga(const std::string& serial)
{
    // Find bitfile
    std::string bitfile_path;
    const char* paths[] = {
        "lib/detection.bit",
        "../lib/detection.bit",
        "../../lib/detection.bit",
        "/Users/antonmelnychuk/workspace/pipeline/data-analyser/lib/detection.bit"
    };
    
    for (const char* path : paths) {
        if (std::filesystem::exists(path)) {
            bitfile_path = std::filesystem::absolute(path).string();
            break;
        }
    }
    
    if (bitfile_path.empty()) {
        std::cerr << "[FPGA] Bitfile not found" << std::endl;
        return false;
    }
    
    // Open device
    OkFrontPanel device;
    int rc = device.openBySerial(serial);
    if (rc != OkFrontPanel::NoError) {
        std::cerr << "[FPGA] Failed to open device: " << OkFrontPanel::getErrorString(rc) << std::endl;
        return false;
    }
    
    // Configure FPGA
    rc = device.configureFPGA(bitfile_path);
    if (rc != OkFrontPanel::NoError) {
        std::cerr << "[FPGA] ConfigureFPGA failed: " << OkFrontPanel::getErrorString(rc) << std::endl;
        return false;
    }
    
    std::cerr << "[FPGA] Successfully configured device: " << serial << std::endl;
    return true;
}
