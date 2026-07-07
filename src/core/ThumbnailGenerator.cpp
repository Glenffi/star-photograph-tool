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
            QMetaObject::invokeMethod(m_generator, [generator = m_generator, filePath = m_filePath]() {
                emit generator->metadataReady(filePath, 0, 0.0, 0.0, 0);
                emit generator->thumbnailReady(filePath, QPixmap());
            }, Qt::QueuedConnection);
            return;
        }
        
        // 先发射元数据（主线程安全）
        int iso = imageData.iso;
        double exposureTime = imageData.exposureTime;
        double aperture = imageData.aperture;
        int focalLength = imageData.focalLength;
        QMetaObject::invokeMethod(m_generator, [generator = m_generator, filePath = m_filePath, iso, exposureTime, aperture, focalLength]() {
            emit generator->metadataReady(filePath, iso, exposureTime, aperture, focalLength);
        }, Qt::QueuedConnection);
        
        std::vector<uint8_t> thumbData;
        if (!loader.generateThumbnail(imageData, m_maxSize, thumbData)) {
            qWarning() << "缩略图生成失败：无法生成缩略图" << m_filePath;
            QMetaObject::invokeMethod(m_generator, [generator = m_generator, filePath = m_filePath]() {
                emit generator->thumbnailReady(filePath, QPixmap());
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
        // 深拷贝：thumbData 是局部变量，run() 返回后会被释放
        QImage imageOwned = image.copy();
        
        // 将 QPixmap 转换移到主线程执行
        QMetaObject::invokeMethod(m_generator, [generator = m_generator, filePath = m_filePath, imageOwned]() {
            emit generator->thumbnailReady(filePath, QPixmap::fromImage(imageOwned));
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
    
    // 在构造函数中一次性连接 batch 进度跟踪，避免 generateBatch 重复连接
    connect(this, &ThumbnailGenerator::thumbnailReady, this, [this](const QString&, const QPixmap&) {
        QMutexLocker locker(&m_mutex);
        if (m_totalBatch <= 0) return; // 不是 batch 模式
        
        m_completedBatch++;
        emit batchProgress(m_completedBatch, m_totalBatch);
        if (m_completedBatch >= m_totalBatch) {
            m_totalBatch = 0;
            m_completedBatch = 0;
            emit batchFinished();
        }
    });
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
