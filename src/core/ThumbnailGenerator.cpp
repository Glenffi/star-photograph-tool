#include "ThumbnailGenerator.h"
#include "RawImageLoader.h"
#include <QImage>
#include <QFileInfo>
#include <QDebug>
#include <QPointer>

#include <QMetaObject>

class ThumbnailTask : public QRunnable {
public:
    ThumbnailTask(const QString& filePath, int maxSize, ThumbnailGenerator* generator)
        : m_filePath(filePath), m_maxSize(maxSize), m_generator(generator) {}
    
    void run() override {
        RawImageLoader loader;
        RawImageLoader::PreviewData preview;
        RawImageLoader::Metadata metadata;
        QPointer<ThumbnailGenerator> safeGen(m_generator);

        const bool loaded = loader.loadPreview(m_filePath.toStdString(), m_maxSize,
                                               preview, &metadata);

        // Metadata is available after open_file(), even when pixel preview
        // extraction fails for a camera-specific RAW variant.
        QMetaObject::invokeMethod(m_generator, [safeGen, filePath = m_filePath, metadata]() {
            if (!safeGen) return;
            emit safeGen->metadataReady(filePath, metadata.iso, metadata.exposureTime,
                                        metadata.aperture, metadata.focalLength);
        }, Qt::QueuedConnection);

        if (!loaded) {
            qWarning() << "缩略图生成失败：无法读取 RAW 预览" << m_filePath;
            QMetaObject::invokeMethod(m_generator, [safeGen, filePath = m_filePath]() {
                if (!safeGen) return;
                emit safeGen->thumbnailReady(filePath, QPixmap());
            }, Qt::QueuedConnection);
            return;
        }

        QImage image;
        if (preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
            image = QImage::fromData(preview.bytes.data(),
                                     static_cast<int>(preview.bytes.size()), "JPEG");
        } else if (preview.width > 0 && preview.height > 0) {
            QImage borrowed(preview.bytes.data(), preview.width, preview.height,
                            preview.width * 3, QImage::Format_RGB888);
            image = borrowed.copy();
        }
        if (image.isNull()) {
            qWarning() << "缩略图生成失败：预览数据格式无效" << m_filePath;
            QMetaObject::invokeMethod(m_generator, [safeGen, filePath = m_filePath]() {
                if (!safeGen) return;
                emit safeGen->thumbnailReady(filePath, QPixmap());
            }, Qt::QueuedConnection);
            return;
        }

        const QImage imageOwned = image.scaled(m_maxSize, m_maxSize,
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
        // 将 QPixmap 转换移到主线程执行
        QMetaObject::invokeMethod(m_generator, [safeGen, filePath = m_filePath, imageOwned]() {
            if (!safeGen) return;
            emit safeGen->thumbnailReady(filePath, QPixmap::fromImage(imageOwned));
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

ThumbnailGenerator::~ThumbnailGenerator() {
    cancelAll();
    m_pool.waitForDone();
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
