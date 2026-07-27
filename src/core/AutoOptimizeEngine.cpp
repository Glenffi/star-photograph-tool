#include "AutoOptimizeEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

bool rgbPixelCount(const std::vector<uint16_t>& src, int w, int h,
                   size_t& pixelCount) {
    if (w <= 0 || h <= 0 ||
        static_cast<size_t>(w) > std::numeric_limits<size_t>::max() /
                                     static_cast<size_t>(h)) {
        return false;
    }
    pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);
    return pixelCount <= std::numeric_limits<size_t>::max() / 3 &&
           src.size() == pixelCount * 3;
}

uint16_t luminanceAt(const std::vector<uint16_t>& rgb, size_t pixel) {
    const size_t base = pixel * 3;
    const uint32_t luminance =
        13933U * rgb[base] + 46871U * rgb[base + 1] + 4732U * rgb[base + 2];
    return static_cast<uint16_t>(luminance >> 16);
}

uint16_t percentileValue(std::vector<uint16_t> values,
                         size_t numerator, size_t denominator) {
    if (values.empty() || denominator == 0) return 0;
    const size_t count = values.size() - 1;
    const size_t index = std::min(
        count, count / denominator * numerator +
                   (count % denominator) * numerator / denominator);
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

uint16_t medianValue(std::vector<uint16_t> values) {
    return percentileValue(std::move(values), 1, 2);
}

uint16_t clampToUint16(double value) {
    return static_cast<uint16_t>(
        std::lround(std::clamp(value, 0.0, 65535.0)));
}

} // namespace

static void normalizeToFloat(const std::vector<uint16_t>& src, std::vector<float>& dst, int w, int h) {
    dst.resize(w * h);
    for (int i = 0; i < w * h; ++i) {
        dst[i] = static_cast<float>(src[i]) / 65535.0f;
    }
}

static void convertToUint16(const std::vector<float>& src, std::vector<uint16_t>& dst, int w, int h) {
    dst.resize(w * h);
    for (int i = 0; i < w * h; ++i) {
        float v = std::max(0.0f, std::min(1.0f, src[i]));
        dst[i] = static_cast<uint16_t>(v * 65535.0f + 0.5f);
    }
}

