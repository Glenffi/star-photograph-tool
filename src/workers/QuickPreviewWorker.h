#pragma once

#include "core/FinishingPipeline.h"

#include <QThread>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Re-runs only the finishing stages on a bounded linear RGB16 cache. Source
// buffers are immutable and shared with the UI, while every run owns its output.
class QuickPreviewWorker : public QThread {
    Q_OBJECT

public:
    QuickPreviewWorker(
        std::shared_ptr<const std::vector<uint16_t>> sourceRgb,
        int width, int height,
        std::shared_ptr<const std::vector<uint8_t>> skyMask,
        const FinishingOptions& options,
        uint64_t generation,
        QObject* parent = nullptr);

    void requestCancel();
    std::vector<uint16_t> takeResult();
    uint64_t generation() const { return m_generation; }
    int resultWidth() const { return m_width; }
    int resultHeight() const { return m_height; }
    bool wasCancelled() const { return m_finishingResult.cancelled; }
    QString errorString() const {
        return QString::fromStdString(m_finishingResult.error);
    }
    const StarReductionStats& starReductionStats() const {
        return m_finishingResult.starReductionStats;
    }

signals:
    void stageMessage(const QString& message);

protected:
    void run() override;

private:
    std::shared_ptr<const std::vector<uint16_t>> m_sourceRgb;
    std::shared_ptr<const std::vector<uint8_t>> m_skyMask;
    int m_width = 0;
    int m_height = 0;
    FinishingOptions m_options;
    uint64_t m_generation = 0;
    std::atomic<bool> m_cancelRequested{false};
    FinishingResult m_finishingResult;
    std::vector<uint16_t> m_result;
};
