#include "PreviewToneMapper.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

bool validPixelCount(int width, int height, size_t channels, size_t size) {
    if (width <= 0 || height <= 0 || channels == 0) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h) return false;
    const size_t pixels = w * h;
    return pixels <= std::numeric_limits<size_t>::max() / channels
        && pixels * channels == size;
}

void previewDimensions(int width, int height, int maxLongSide, int& outWidth, int& outHeight) {
    const int limit = std::max(1, maxLongSide);
    const int longSide = std::max(width, height);
    if (longSide <= limit) {
        outWidth = width;
        outHeight = height;
        return;
    }

    const double scale = static_cast<double>(limit) / longSide;
    outWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    outHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
}

std::pair<uint16_t, uint16_t> displayRange(std::vector<uint16_t>& samples) {
    if (samples.empty()) return {0, 65535};

    const size_t blackIndex = samples.size() / 100;       // 1st percentile
    const size_t whiteIndex = samples.size() * 995 / 1000; // 99.5th percentile
    std::nth_element(samples.begin(), samples.begin() + blackIndex, samples.end());
    const uint16_t black = samples[blackIndex];
    std::nth_element(samples.begin(), samples.begin() + whiteIndex, samples.end());
    uint16_t white = samples[whiteIndex];

    if (white <= black) {
        const auto range = std::minmax_element(samples.begin(), samples.end());
        white = *range.second;
        if (white <= black) white = static_cast<uint16_t>(std::min(65535, black + 1));
    }
    return {black, white};
}

std::array<uint8_t, 65536> makeLut(uint16_t black, uint16_t white) {
    std::array<uint8_t, 65536> lut{};
    const double range = std::max(1, static_cast<int>(white) - static_cast<int>(black));
    constexpr double stretch = 8.0;
    const double denominator = std::asinh(stretch);

    for (size_t i = 0; i < lut.size(); ++i) {
        const double normalized = std::clamp((static_cast<double>(i) - black) / range, 0.0, 1.0);
        const double mapped = std::asinh(normalized * stretch) / denominator;
        lut[i] = static_cast<uint8_t>(std::lround(mapped * 255.0));
    }
    return lut;
}

template <typename SampleFn>
std::vector<uint16_t> collectSamples(size_t pixelCount, SampleFn sample) {
    constexpr size_t maxSamples = 65536;
    const size_t step = std::max<size_t>(1, (pixelCount - 1) / maxSamples + 1);
    std::vector<uint16_t> samples;
    samples.reserve(std::min(pixelCount, maxSamples + 1));
    for (size_t i = 0; i < pixelCount; i += step) samples.push_back(sample(i));
    return samples;
}

} // namespace

PreviewImage8 PreviewToneMapper::mapMono16(const std::vector<uint16_t>& data,
                                            int width,
                                            int height,
                                            int maxLongSide) {
    PreviewImage8 result;
    if (!validPixelCount(width, height, 1, data.size())) return result;

    const size_t pixelCount = static_cast<size_t>(width) * height;
    auto samples = collectSamples(pixelCount, [&](size_t i) { return data[i]; });
    const auto [black, white] = displayRange(samples);
    const auto lut = makeLut(black, white);
    previewDimensions(width, height, maxLongSide, result.width, result.height);
    result.rgb.resize(static_cast<size_t>(result.width) * result.height * 3);

    for (int y = 0; y < result.height; ++y) {
        const int sourceY = std::min(height - 1, static_cast<int>((static_cast<int64_t>(y) * height) / result.height));
        for (int x = 0; x < result.width; ++x) {
            const int sourceX = std::min(width - 1, static_cast<int>((static_cast<int64_t>(x) * width) / result.width));
            const uint8_t value = lut[data[static_cast<size_t>(sourceY) * width + sourceX]];
            const size_t target = (static_cast<size_t>(y) * result.width + x) * 3;
            result.rgb[target] = value;
            result.rgb[target + 1] = value;
            result.rgb[target + 2] = value;
        }
    }
    return result;
}

PreviewImage8 PreviewToneMapper::mapRgb16(const std::vector<uint16_t>& rgb,
                                           int width,
                                           int height,
                                           int maxLongSide) {
    PreviewImage8 result;
    if (!validPixelCount(width, height, 3, rgb.size())) return result;

    const size_t pixelCount = static_cast<size_t>(width) * height;
    auto samples = collectSamples(pixelCount, [&](size_t i) {
        const size_t base = i * 3;
        const uint32_t luminance = 13933U * rgb[base] + 46871U * rgb[base + 1]
                                 + 4732U * rgb[base + 2];
        return static_cast<uint16_t>(luminance >> 16);
    });
    const auto [black, white] = displayRange(samples);
    const auto lut = makeLut(black, white);
    previewDimensions(width, height, maxLongSide, result.width, result.height);
    result.rgb.resize(static_cast<size_t>(result.width) * result.height * 3);

    for (int y = 0; y < result.height; ++y) {
        const int sourceY = std::min(height - 1, static_cast<int>((static_cast<int64_t>(y) * height) / result.height));
        for (int x = 0; x < result.width; ++x) {
            const int sourceX = std::min(width - 1, static_cast<int>((static_cast<int64_t>(x) * width) / result.width));
            const size_t source = (static_cast<size_t>(sourceY) * width + sourceX) * 3;
            const size_t target = (static_cast<size_t>(y) * result.width + x) * 3;
            result.rgb[target] = lut[rgb[source]];
            result.rgb[target + 1] = lut[rgb[source + 1]];
            result.rgb[target + 2] = lut[rgb[source + 2]];
        }
    }
    return result;
}
