#include "FinishingPipeline.h"

#include "ImageBufferUtils.h"
#include "NoiseReductionEngine.h"

#include <exception>
#include <limits>
#include <utility>

namespace {

bool checkedPixelCount(int width, int height, size_t& pixelCount) {
    if (width <= 0 || height <= 0) return false;
    const size_t unsignedWidth = static_cast<size_t>(width);
    const size_t unsignedHeight = static_cast<size_t>(height);
    if (unsignedWidth > std::numeric_limits<size_t>::max() / unsignedHeight) {
        return false;
    }
    pixelCount = unsignedWidth * unsignedHeight;
    return pixelCount <= std::numeric_limits<size_t>::max() / 3;
}

} // namespace

bool FinishingPipeline::process(
    std::vector<uint16_t>& rgb, int width, int height,
    const std::vector<uint8_t>* skyMask, const FinishingOptions& options,
    FinishingResult& result, StageCallback stageCallback,
    CancelCheck cancelCheck) {
    result = {};

    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        rgb.size() != pixelCount * 3) {
        result.error = "Invalid RGB dimensions or buffer size";
        return false;
    }
    if (options.skyGroundSeparation && !skyMask) {
        result.error = "Sky-ground separation requires a sky mask";
        return false;
    }
    if (options.skyGroundSeparation && skyMask->size() != pixelCount) {
        result.error = "Sky mask dimensions do not match the RGB image";
        return false;
    }

    const auto stopIfCancelled = [&]() {
        if (cancelCheck && cancelCheck()) {
            result.cancelled = true;
            return true;
        }
        return false;
    };
    const auto beginStage = [&](FinishingStage stage) {
        if (stopIfCancelled()) return false;
        if (stageCallback) stageCallback(stage);
        // The callback may relay work to another thread, so check again before
        // starting the expensive image operation.
        return !stopIfCancelled();
    };
    const auto finishStage = [&]() {
        return !stopIfCancelled();
    };

    try {
        if (stopIfCancelled()) return false;

        if (options.modifiedCameraColorEnabled &&
            options.modifiedCameraColor.strength > 0) {
            if (!beginStage(FinishingStage::ModifiedCameraColor)) return false;
            std::vector<uint16_t> restored;
            const std::vector<uint8_t>* samplingMask =
                options.skyGroundSeparation ? skyMask : nullptr;
            if (!AutoOptimizeEngine::restoreModifiedCameraColorRgb(
                    rgb, width, height, restored,
                    &result.modifiedCameraColorStats, samplingMask,
                    options.modifiedCameraColor)) {
                result.error = "Modified-camera color restoration failed";
                return false;
            }
            rgb = std::move(restored);
            if (!finishStage()) return false;
        }

        if (options.noiseReductionEnabled &&
            options.noiseReductionStrength > 0) {
            if (!beginStage(FinishingStage::NoiseReduction)) return false;
            std::vector<uint16_t> denoised;
            if (!NoiseReductionEngine::denoiseRgb(
                    rgb, width, height, options.noiseReductionStrength,
                    denoised)) {
                result.error = "Multiscale RGB noise reduction failed";
                return false;
            }
            if (options.skyGroundSeparation &&
                !ImageBufferUtils::blendSkyGroundInPlace(
                    denoised, rgb, *skyMask, width, height)) {
                result.error = "Sky-ground noise reduction blend failed";
                return false;
            }
            rgb = std::move(denoised);
            if (!finishStage()) return false;
        }

        if (options.dehazeEnabled) {
            if (!beginStage(FinishingStage::Dehaze)) return false;
            std::vector<uint16_t> dehazed;
            if (!AutoOptimizeEngine::dehazeRgb(
                    rgb, width, height, options.dehazeStrength, dehazed)) {
                result.error = "RGB dehaze failed";
                return false;
            }
            rgb = std::move(dehazed);
            if (!finishStage()) return false;
        }

        if (options.stretchEnabled) {
            if (!beginStage(FinishingStage::Stretch)) return false;
            std::vector<uint16_t> stretched;
            if (!AutoOptimizeEngine::stretchRgb(
                    rgb, width, height, stretched)) {
                result.error = "RGB curve stretch failed";
                return false;
            }
            rgb = std::move(stretched);
            if (!finishStage()) return false;
        }

        if (options.skyGroundSeparation &&
            options.groundDetailStrength > 0) {
            if (!beginStage(FinishingStage::GroundDetail)) return false;
            if (!AutoOptimizeEngine::enhanceGroundDetail(
                    rgb, width, height, *skyMask,
                    options.groundDetailStrength)) {
                result.error = "Ground detail restoration failed";
                return false;
            }
            if (!finishStage()) return false;
        }

        if (options.starReductionEnabled &&
            options.starReductionStrength > 0) {
            if (!beginStage(FinishingStage::StarReduction)) return false;
            const std::vector<uint8_t>* processingMask =
                options.skyGroundSeparation ? skyMask : nullptr;
            if (!StarReducer::reduce(
                    rgb, width, height, options.starReductionStrength,
                    &result.starReductionStats, processingMask)) {
                result.error = "Star reduction failed";
                return false;
            }
            if (!finishStage()) return false;
        }
        return !stopIfCancelled();
    } catch (const std::exception& exception) {
        result.error = std::string("Finishing pipeline exception: ") +
            exception.what();
        return false;
    } catch (...) {
        result.error = "Finishing pipeline failed with an unknown exception";
        return false;
    }
}
