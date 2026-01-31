#include <QApplication>
#include <QMessageBox>
#include <QLoggingCategory>
#include "seizure_analyzer.h"
#include "gui/fpga_device_dialog.h"

// Static variable to store the default handler (captured before installing our handler)
static QtMessageHandler defaultMessageHandler = nullptr;

// Custom message handler to filter out disconnect warnings
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Filter out disconnect warnings
    if (msg.contains("QObject::disconnect") && msg.contains("wildcard call disconnects")) {
        return; // Suppress these messages
    }
    
    // Use default Qt message handler for everything else
    if (defaultMessageHandler) {
        defaultMessageHandler(type, context, msg);
    } else {
        // Fallback: output to stderr
        fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    // Set application name
    app.setApplicationName("HALO Reading Controller");
    app.setApplicationDisplayName("HALO Reading Controller");
    
    // Capture the default handler BEFORE installing our custom one
    defaultMessageHandler = qInstallMessageHandler(messageHandler);
    
    // Show device selection dialog first
    FpgaDeviceDialog deviceDialog;
    if (deviceDialog.exec() != QDialog::Accepted) {
        // User cancelled or no device selected
        return 0;
    }
    
    // Check if FPGA was successfully configured
    if (!deviceDialog.isFpgaConfigured()) {
        QMessageBox::critical(nullptr, "FPGA Configuration Failed",
                             "Failed to configure FPGA device.\n"
                             "The application will exit.");
        return 1;
    }
    
    // Now show the main window
    SeizureAnalyzer window;
    window.show();
    
    return app.exec();
}
