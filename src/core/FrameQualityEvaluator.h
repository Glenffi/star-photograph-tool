#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct FrameQualityMetrics {
    bool valid = false;
    int detectedStars = 0;
    int usableStars = 0;
    double medianFwhm = 0.0;
    double medianEllipticity = 0.0;
    double medianFlux = 0.0;
    double backgroundMedian = 0.0;
    double backgroundNoise = 0.0;
    double clippedFraction = 0.0;
    double score = 0.0;
};

struct FrameQualitySelection {
    size_t referenceIndex = 0;
    std::vector<bool> rejected;
    size_t validMetricCount = 0;
};

/**
 * @brief Evaluates preview-scale frame quality and compares a whole sequence.
 *
 * The per-frame pass measures stars and background statistics without making
 * decisions from camera-specific absolute thresholds. selectSequence() then
 * normalizes those metrics within one sequence, chooses a reference frame and
 * optionally rejects only severe multi-metric outliers.
 */
class FrameQualityEvaluator {
public:
    static bool evaluate(const std::vector<uint16_t>& luminance,
                         int width, int height,
                         FrameQualityMetrics& metrics);

    static bool selectSequence(std::vector<FrameQualityMetrics>& metrics,
                               size_t preferredCenterIndex,
                               bool rejectSevereOutliers,
                               FrameQualitySelection& selection);
};
