#pragma once

#include <cstdint>
#include <vector>

/**
 * @brief Linear RGB multiscale noise reduction.
 *
 * The engine converts RGB into one luminance and two color-difference planes,
 * applies two levels of edge-preserving a-trous wavelet shrinkage, then
 * reconstructs RGB. Chroma receives stronger thresholds than luminance so
 * color speckle is reduced without smearing Milky Way structure.
 */
class NoiseReductionEngine {
public:
    /**
     * @param src       Input interleaved linear RGB16 pixels.
     * @param width     Image width.
     * @param height    Image height.
     * @param strength  0-100. Zero is an exact no-op.
     * @param dst       Output interleaved linear RGB16 pixels.
     */
    static bool denoiseRgb(const std::vector<uint16_t>& src,
                           int width, int height, int strength,
                           std::vector<uint16_t>& dst);
};
