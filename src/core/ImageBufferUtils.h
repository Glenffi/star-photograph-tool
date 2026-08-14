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
bool extractChannel(const std::vector<uint16_t>& rgb, int width, int height,
                    int channel, std::vector<uint16_t>& samples);
bool splitRgb(const std::vector<uint16_t>& rgb, int width, int height,
              RgbChannels& channels);
bool mergeRgb(const RgbChannels& channels, int width, int height,
              std::vector<uint16_t>& rgb);

// Builds a bounded linear 16-bit preview with bilinear sampling. Dimensions
// are preserved when the image already fits within maxLongSide.
bool resizeRgb16ToLongSide(const std::vector<uint16_t>& rgb,
                           int width, int height, int maxLongSide,
                           std::vector<uint16_t>& resized,
                           int& resizedWidth, int& resizedHeight);

// Resizes a grayscale mask to an explicit size using the same center-aligned
// bilinear mapping as resizeRgb16ToLongSide.
bool resizeMask8(const std::vector<uint8_t>& mask,
                 int width, int height,
                 int resizedWidth, int resizedHeight,
                 std::vector<uint8_t>& resized);

// Replaces the ground portion of processedSky in place with protectedGround.
// skyMask uses 255 for sky and 0 for ground; feather values blend continuously.
bool blendSkyGroundInPlace(std::vector<uint16_t>& processedSky,
                           const std::vector<uint16_t>& protectedGround,
                           const std::vector<uint8_t>& skyMask,
                           int width, int height);

// Sky alignment moves terrain relative to the reference horizon. These masks
// are hard maps separate from the feathered blend mask: the source map marks
// reliable sky samples, while the reference map marks every output pixel where
// sky contributes visibly. Invalid source samples are zeroed so ignore-zero
// stacking cannot build a displaced terrain silhouette.
bool excludeShiftedGroundInPlace(
    std::vector<uint16_t>& alignedRgb,
    const std::vector<uint8_t>& alignedSourceSkyMask,
    const std::vector<uint8_t>& referenceSkyMask,
    int width, int height);

} // namespace ImageBufferUtils