bool AutoOptimizeEngine::neutralizeBackgroundRgb(
    const std::vector<uint16_t>& src, int w, int h,
    std::vector<uint16_t>& dst) {
    size_t pixelCount = 0;
    if (!rgbPixelCount(src, w, h, pixelCount)) return false;

    if (w >= 32 && h >= 32) {
        const int gridColumns = std::clamp(w / 256, 6, 16);
        const int gridRows = std::clamp(h / 256, 4, 12);
        const size_t gridSize =
            static_cast<size_t>(gridColumns) * gridRows;
        std::array<std::vector<uint16_t>, 3> grid;
        for (auto& channel : grid) channel.resize(gridSize);

        // A low percentile in each cell rejects stars and most bright subject
        // detail. The resulting coarse surface follows additive sky glow and
        // color gradients without blurring the full-resolution image.
        for (int gy = 0; gy < gridRows; ++gy) {
            const int y0 = gy * h / gridRows;
            const int y1 = std::max(y0 + 1, (gy + 1) * h / gridRows);
            for (int gx = 0; gx < gridColumns; ++gx) {
                const int x0 = gx * w / gridColumns;
                const int x1 = std::max(x0 + 1, (gx + 1) * w / gridColumns);
                const size_t cellPixels =
                    static_cast<size_t>(x1 - x0) * (y1 - y0);
                const size_t sampleStep =
                    std::max<size_t>(1, cellPixels / 2048);
                std::array<std::vector<uint16_t>, 3> samples;
                for (auto& channel : samples) {
                    channel.reserve(std::min<size_t>(cellPixels, 2049));
                }
                size_t position = 0;
                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x, ++position) {
                        if (position % sampleStep != 0) continue;
                        const size_t base =
                            (static_cast<size_t>(y) * w + x) * 3;
                        for (size_t channel = 0; channel < 3; ++channel) {
                            samples[channel].push_back(src[base + channel]);
                        }
                    }
                }
                const size_t index =
                    static_cast<size_t>(gy) * gridColumns + gx;
                for (size_t channel = 0; channel < 3; ++channel) {
                    grid[channel][index] =
                        percentileValue(std::move(samples[channel]), 20, 100);
                }
            }
        }

        // Median smoothing makes the surface robust to an isolated bright
        // horizon cell or a dense Milky Way patch.
        std::array<std::vector<uint16_t>, 3> smoothGrid = grid;
        for (size_t channel = 0; channel < 3; ++channel) {
            for (int gy = 0; gy < gridRows; ++gy) {
                for (int gx = 0; gx < gridColumns; ++gx) {
                    std::vector<uint16_t> neighbors;
                    neighbors.reserve(9);
                    for (int dy = -1; dy <= 1; ++dy) {
                        const int yy = std::clamp(gy + dy, 0, gridRows - 1);
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int xx =
                                std::clamp(gx + dx, 0, gridColumns - 1);
                            neighbors.push_back(
                                grid[channel][static_cast<size_t>(yy) *
                                                  gridColumns + xx]);
                        }
                    }
                    smoothGrid[channel][static_cast<size_t>(gy) *
                                                gridColumns + gx] =
                        medianValue(std::move(neighbors));
                }
            }
        }

        std::array<uint16_t, 3> channelBackground{};
        for (size_t channel = 0; channel < 3; ++channel) {
            channelBackground[channel] = medianValue(smoothGrid[channel]);
        }
        // Never invent signal in a weak channel. The lowest channel background
        // is the neutral residual; stronger additive casts are subtracted.
        const double target = *std::min_element(
            channelBackground.begin(), channelBackground.end());

        std::vector<uint16_t> output(src.size());
        for (int y = 0; y < h; ++y) {
            const double gridY = (static_cast<double>(y) + 0.5) *
                                     gridRows / h - 0.5;
            const int yLow = gridY <= 0.0
                ? 0 : gridY >= gridRows - 1
                    ? gridRows - 1
                    : static_cast<int>(std::floor(gridY));
            const int yHigh =
                gridY <= 0.0 || gridY >= gridRows - 1
                    ? yLow : yLow + 1;
            const double yFraction =
                yLow == yHigh ? 0.0 : gridY - yLow;
            for (int x = 0; x < w; ++x) {
                const double gridX = (static_cast<double>(x) + 0.5) *
                                         gridColumns / w - 0.5;
                const int xLow = gridX <= 0.0
                    ? 0 : gridX >= gridColumns - 1
                        ? gridColumns - 1
                        : static_cast<int>(std::floor(gridX));
                const int xHigh =
                    gridX <= 0.0 || gridX >= gridColumns - 1
                        ? xLow : xLow + 1;
                const double xFraction =
                    xLow == xHigh ? 0.0 : gridX - xLow;
                const size_t base =
                    (static_cast<size_t>(y) * w + x) * 3;
                for (size_t channel = 0; channel < 3; ++channel) {
                    const auto& surface = smoothGrid[channel];
                    const double top =
                        surface[static_cast<size_t>(yLow) * gridColumns + xLow] *
                            (1.0 - xFraction) +
                        surface[static_cast<size_t>(yLow) * gridColumns + xHigh] *
                            xFraction;
                    const double bottom =
                        surface[static_cast<size_t>(yHigh) * gridColumns + xLow] *
                            (1.0 - xFraction) +
                        surface[static_cast<size_t>(yHigh) * gridColumns + xHigh] *
                            xFraction;
                    const double model =
                        top * (1.0 - yFraction) + bottom * yFraction;
                    const double correction = std::max(0.0, model - target);
                    output[base + channel] =
                        clampToUint16(src[base + channel] - correction);
                }
            }
        }
        dst = std::move(output);
        return true;
    }

    // Prefer the upper 75% of a nightscape so a dark foreground does not drive
    // sky color estimation. This is also harmless for all-sky/deep-sky frames.
    const size_t regionPixels =
        static_cast<size_t>(w) *
        std::max<size_t>(1, static_cast<size_t>(h) * 3 / 4);
    constexpr size_t kMaxSamples = 262144;
    const size_t step = std::max<size_t>(
        1, (regionPixels - 1) / kMaxSamples + 1);

    std::vector<size_t> indices;
    std::vector<uint16_t> luminanceSamples;
    indices.reserve(std::min(regionPixels, kMaxSamples + 1));
    luminanceSamples.reserve(indices.capacity());
    for (size_t pixel = 0; pixel < regionPixels; pixel += step) {
        indices.push_back(pixel);
        luminanceSamples.push_back(luminanceAt(src, pixel));
    }
    if (indices.empty()) return false;

    const uint16_t low = percentileValue(luminanceSamples, 15, 100);
    const uint16_t high = percentileValue(luminanceSamples, 65, 100);
    std::array<std::vector<uint16_t>, 3> backgrounds;
    for (auto& channel : backgrounds) channel.reserve(indices.size() / 2);

    for (size_t sample = 0; sample < indices.size(); ++sample) {
        const uint16_t luminance = luminanceSamples[sample];
        if (luminance < low || luminance > high) continue;
        const size_t base = indices[sample] * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            backgrounds[channel].push_back(src[base + channel]);
        }
    }
    // Very small images or flat synthetic inputs can collapse the percentile
    // interval. Use all spatial samples rather than inventing a correction.
    if (backgrounds[0].size() < 16) {
        for (auto& channel : backgrounds) channel.clear();
        for (const size_t pixel : indices) {
            const size_t base = pixel * 3;
            for (size_t channel = 0; channel < 3; ++channel) {
                backgrounds[channel].push_back(src[base + channel]);
            }
        }
    }

    const std::array<int32_t, 3> medians = {
        static_cast<int32_t>(medianValue(std::move(backgrounds[0]))),
        static_cast<int32_t>(medianValue(std::move(backgrounds[1]))),
        static_cast<int32_t>(medianValue(std::move(backgrounds[2])))
    };
    const int32_t neutralTarget =
        static_cast<int32_t>((13933LL * medians[0] +
                              46871LL * medians[1] +
                              4732LL * medians[2]) >> 16);
    const std::array<int32_t, 3> offsets = {
        medians[0] - neutralTarget,
        medians[1] - neutralTarget,
        medians[2] - neutralTarget
    };

    std::vector<uint16_t> output(src.size());
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            const int32_t corrected =
                static_cast<int32_t>(src[base + channel]) - offsets[channel];
            output[base + channel] = static_cast<uint16_t>(
                std::clamp(corrected, 0, 65535));
        }
    }
    dst = std::move(output);
    return true;
}

