#include "file_processor_worker.h"
#include "../core/seizure_processor.h"
#include <QMutexLocker>

FileProcessorWorker::FileProcessorWorker(QObject *parent)
    : QObject(parent)
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
    
    // Process file in background thread
    SeizureProcessor processor(threshold, windowTimeout, transitionCount);
    bool success = processor.processFile(filePath.toStdString());
    
    QString error;
    if (!success) {
        error = QString::fromStdString(processor.getLastError());
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
