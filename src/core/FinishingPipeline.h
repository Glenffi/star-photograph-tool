#pragma once

#include "AutoOptimizeEngine.h"
#include "StarReducer.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class FinishingStage {
    ModifiedCameraColor,
    NoiseReduction,
    Dehaze,
    Stretch,
    GroundDetail,
    StarReduction
};

struct FinishingOptions {
    bool noiseReductionEnabled = false;
    int noiseReductionStrength = 30;
    bool modifiedCameraColorEnabled = false;
    ModifiedCameraColorOptions modifiedCameraColor;
    bool dehazeEnabled = false;
    int dehazeStrength = 30;
    bool stretchEnabled = false;
    bool skyGroundSeparation = false;
    int groundDetailStrength = 40;
    bool starReductionEnabled = false;
    int starReductionStrength = 70;
};

struct FinishingResult {
    ModifiedCameraColorStats modifiedCameraColorStats;
    StarReductionStats starReductionStats;
    bool cancelled = false;
    std::string error;
};

using StageCallback = std::function<void(FinishingStage)>;
using CancelCheck = std::function<bool()>;

/**
 * Reusable, UI-independent finishing sequence for linear RGB16 images.
 *
 * The pipeline intentionally owns no thread. Callers decide where it runs and
 * may provide callbacks for progress reporting and cooperative cancellation.
 * A sky mask uses 255 for sky, 0 for ground and intermediate values for the
 * feathered transition.
 */
class FinishingPipeline {
public:
    static bool process(std::vector<uint16_t>& rgb, int width, int height,
                        const std::vector<uint8_t>* skyMask,
                        const FinishingOptions& options,
                        FinishingResult& result,
                        StageCallback stageCallback = {},
                        CancelCheck cancelCheck = {});
};
