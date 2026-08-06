#pragma once

#include "PhotometricNormalizer.h"

#include <cstddef>
#include <vector>

/**
 * @brief Builds conservative per-frame corrections for timelapse flicker.
 *
 * Each input model maps one original frame into a fixed sequence reference.
 * A clamped-window median removes isolated gain/background jumps while leaving
 * monotonic exposure trends unchanged. The returned correction is applied to
 * a result that is still anchored to its target frame.
 */
class TemporalPhotometricSmoother {
public:
    struct Sample {
        bool valid = false;
        PhotometricModel model;
    };

    struct Options {
        size_t windowSize = 5;
        double strength = 65.0;
        double maximumGainChange = 0.03;
        double maximumOffsetChange = 1024.0;
    };

    static bool correctionForFrame(const std::vector<Sample>& samples,
                                   size_t frameIndex,
                                   const Options& options,
                                   PhotometricModel& correction);
};
