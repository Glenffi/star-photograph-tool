#include "ImageBufferUtils.h"

#include <algorithm>
#include <cmath>
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

bool extractChannel(const std::vector<uint16_t>& rgb, int width, int height,
                    int channel, std::vector<uint16_t>& samples) {
    size_t pixelCount = 0;
    if (channel < 0 || channel > 2 ||
        !checkedPixelCount(width, height, pixelCount) ||
        rgb.size() != pixelCount * 3) {
        return false;
    }
    std::vector<uint16_t> output(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        output[pixel] = rgb[pixel * 3 + static_cast<size_t>(channel)];
    }
    samples = std::move(output);
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

bool resizeRgb16ToLongSide(const std::vector<uint16_t>& rgb,
                           int width, int height, int maxLongSide,
                           std::vector<uint16_t>& resized,
                           int& resizedWidth, int& resizedHeight) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        rgb.size() != pixelCount * 3 || maxLongSide <= 0) {
        return false;
    }
    const int longSide = std::max(width, height);
    const double scale = longSide > maxLongSide
        ? static_cast<double>(maxLongSide) / longSide : 1.0;
    const int outputWidth = std::max(
        1, static_cast<int>(std::lround(width * scale)));
    const int outputHeight = std::max(
        1, static_cast<int>(std::lround(height * scale)));
    size_t outputPixelCount = 0;
    if (!checkedPixelCount(outputWidth, outputHeight, outputPixelCount)) {
        return false;
    }
    if (outputWidth == width && outputHeight == height) {
        resized = rgb;
        resizedWidth = width;
        resizedHeight = height;
        return true;
    }

    std::vector<uint16_t> output(outputPixelCount * 3);
    for (int y = 0; y < outputHeight; ++y) {
        const double sourceY = std::clamp(
            (y + 0.5) * height / outputHeight - 0.5,
            0.0, static_cast<double>(height - 1));
        const int y0 = static_cast<int>(std::floor(sourceY));
        const int y1 = std::min(height - 1, y0 + 1);
        const double fy = sourceY - y0;
        for (int x = 0; x < outputWidth; ++x) {
            const double sourceX = std::clamp(
                (x + 0.5) * width / outputWidth - 0.5,
                0.0, static_cast<double>(width - 1));
            const int x0 = static_cast<int>(std::floor(sourceX));
            const int x1 = std::min(width - 1, x0 + 1);
            const double fx = sourceX - x0;
            const size_t topLeft =
                (static_cast<size_t>(y0) * width + x0) * 3;
            const size_t topRight =
                (static_cast<size_t>(y0) * width + x1) * 3;
            const size_t bottomLeft =
                (static_cast<size_t>(y1) * width + x0) * 3;
            const size_t bottomRight =
                (static_cast<size_t>(y1) * width + x1) * 3;
            const size_t target =
                (static_cast<size_t>(y) * outputWidth + x) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                const double top = rgb[topLeft + channel] * (1.0 - fx) +
                    rgb[topRight + channel] * fx;
                const double bottom = rgb[bottomLeft + channel] * (1.0 - fx) +
                    rgb[bottomRight + channel] * fx;
                output[target + channel] = static_cast<uint16_t>(std::lround(
                    top * (1.0 - fy) + bottom * fy));
            }
        }
    }
    resized = std::move(output);
    resizedWidth = outputWidth;
    resizedHeight = outputHeight;
    return true;
}

bool resizeMask8(const std::vector<uint8_t>& mask,
                 int width, int height,
                 int resizedWidth, int resizedHeight,
                 std::vector<uint8_t>& resized) {
    size_t pixelCount = 0;
    size_t outputPixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        mask.size() != pixelCount ||
        !checkedPixelCount(resizedWidth, resizedHeight, outputPixelCount)) {
        return false;
    }
    if (width == resizedWidth && height == resizedHeight) {
        resized = mask;
        return true;
    }

    std::vector<uint8_t> output(outputPixelCount);
    for (int y = 0; y < resizedHeight; ++y) {
        const double sourceY = std::clamp(
            (y + 0.5) * height / resizedHeight - 0.5,
            0.0, static_cast<double>(height - 1));
        const int y0 = static_cast<int>(std::floor(sourceY));
        const int y1 = std::min(height - 1, y0 + 1);
        const double fy = sourceY - y0;
        for (int x = 0; x < resizedWidth; ++x) {
            const double sourceX = std::clamp(
                (x + 0.5) * width / resizedWidth - 0.5,
                0.0, static_cast<double>(width - 1));
            const int x0 = static_cast<int>(std::floor(sourceX));
            const int x1 = std::min(width - 1, x0 + 1);
            const double fx = sourceX - x0;
            const double top =
                mask[static_cast<size_t>(y0) * width + x0] * (1.0 - fx) +
                mask[static_cast<size_t>(y0) * width + x1] * fx;
            const double bottom =
                mask[static_cast<size_t>(y1) * width + x0] * (1.0 - fx) +
                mask[static_cast<size_t>(y1) * width + x1] * fx;
            output[static_cast<size_t>(y) * resizedWidth + x] =
                static_cast<uint8_t>(std::clamp(
                    std::lround(top * (1.0 - fy) + bottom * fy),
                    0L, 255L));
        }
    }
    resized = std::move(output);
    return true;
}

bool blendSkyGroundInPlace(std::vector<uint16_t>& processedSky,
                           const std::vector<uint16_t>& protectedGround,
                           const std::vector<uint8_t>& skyMask,
                           int width, int height) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        processedSky.size() != pixelCount * 3 ||
        protectedGround.size() != pixelCount * 3 ||
        skyMask.size() != pixelCount) {
        return false;
    }

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const uint32_t skyWeight = skyMask[pixel];
        const uint32_t groundWeight = 255U - skyWeight;
        const size_t base = pixel * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            processedSky[base + channel] = static_cast<uint16_t>(
                (static_cast<uint32_t>(processedSky[base + channel]) * skyWeight +
                 static_cast<uint32_t>(protectedGround[base + channel]) *
                     groundWeight +
                 127U) /
                255U);
        }
    }
    return true;
}

bool excludeShiftedGroundInPlace(
    std::vector<uint16_t>& alignedRgb,
    const std::vector<uint8_t>& alignedSourceSkyMask,
    const std::vector<uint8_t>& referenceSkyMask,
    int width, int height) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        alignedRgb.size() != pixelCount * 3 ||
        alignedSourceSkyMask.size() != pixelCount ||
        referenceSkyMask.size() != pixelCount) {
        return false;
    }

    constexpr uint8_t kValidSky = 128;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        if (referenceSkyMask[pixel] < kValidSky ||
            alignedSourceSkyMask[pixel] >= kValidSky) {
            continue;
        }
        const size_t base = pixel * 3;
        alignedRgb[base] = 0;
        alignedRgb[base + 1] = 0;
        alignedRgb[base + 2] = 0;
    }
    return true;
}

} // namespace ImageBufferUtils
