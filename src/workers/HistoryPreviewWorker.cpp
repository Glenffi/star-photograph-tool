#include "HistoryPreviewWorker.h"

#include "core/ImageExporter.h"
#include "core/PreviewToneMapper.h"

#include <QFileInfo>

#include <vector>

HistoryPreviewWorker::HistoryPreviewWorker(const QString& path,
                                           QObject* parent)
    : QThread(parent), m_path(path) {}

void HistoryPreviewWorker::run() {
    std::vector<uint16_t> rgb;
    int width = 0;
    int height = 0;
    if (!ImageExporter::loadTiffRgb16(m_path, rgb, width, height)) {
        emit failed(m_path, QString::fromUtf8("无法读取 16-bit TIFF"));
        return;
    }
    if (isInterruptionRequested()) return;

    const PreviewImage8 mapped = PreviewToneMapper::mapRgb16(
        rgb, width, height, 2400);
    if (mapped.rgb.empty() || mapped.width <= 0 || mapped.height <= 0) {
        emit failed(m_path, QString::fromUtf8("无法生成历史结果预览"));
        return;
    }
    const QImage borrowed(mapped.rgb.data(), mapped.width, mapped.height,
                          mapped.width * 3, QImage::Format_RGB888);
    const QImage preview = borrowed.copy();
    if (!isInterruptionRequested()) emit loaded(m_path, preview);
}