bool AutoOptimizeEngine::stretchRgb(const std::vector<uint16_t>& src,
                                    int w, int h,
                                    std::vector<uint16_t>& dst) {
    size_t pixelCount = 0;
    if (!rgbPixelCount(src, w, h, pixelCount)) return false;

    std::vector<uint16_t> neutralized;
    if (!neutralizeBackgroundRgb(src, w, h, neutralized)) return false;

    constexpr size_t kMaxSamples = 262144;
    const size_t step =
        std::max<size_t>(1, (pixelCount - 1) / kMaxSamples + 1);
    std::vector<uint16_t> samples;
    samples.reserve(std::min(pixelCount, kMaxSamples + 1));
    for (size_t pixel = 0; pixel < pixelCount; pixel += step) {
        samples.push_back(luminanceAt(neutralized, pixel));
    }

    const uint16_t black = percentileValue(samples, 1, 1000);
    const uint16_t white = percentileValue(samples, 9995, 10000);
    if (white <= black + 32) {
        dst = std::move(neutralized);
        return true;
    }

    constexpr double kStretch = 3.0;
    const double denominator = std::asinh(kStretch);
    const double range = static_cast<double>(white) - black;
    std::vector<uint16_t> output(neutralized.size());

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        const std::array<double, 3> shifted = {
            std::max(0.0, static_cast<double>(neutralized[base]) - black),
            std::max(0.0, static_cast<double>(neutralized[base + 1]) - black),
            std::max(0.0, static_cast<double>(neutralized[base + 2]) - black)
        };
        const double inputLuminance =
            (13933.0 * shifted[0] + 46871.0 * shifted[1] +
             4732.0 * shifted[2]) / 65536.0;
        if (inputLuminance <= 0.0) {
            output[base] = output[base + 1] = output[base + 2] = 0;
            continue;
        }

        const double normalized =
            std::clamp(inputLuminance / range, 0.0, 1.0);
        const double outputLuminance =
            std::asinh(normalized * kStretch) / denominator * 65535.0;
        const double ratio = outputLuminance / inputLuminance;

        // Suppress unstable chroma in the deepest shadows while retaining full
        // star and Milky Way color once the signal clears roughly 5% of range.
        const double chromaRetention = std::clamp(normalized * 20.0, 0.0, 1.0);
        for (size_t channel = 0; channel < 3; ++channel) {
            const double scaled = shifted[channel] * ratio;
            const double value =
                outputLuminance + (scaled - outputLuminance) * chromaRetention;
            output[base + channel] = clampToUint16(value);
        }
    }

    dst = std::move(output);
    return true;
}

