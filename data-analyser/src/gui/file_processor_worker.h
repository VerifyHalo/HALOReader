#ifndef FILE_PROCESSOR_WORKER_H
#define FILE_PROCESSOR_WORKER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QWaitCondition>
#include <memory>
#include "../core/fpga_processor.h"
#include "../core/seizure_processor.h"

class FileProcessorWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileProcessorWorker(FpgaProcessor* fpgaProcessor, QObject *parent = nullptr);
    ~FileProcessorWorker();

public slots:
    void processFile(const QString& filePath, uint32_t threshold, uint32_t windowTimeout, uint32_t transitionCount);
    void stop();

signals:
    void fileProcessed(const QString& filePath, bool success, const QString& error);
    void finished();

private:
    FpgaProcessor* fpgaProcessor_;                  // non-owning, kept alive by SeizureAnalyzer
    std::unique_ptr<SeizureProcessor> processor_;   // persistent, lazy-initialized
    bool shouldStop_;
    QMutex mutex_;
};

#endif // FILE_PROCESSOR_WORKER_H
