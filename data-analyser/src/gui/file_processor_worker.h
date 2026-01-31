#ifndef FILE_PROCESSOR_WORKER_H
#define FILE_PROCESSOR_WORKER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QWaitCondition>

class FileProcessorWorker : public QObject
{
    Q_OBJECT

public:
    explicit FileProcessorWorker(QObject *parent = nullptr);
    ~FileProcessorWorker();

public slots:
    void processFile(const QString& filePath, uint32_t threshold, uint32_t windowTimeout, uint32_t transitionCount);
    void stop();

signals:
    void fileProcessed(const QString& filePath, bool success, const QString& error);
    void finished();

private:
    bool shouldStop_;
    QMutex mutex_;
};

#endif // FILE_PROCESSOR_WORKER_H
