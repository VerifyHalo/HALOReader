#include <QApplication>
#include <QMessageBox>
#include <QLoggingCategory>
#include <QIcon>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <cstdlib>
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif
#include "seizure_analyzer.h"
#include "gui/fpga_device_dialog.h"

// Static variable to store the default handler (captured before installing our handler)
static QtMessageHandler defaultMessageHandler = nullptr;
static QFile* logFile = nullptr;
static QTextStream* logStream = nullptr;

// Signal handler for crashes
void crashHandler(int signal) {
    // Force output to console immediately
    fflush(stderr);
    fflush(stdout);
    
    if (logStream) {
        *logStream << "\n=== CRASH DETECTED ===" << Qt::endl;
        *logStream << "Signal: " << signal << Qt::endl;
        *logStream << "Time: " << QDateTime::currentDateTime().toString() << Qt::endl;
        logStream->flush();
    }
    
    fprintf(stderr, "\n\n========================================\n");
    fprintf(stderr, "=== CRASH DETECTED ===\n");
    fprintf(stderr, "Signal: %d\n", signal);
    fprintf(stderr, "Time: %s\n", QDateTime::currentDateTime().toString().toLocal8Bit().constData());
    fprintf(stderr, "========================================\n\n");
    fflush(stderr);
    
    // Give time for output to be written
    #ifdef _WIN32
    Sleep(2000);
    #endif
    
    exit(1);
}

// Custom message handler to filter out disconnect warnings and log to file
void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Filter out disconnect warnings
    if (msg.contains("QObject::disconnect") && msg.contains("wildcard call disconnects")) {
        return; // Suppress these messages
    }
    
    // Log to file if available
    if (logStream) {
        QString typeStr;
        switch (type) {
            case QtDebugMsg: typeStr = "DEBUG"; break;
            case QtWarningMsg: typeStr = "WARNING"; break;
            case QtCriticalMsg: typeStr = "CRITICAL"; break;
            case QtFatalMsg: typeStr = "FATAL"; break;
            case QtInfoMsg: typeStr = "INFO"; break;
        }
        *logStream << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << "] "
                   << typeStr << ": " << msg << Qt::endl;
        logStream->flush();
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
    // Allocate console for Windows GUI apps FIRST, before anything else
    #ifdef _WIN32
    bool consoleAttached = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        consoleAttached = true;
    } else if (AllocConsole()) {
        consoleAttached = true;
    }
    if (consoleAttached) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        freopen("CONIN$", "r", stdin);
        fprintf(stderr, "\n========================================\n");
        fprintf(stderr, "HALOReader Starting...\n");
        fprintf(stderr, "========================================\n\n");
        fflush(stderr);
    }
    #endif
    
    // Install signal handlers for crash detection
    signal(SIGSEGV, crashHandler);  // Segmentation fault
    signal(SIGABRT, crashHandler);  // Abort
    signal(SIGFPE, crashHandler);    // Floating point exception
    signal(SIGILL, crashHandler);   // Illegal instruction
    
    fprintf(stderr, "Initializing QApplication...\n");
    fflush(stderr);
    
    QApplication app(argc, argv);
    
    // Set application name and icon
    app.setApplicationName("HALO Reading Controller");
    app.setApplicationDisplayName("HALO Reading Controller");
    app.setWindowIcon(QIcon(":/app_icon.png"));
    
    fprintf(stderr, "QApplication initialized.\n");
    fflush(stderr);
    
    // Setup logging to file
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(logDir);
    QString logFilePath = logDir + "/haloreader_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".log";
    logFile = new QFile(logFilePath);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Text)) {
        logStream = new QTextStream(logFile);
        *logStream << "=== HALOReader Log Started ===" << Qt::endl;
        *logStream << "Time: " << QDateTime::currentDateTime().toString() << Qt::endl;
        *logStream << "Log file: " << logFilePath << Qt::endl;
        logStream->flush();
        fprintf(stderr, "\n========================================\n");
        fprintf(stderr, "HALOReader Debug Console\n");
        fprintf(stderr, "========================================\n");
        fprintf(stderr, "Logging to: %s\n", logFilePath.toLocal8Bit().constData());
        fprintf(stderr, "========================================\n\n");
        fflush(stderr);
    }
    
    // Capture the default handler BEFORE installing our custom one
    defaultMessageHandler = qInstallMessageHandler(messageHandler);
    
    fprintf(stderr, "Showing FPGA device selection dialog...\n");
    fflush(stderr);
    
    // Show device selection dialog first
    FpgaDeviceDialog deviceDialog;
    int dialogResult = deviceDialog.exec();
    
    if (dialogResult != QDialog::Accepted) {
        // User cancelled or no device selected
        fprintf(stderr, "Device dialog cancelled or closed. Exiting.\n");
        fflush(stderr);
        return 0;
    }
    
    fprintf(stderr, "Device dialog accepted. Checking FPGA configuration...\n");
    fflush(stderr);
    
    // Check if FPGA was successfully configured
    if (!deviceDialog.isFpgaConfigured()) {
        QMessageBox::critical(nullptr, "FPGA Configuration Failed",
                             "Failed to configure FPGA device.\n"
                             "The application will exit.");
        return 1;
    }

    // Take ownership of the already-open FpgaProcessor — never close and reopen
    auto fpgaProc = deviceDialog.takeFpgaProcessor();

    // Now show the main window
    SeizureAnalyzer window(std::move(fpgaProc));
    window.show();
    
    int result = app.exec();
    
    // Cleanup logging
    if (logStream) {
        *logStream << "=== HALOReader Log Ended ===" << Qt::endl;
        logStream->flush();
        delete logStream;
        logStream = nullptr;
    }
    if (logFile) {
        logFile->close();
        delete logFile;
        logFile = nullptr;
    }
    
    return result;
}