bool AutoOptimizeEngine::dehazeRgb(const std::vector<uint16_t>& src,
                                   int w, int h, int strength,
                                   std::vector<uint16_t>& dst) {
    size_t pixelCount = 0;
    if (!rgbPixelCount(src, w, h, pixelCount) || strength < 0) return false;
    if (strength == 0) {
        dst = src;
        return true;
    }

    std::vector<uint16_t> luminance(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        luminance[pixel] = luminanceAt(src, pixel);
    }
    std::vector<uint16_t> dehazed;
    if (!dehaze(luminance, w, h, std::min(strength, 100), dehazed)) {
        return false;
    }

    std::vector<uint16_t> output(src.size());
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const double original = luminance[pixel];
        const double ratio = original > 0.0
            ? std::min(16.0, static_cast<double>(dehazed[pixel]) / original)
            : 0.0;
        const size_t base = pixel * 3;
        output[base] = clampToUint16(src[base] * ratio);
        output[base + 1] = clampToUint16(src[base + 1] * ratio);
        output[base + 2] = clampToUint16(src[base + 2] * ratio);
    }
    dst = std::move(output);
    return true;
}

static void computeDarkChannel(const std::vector<float>& img, int w, int h, std::vector<float>& dark, int patchSize) {
    dark.resize(w * h);
    int half = patchSize / 2;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float minVal = 1.0f;
            for (int dy = -half; dy <= half; ++dy) {
                int yy = std::max(0, std::min(h - 1, y + dy));
                for (int dx = -half; dx <= half; ++dx) {
                    int xx = std::max(0, std::min(w - 1, x + dx));
                    minVal = std::min(minVal, img[yy * w + xx]);
                }
            }
            dark[y * w + x] = minVal;
        }
    }
}

static float estimateAtmosphericLight(const std::vector<float>& img, const std::vector<float>& dark, int w, int h) {
    int total = w * h;
    int topCount = std::max(1, static_cast<int>(total * 0.001));
    std::vector<std::pair<float, int>> darkIndexed;
    darkIndexed.reserve(total);
    for (int i = 0; i < total; ++i) {
        darkIndexed.emplace_back(dark[i], i);
    }
    std::partial_sort(darkIndexed.begin(), darkIndexed.begin() + topCount, darkIndexed.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });
    
    double sum = 0.0;
    for (int i = 0; i < topCount; ++i) {
        sum += img[darkIndexed[i].second];
    }
    return static_cast<float>(sum / topCount);
}

static void boxFilter(const std::vector<float>& src, std::vector<float>& dst, int w, int h, int r) {
    dst.resize(w * h);
    std::vector<float> temp(w * h);
    
    // Horizontal pass
    for (int y = 0; y < h; ++y) {
        float sum = 0.0f;
        for (int x = 0; x < r && x < w; ++x) {
            sum += src[y * w + x];
        }
        for (int x = 0; x < w; ++x) {
            int left = x - r - 1;
            int right = x + r;
            if (left >= 0) sum -= src[y * w + left];
            if (right < w) sum += src[y * w + right];
            int count = std::min(x + r, w - 1) - std::max(x - r, 0) + 1;
            temp[y * w + x] = sum / count;
        }
    }
    
    // Vertical pass
    for (int x = 0; x < w; ++x) {
        float sum = 0.0f;
        for (int y = 0; y < r && y < h; ++y) {
            sum += temp[y * w + x];
        }
        for (int y = 0; y < h; ++y) {
            int top = y - r - 1;
            int bottom = y + r;
            if (top >= 0) sum -= temp[top * w + x];
            if (bottom < h) sum += temp[bottom * w + x];
            int count = std::min(y + r, h - 1) - std::max(y - r, 0) + 1;
            dst[y * w + x] = sum / count;
        }
    }
}

