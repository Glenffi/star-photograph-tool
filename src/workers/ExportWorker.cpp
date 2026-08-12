#include "ExportWorker.h"

#include <utility>

ExportWorker::ExportWorker(
    std::shared_ptr<std::vector<uint16_t>> image,
    int width, int height, QString path,
    ImageExporter::Format format, QObject* parent)
    : QThread(parent)
    , m_image(std::move(image))
    , m_width(width)
    , m_height(height)
    , m_path(std::move(path))
    , m_format(format) {}

void ExportWorker::requestCancel() {
    m_cancelRequested.store(true);
}

void ExportWorker::run() {
    if (!m_image) return;
    m_succeeded = ImageExporter::exportRgb16(
        *m_image, m_width, m_height, m_path, m_format,
        [this]() {
            if (!m_cancelRequested.load()) return false;
            m_wasCancelled = true;
            return true;
        });
    if (!m_succeeded && m_cancelRequested.load()) {
        m_wasCancelled = true;
    }
}
