#include "QuickPreviewWorker.h"

#include <utility>

namespace {

QString stageText(FinishingStage stage) {
    switch (stage) {
    case FinishingStage::ModifiedCameraColor:
        return QString::fromUtf8("快速预览：改机色彩还原");
    case FinishingStage::NoiseReduction:
        return QString::fromUtf8("快速预览：多尺度降噪");
    case FinishingStage::Dehaze:
        return QString::fromUtf8("快速预览：去雾");
    case FinishingStage::Stretch:
        return QString::fromUtf8("快速预览：曲线拉伸");
    case FinishingStage::GroundDetail:
        return QString::fromUtf8("快速预览：地景细节");
    case FinishingStage::StarReduction:
        return QString::fromUtf8("快速预览：缩星");
    }
    return QString::fromUtf8("快速预览");
}

} // namespace

QuickPreviewWorker::QuickPreviewWorker(
    std::shared_ptr<const std::vector<uint16_t>> sourceRgb,
    int width, int height,
    std::shared_ptr<const std::vector<uint8_t>> skyMask,
    const FinishingOptions& options,
    uint64_t generation,
    QObject* parent)
    : QThread(parent),
      m_sourceRgb(std::move(sourceRgb)),
      m_skyMask(std::move(skyMask)),
      m_width(width),
      m_height(height),
      m_options(options),
      m_generation(generation) {}

void QuickPreviewWorker::requestCancel() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

std::vector<uint16_t> QuickPreviewWorker::takeResult() {
    return std::move(m_result);
}

void QuickPreviewWorker::run() {
    m_finishingResult = {};
    m_result.clear();
    if (!m_sourceRgb) {
        m_finishingResult.error = "快速预览源缓存不存在";
        return;
    }

    m_result = *m_sourceRgb;
    const std::vector<uint8_t>* mask =
        m_skyMask && !m_skyMask->empty() ? m_skyMask.get() : nullptr;
    const bool processed = FinishingPipeline::process(
        m_result, m_width, m_height, mask, m_options, m_finishingResult,
        [this](FinishingStage stage) { emit stageMessage(stageText(stage)); },
        [this]() {
            return m_cancelRequested.load(std::memory_order_relaxed);
        });
    if (!processed) {
        m_result.clear();
        m_result.shrink_to_fit();
    }
}
