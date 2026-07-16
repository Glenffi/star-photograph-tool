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

} // namespace ImageBufferUtils
