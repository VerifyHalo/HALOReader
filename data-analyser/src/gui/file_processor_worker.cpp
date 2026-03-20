#include "file_processor_worker.h"
#include "../core/seizure_processor.h"
#include <QMutexLocker>

FileProcessorWorker::FileProcessorWorker(FpgaProcessor* fpgaProcessor, QObject *parent)
    : QObject(parent)
    , fpgaProcessor_(fpgaProcessor)
    , shouldStop_(false)
{
}

FileProcessorWorker::~FileProcessorWorker()
{
}

void FileProcessorWorker::processFile(const QString& filePath, uint32_t threshold,
                                      uint32_t windowTimeout, uint32_t transitionCount)
{
    QMutexLocker locker(&mutex_);
    if (shouldStop_) {
        emit finished();
        return;
    }
    locker.unlock();

    // Lazy-init persistent processor with the already-open FPGA device
    if (!processor_) {
        processor_ = std::make_unique<SeizureProcessor>(fpgaProcessor_, threshold, windowTimeout, transitionCount);
    } else {
        processor_->setThreshold(threshold);
        processor_->setWindowTimeout(windowTimeout);
        processor_->setTransitionCount(transitionCount);
    }

    bool success = processor_->processFile(filePath.toStdString());

    QString error;
    if (!success) {
        error = QString::fromStdString(processor_->getLastError());
    }

    // Emit result signal (will be handled on main thread)
    emit fileProcessed(filePath, success, error);

    locker.relock();
    if (shouldStop_) {
        emit finished();
    }
}

void FileProcessorWorker::stop()
{
    QMutexLocker locker(&mutex_);
    shouldStop_ = true;
}
