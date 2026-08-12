#pragma once

#include "core/ImageExporter.h"

#include <QThread>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

/**
 * @brief Writes an already processed full-resolution result off the UI thread.
 *
 * The image buffer is shared read-only with MainWindow. MainWindow blocks
 * project edits while this worker is active and waits for it on shutdown, so
 * large PNG compression or TIFF I/O never freezes the interface or observes a
 * mutated buffer.
 */
class ExportWorker : public QThread {
    Q_OBJECT

public:
    ExportWorker(std::shared_ptr<std::vector<uint16_t>> image,
                 int width, int height, QString path,
                 ImageExporter::Format format,
                 QObject* parent = nullptr);

    void requestCancel();
    bool succeeded() const { return m_succeeded; }
    bool wasCancelled() const { return m_wasCancelled; }
    QString outputPath() const { return m_path; }

protected:
    void run() override;

private:
    std::shared_ptr<std::vector<uint16_t>> m_image;
    int m_width = 0;
    int m_height = 0;
    QString m_path;
    ImageExporter::Format m_format = ImageExporter::Tiff16;
    std::atomic<bool> m_cancelRequested{false};
    bool m_succeeded = false;
    bool m_wasCancelled = false;
};
