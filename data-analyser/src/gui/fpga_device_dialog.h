#ifndef FPGA_DEVICE_DIALOG_H
#define FPGA_DEVICE_DIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProgressBar>
#include <string>
#include <memory>
#include "../core/fpga_processor.h"

class FpgaDeviceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FpgaDeviceDialog(QWidget *parent = nullptr);
    ~FpgaDeviceDialog();

    // Get selected device serial (empty if cancelled or failed)
    std::string getSelectedDeviceSerial() const { return selected_serial_; }

    // Check if FPGA was successfully configured
    bool isFpgaConfigured() const { return fpga_processor_ != nullptr; }

    // Transfer ownership of the configured FpgaProcessor to the caller
    std::unique_ptr<FpgaProcessor> takeFpgaProcessor() { return std::move(fpga_processor_); }

private slots:
    void onRefreshClicked();
    void onSelectClicked();

private:
    void scanDevices();
    void updateDeviceList();
    std::unique_ptr<FpgaProcessor> configureFpga(const std::string& serial);

    QListWidget *deviceList;
    QPushButton *refreshButton;
    QPushButton *selectButton;
    QLabel *statusLabel;
    QProgressBar *progressBar;

    std::vector<std::string> device_serials_;
    std::string selected_serial_;
    std::unique_ptr<FpgaProcessor> fpga_processor_;

    static constexpr const char* BITFILE_PATH = "lib/detection.bit";
};

#endif // FPGA_DEVICE_DIALOG_H
