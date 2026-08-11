#include "MaskPreviewWorker.h"

#include "core/RawImageLoader.h"
#include "core/SkyGroundMask.h"

#include <QImage>

#include <algorithm>
#include <utility>

MaskPreviewWorker::MaskPreviewWorker(const QString& filePath, int featherRadius,
                                     QObject* parent)
    : QThread(parent)
    , m_filePath(filePath)
    , m_featherRadius(featherRadius) {}

std::vector<uint8_t> MaskPreviewWorker::takeMask() {
    return std::move(m_mask);
}

void MaskPreviewWorker::run() {
    m_error.clear();
    m_mask.clear();

    RawImageLoader loader;
    RawImageLoader::PreviewData preview;
    RawImageLoader::Metadata metadata;
    constexpr int kPreviewLongSide = 2400;
    if (!loader.loadPreview(m_filePath, kPreviewLongSide,
                            preview, &metadata)) {
        m_error = "无法加载图像";
        return;
    }
    if (isInterruptionRequested()) return;

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
        m_error = "RAW 预览数据无效";
        return;
    }
    if (std::max(image.width(), image.height()) > kPreviewLongSide) {
        image = image.scaled(kPreviewLongSide, kPreviewLongSide,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    const int previewWidth = image.width();
    const int previewHeight = image.height();
    if (isInterruptionRequested()) return;
    const int fullLongSide = std::max(metadata.width, metadata.height);
    const double previewScale = fullLongSide > 0
        ? static_cast<double>(std::max(previewWidth, previewHeight)) / fullLongSide
        : 1.0;
    const int previewFeather = std::max(0, qRound(m_featherRadius * previewScale));
    if (!SkyGroundMask::autoDetectPreview(image, previewWidth, previewHeight,
                                          m_mask, previewFeather)) {
        m_error = "地景检测失败";
        return;
    }
    if (isInterruptionRequested()) return;

    m_width = previewWidth;
    m_height = previewHeight;
}