void AutoOptimizeEngine::guidedFilter(const std::vector<float>& guide, const std::vector<float>& input,
                                      std::vector<float>& output, int w, int h, int r, float eps) {
    output.resize(w * h);
    std::vector<float> mean_I(w * h), mean_p(w * h), mean_Ip(w * h);
    std::vector<float> mean_II(w * h), a(w * h), b(w * h);
    
    boxFilter(guide, mean_I, w, h, r);
    boxFilter(input, mean_p, w, h, r);
    
    std::vector<float> Ip(w * h), II(w * h);
    for (int i = 0; i < w * h; ++i) {
        Ip[i] = guide[i] * input[i];
        II[i] = guide[i] * guide[i];
    }
    boxFilter(Ip, mean_Ip, w, h, r);
    boxFilter(II, mean_II, w, h, r);
    
    for (int i = 0; i < w * h; ++i) {
        float cov = mean_Ip[i] - mean_I[i] * mean_p[i];
        float var = mean_II[i] - mean_I[i] * mean_I[i];
        a[i] = cov / (var + eps);
        b[i] = mean_p[i] - a[i] * mean_I[i];
    }
    
    std::vector<float> mean_a(w * h), mean_b(w * h);
    boxFilter(a, mean_a, w, h, r);
    boxFilter(b, mean_b, w, h, r);
    
    for (int i = 0; i < w * h; ++i) {
        output[i] = mean_a[i] * guide[i] + mean_b[i];
    }
}

bool AutoOptimizeEngine::dehaze(const std::vector<uint16_t>& src, int w, int h,
                                 int strength, std::vector<uint16_t>& dst) {
    if (src.empty() || w <= 0 || h <= 0 || static_cast<int>(src.size()) != w * h) {
        return false;
    }
    if (strength <= 0) {
        dst = src;
        return true;
    }
    
    std::vector<float> img;
    normalizeToFloat(src, img, w, h);
    
    // Dark Channel Prior
    std::vector<float> dark;
    computeDarkChannel(img, w, h, dark, 15);
    
    // Estimate atmospheric light
    float A = estimateAtmosphericLight(img, dark, w, h);
    A = std::max(A, 0.001f);
    
    // Estimate transmission
    constexpr float omega = 0.95f;
    std::vector<float> t(w * h);
    for (int i = 0; i < w * h; ++i) {
        t[i] = 1.0f - omega * (dark[i] / A);
    }
    
    // Guided Filter refine transmission
    std::vector<float> t_refined;
    guidedFilter(img, t, t_refined, w, h, 40, 0.001f);
    
    // Recover image
    std::vector<float> result(w * h);
    for (int i = 0; i < w * h; ++i) {
        float t_clamped = std::max(t_refined[i], 0.1f);
        result[i] = (img[i] - A) / t_clamped + A;
        result[i] = std::max(0.0f, std::min(1.0f, result[i]));
    }
    
    // Blend based on strength
    float blend = static_cast<float>(strength) / 100.0f;
    for (int i = 0; i < w * h; ++i) {
        result[i] = img[i] * (1.0f - blend) + result[i] * blend;
    }
    
    convertToUint16(result, dst, w, h);
    return true;
}

static float computePercentile(const std::vector<uint16_t>& data, float percentile) {
    std::vector<uint16_t> sorted = data;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(percentile / 100.0f * (sorted.size() - 1));
    return static_cast<float>(sorted[idx]);
}

bool AutoOptimizeEngine::stretchCurve(const std::vector<uint16_t>& src, int w, int h,
                                       std::vector<uint16_t>& dst) {
    if (src.empty() || w <= 0 || h <= 0 || static_cast<int>(src.size()) != w * h) {
        return false;
    }
    
    float p1 = computePercentile(src, 1.0f);
    float p99 = computePercentile(src, 99.0f);
    
    if (p99 <= p1) {
        dst = src;
        return true;
    }
    
    float stretchFactor = 1.0f / (p99 - p1);
    float maxVal = 65535.0f;
    float asinhMax = std::asinh(maxVal * stretchFactor);
    
    dst.resize(w * h);
    float softClipStart = maxVal * 0.95f;
    float softClipRange = maxVal - softClipStart;
    
    for (int i = 0; i < w * h; ++i) {
        float input = static_cast<float>(src[i]);
        float shifted = std::max(0.0f, input - p1);
        float stretched = std::asinh(shifted * stretchFactor) / asinhMax * maxVal;
        
        // Soft-clipping for highlights
        if (stretched > softClipStart && softClipRange > 0.0f) {
            float excess = stretched - softClipStart;
            float compressed = softClipRange * (1.0f - std::exp(-excess / softClipRange));
            stretched = softClipStart + compressed;
        }
        
        dst[i] = static_cast<uint16_t>(std::max(0.0f, std::min(maxVal, stretched)) + 0.5f);
    }
    
    return true;
}
