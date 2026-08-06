#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct PhotometricReferenceSample {
    size_t pixelIndex = 0;
    std::array<uint16_t, 3> rgb = {};
    uint16_t luminance = 0;
};

struct PhotometricReferenceProfile {
    int width = 0;
    int height = 0;
    std::vector<PhotometricReferenceSample> samples;
};

struct PhotometricModel {
    double gain = 1.0;
    std::array<double, 3> offsets = {};
    size_t sampleCount = 0;
    size_t inlierCount = 0;
    double residualMad = 0.0;
};

/**
 * @brief Matches frame exposure and per-channel additive background to a reference.
 *
 * A sparse reference profile avoids retaining another full RGB16 frame while the
 * worker aligns the sequence. Estimation uses one shared multiplicative gain to
 * preserve color ratios, then robust per-channel offsets to absorb changing sky
 * glow. Large residuals from stars, moving foreground and local clouds are
 * rejected before the final fit.
 */
class PhotometricNormalizer {
public:
    static bool buildReferenceProfile(
        const std::vector<uint16_t>& reference, int width, int height,
        PhotometricReferenceProfile& profile, size_t maxSamples = 65536,
        const std::vector<uint8_t>* inclusionMask = nullptr,
        uint8_t minimumMaskValue = 128);

    static bool estimate(const PhotometricReferenceProfile& reference,
                         const std::vector<uint16_t>& source,
                         PhotometricModel& model);

    static bool applyInPlace(std::vector<uint16_t>& rgb, int width, int height,
                             const PhotometricModel& model);
};
