#include "ThumbnailGenerator.h"
#include "RawImageLoader.h"
#include <QImage>
#include <QFileInfo>
#include <QDebug>

#include <QMetaObject>

class ThumbnailTask : public QRunnable {
public:
    ThumbnailTask(const QString& filePath, int maxSize, ThumbnailGenerator* generator)
        : m_filePath(filePath), m_maxSize(maxSize), m_generator(generator) {}
    
    void run() override {
        // 使用 LibRaw 加载 RAW 并生成缩略图
        RawImageLoader loader;
        RawImageLoader::ImageData imageData;
        
        if (!loader.loadRaw(m_filePath.toStdString(), imageData)) {
            qWarning() << "缩略图生成失败：无法加载 RAW" << m_filePath;
            QMetaObject::invokeMethod(m_generator, [this]() {
                emit m_generator->thumbnailReady(m_filePath, QPixmap());
            }, Qt::QueuedConnection);
            return;
        }
        
        std::vector<uint8_t> thumbData;
        if (!loader.generateThumbnail(imageData, m_maxSize, thumbData)) {
            qWarning() << "缩略图生成失败：无法生成缩略图" << m_filePath;
            QMetaObject::invokeMethod(m_generator, [this]() {
                emit m_generator->thumbnailReady(m_filePath, QPixmap());
            }, Qt::QueuedConnection);
            return;
        }
        
        // 计算缩略图尺寸
        int thumbWidth, thumbHeight;
        if (imageData.width > imageData.height) {
            thumbWidth = m_maxSize;
            thumbHeight = imageData.height * m_maxSize / imageData.width;
        } else {
            thumbHeight = m_maxSize;
            thumbWidth = imageData.width * m_maxSize / imageData.height;
        }
        if (thumbWidth < 1) thumbWidth = 1;
        if (thumbHeight < 1) thumbHeight = 1;
        
        // 创建 QImage（在 worker 线程安全）
        QImage image(thumbData.data(), thumbWidth, thumbHeight, thumbWidth * 3, QImage::Format_RGB888);
        
        // 将 QPixmap 转换移到主线程执行
        QMetaObject::invokeMethod(m_generator, [this, image]() {
            emit m_generator->thumbnailReady(m_filePath, QPixmap::fromImage(image.copy()));
        }, Qt::QueuedConnection);
    }
    
private:
    QString m_filePath;
    int m_maxSize;
    ThumbnailGenerator* m_generator;
};

ThumbnailGenerator::ThumbnailGenerator(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(QThread::idealThreadCount());
    m_pool.setExpiryTimeout(30000); // 30秒超时
}

void ThumbnailGenerator::generateAsync(const QString& filePath, int maxSize) {
    auto* task = new ThumbnailTask(filePath, maxSize, this);
    task->setAutoDelete(true);
    m_pool.start(task);
}

void ThumbnailGenerator::generateBatch(const QStringList& filePaths, int maxSize) {
    QMutexLocker locker(&m_mutex);
    m_totalBatch = filePaths.size();
    m_completedBatch = 0;
    
    for (const QString& filePath : filePaths) {
        auto* task = new ThumbnailTask(filePath, maxSize, this);
        task->setAutoDelete(true);
        
        connect(this, &ThumbnailGenerator::thumbnailReady, this, [this](const QString&, const QPixmap&) {
            QMutexLocker locker(&m_mutex);
            m_completedBatch++;
            emit batchProgress(m_completedBatch, m_totalBatch);
            if (m_completedBatch >= m_totalBatch) {
                emit batchFinished();
            }
        }, Qt::UniqueConnection);
        
        m_pool.start(task);
    }
    
    if (filePaths.isEmpty()) {
        emit batchFinished();
    }
}

void ThumbnailGenerator::cancelAll() {
    m_pool.clear();
}

void ThumbnailGenerator::setMaxConcurrent(int maxConcurrent) {
    m_pool.setMaxThreadCount(maxConcurrent);
}