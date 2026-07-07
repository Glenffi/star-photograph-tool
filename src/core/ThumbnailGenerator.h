#pragma once

#include <QObject>
#include <QPixmap>
#include <QThreadPool>
#include <QString>
#include <QStringList>
#include <QRunnable>
#include <QMutex>

class ThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailGenerator(QObject* parent = nullptr);
    ~ThumbnailGenerator() override;
    
    void generateAsync(const QString& filePath, int maxSize);
    void generateBatch(const QStringList& filePaths, int maxSize);
    
    void cancelAll();
    void setMaxConcurrent(int maxConcurrent);
    
signals:
    void thumbnailReady(const QString& filePath, const QPixmap& thumbnail);
    void metadataReady(const QString& filePath, int iso, double exposureTime, double aperture, int focalLength);
    void batchProgress(int current, int total);
    void batchFinished();
    
private:
    QThreadPool m_pool;
    QMutex m_mutex;
    int m_totalBatch = 0;
    int m_completedBatch = 0;
};
