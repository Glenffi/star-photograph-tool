#include "ImageBufferUtils.h"

#include <limits>

namespace {

bool checkedPixelCount(int width, int height, size_t& pixelCount) {
    if (width <= 0 || height <= 0) return false;
    if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() /
                                     static_cast<size_t>(height)) {
        return false;
    }
    pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    return pixelCount <= std::numeric_limits<size_t>::max() / 3;
}

} // namespace

namespace ImageBufferUtils {

bool extractLuminance(const std::vector<uint16_t>& rgb, int width, int height,
                      std::vector<uint16_t>& luminance) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) || rgb.size() != pixelCount * 3) {
        return false;
    }

    std::vector<uint16_t> output(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint32_t red = rgb[i * 3];
        const uint32_t green = rgb[i * 3 + 1];
        const uint32_t blue = rgb[i * 3 + 2];
        output[i] = static_cast<uint16_t>((red * 299 + green * 587 + blue * 114) / 1000);
    }
    luminance = std::move(output);
    return true;
}

bool splitRgb(const std::vector<uint16_t>& rgb, int width, int height,
              RgbChannels& channels) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) || rgb.size() != pixelCount * 3) {
        return false;
    }

    RgbChannels output;
    output.red.resize(pixelCount);
    output.green.resize(pixelCount);
    output.blue.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        output.red[i] = rgb[i * 3];
        output.green[i] = rgb[i * 3 + 1];
        output.blue[i] = rgb[i * 3 + 2];
    }
    channels = std::move(output);
    return true;
}

bool mergeRgb(const RgbChannels& channels, int width, int height,
              std::vector<uint16_t>& rgb) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        channels.red.size() != pixelCount ||
        channels.green.size() != pixelCount ||
        channels.blue.size() != pixelCount) {
        return false;
    }

    std::vector<uint16_t> output(pixelCount * 3);
    for (size_t i = 0; i < pixelCount; ++i) {
        output[i * 3] = channels.red[i];
        output[i * 3 + 1] = channels.green[i];
        output[i * 3 + 2] = channels.blue[i];
    }
    rgb = std::move(output);
    return true;
}

} // namespace ImageBufferUtils
