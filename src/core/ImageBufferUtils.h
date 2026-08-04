#pragma once

#include <cstdint>
#include <vector>

namespace ImageBufferUtils {

struct RgbChannels {
    std::vector<uint16_t> red;
    std::vector<uint16_t> green;
    std::vector<uint16_t> blue;
};

// All functions validate dimensions and exact buffer lengths before allocating
// output. A failed call leaves the output object unchanged.
bool extractLuminance(const std::vector<uint16_t>& rgb, int width, int height,
                      std::vector<uint16_t>& luminance);
bool splitRgb(const std::vector<uint16_t>& rgb, int width, int height,
              RgbChannels& channels);
bool mergeRgb(const RgbChannels& channels, int width, int height,
              std::vector<uint16_t>& rgb);

// Replaces the ground portion of processedSky in place with protectedGround.
// skyMask uses 255 for sky and 0 for ground; feather values blend continuously.
bool blendSkyGroundInPlace(std::vector<uint16_t>& processedSky,
                           const std::vector<uint16_t>& protectedGround,
                           const std::vector<uint8_t>& skyMask,
                           int width, int height);

} // namespace ImageBufferUtils
