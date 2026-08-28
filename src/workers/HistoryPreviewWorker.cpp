#include "HistoryPreviewWorker.h"

#include "core/ImageExporter.h"
#include "core/PreviewToneMapper.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <exception>
#include <vector>

HistoryPreviewWorker::HistoryPreviewWorker(const QString& path,
                                           QObject* parent)
    : QThread(parent), m_path(path) {}

void HistoryPreviewWorker::run() {
    try {
        std::vector<uint16_t> rgb;
        int width = 0;
        int height = 0;
        if (!ImageExporter::loadTiffRgb16(
                m_path, rgb, width, height, 2400,
                [this]() { return isInterruptionRequested(); })) {
            if (isInterruptionRequested()) return;
            emit failed(m_path, QCoreApplication::translate("Nebulens", "无法读取 16-bit TIFF"));
            return;
        }
        if (isInterruptionRequested()) return;

        const PreviewImage8 mapped = PreviewToneMapper::mapRgb16(
            rgb, width, height, 2400);
        if (mapped.rgb.empty() || mapped.width <= 0 || mapped.height <= 0) {
            emit failed(m_path, QCoreApplication::translate("Nebulens", "无法生成历史结果预览"));
            return;
        }
        const QImage borrowed(mapped.rgb.data(), mapped.width, mapped.height,
                              mapped.width * 3, QImage::Format_RGB888);
        const QImage preview = borrowed.copy();
        if (preview.isNull()) {
            emit failed(m_path, QCoreApplication::translate("Nebulens", "历史结果预览内存不足"));
            return;
        }
        if (!isInterruptionRequested()) emit loaded(m_path, preview);
    } catch (const std::exception&) {
        if (!isInterruptionRequested()) {
            emit failed(m_path, QCoreApplication::translate("Nebulens", "历史结果预览内存不足"));
        }
    }
}
