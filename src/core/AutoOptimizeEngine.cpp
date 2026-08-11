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
    std::vector<uint16_t>& dst,
    const std::vector<uint8_t>* skyMask) {
    size_t pixelCount = 0;
    if (!rgbPixelCount(src, w, h, pixelCount) ||
        (skyMask && skyMask->size() != pixelCount)) {
        return false;
    }

    if (w >= 32 && h >= 32) {
        const int gridColumns = std::clamp(w / 256, 6, 16);
        const int gridRows = std::clamp(h / 256, 4, 12);
        const size_t gridSize =
            static_cast<size_t>(gridColumns) * gridRows;
        std::array<std::vector<uint16_t>, 3> grid;
        for (auto& channel : grid) channel.resize(gridSize);
        std::vector<uint8_t> validGridCell(gridSize, 0);

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
                        const size_t pixel =
                            static_cast<size_t>(y) * w + x;
                        // Use only the reliable interior of the sky. The
                        // feathered boundary is reserved for blending the
                        // resulting correction, not estimating it.
                        if (skyMask && (*skyMask)[pixel] < 224) continue;
                        const size_t base = pixel * 3;
                        for (size_t channel = 0; channel < 3; ++channel) {
                            samples[channel].push_back(src[base + channel]);
                        }
                    }
                }
                const size_t index =
                    static_cast<size_t>(gy) * gridColumns + gx;
                if (samples[0].size() < 16) continue;
                validGridCell[index] = 1;
                for (size_t channel = 0; channel < 3; ++channel) {
                    grid[channel][index] =
                        percentileValue(std::move(samples[channel]), 20, 100);
                }
            }
        }

        if (skyMask) {
            std::vector<size_t> validIndices;
            validIndices.reserve(gridSize);
            for (size_t index = 0; index < gridSize; ++index) {
                if (validGridCell[index]) validIndices.push_back(index);
            }
            // A malformed all-ground mask should not make a previously valid
            // finishing operation fail or let terrain drive a sky model.
            // Keep the input unchanged and let the linked curve proceed.
            if (validIndices.empty()) {
                dst = src;
                return true;
            }
            // Ground-only grid cells are never sampled. Extend the nearest
            // valid sky estimate into them so interpolation near the horizon
            // cannot fall toward zero or inherit a terrain-shaped low value.
            for (size_t index = 0; index < gridSize; ++index) {
                if (validGridCell[index]) continue;
                const int gx = static_cast<int>(index % gridColumns);
                const int gy = static_cast<int>(index / gridColumns);
                size_t nearest = validIndices.front();
                int nearestDistance = std::numeric_limits<int>::max();
                for (size_t candidate : validIndices) {
                    const int candidateX =
                        static_cast<int>(candidate % gridColumns);
                    const int candidateY =
                        static_cast<int>(candidate / gridColumns);
                    const int dx = gx - candidateX;
                    const int dy = gy - candidateY;
                    const int distance = dx * dx + dy * dy;
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        nearest = candidate;
                    }
                }
                for (size_t channel = 0; channel < 3; ++channel) {
                    grid[channel][index] = grid[channel][nearest];
                }
            }
        } else {
            std::fill(validGridCell.begin(), validGridCell.end(), 1);
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

        std::vector<uint16_t> validBackgroundLuminances;
        validBackgroundLuminances.reserve(gridSize);
        for (size_t index = 0; index < gridSize; ++index) {
            if (!validGridCell[index]) continue;
            validBackgroundLuminances.push_back(clampToUint16(
                (13933.0 * smoothGrid[0][index] +
                 46871.0 * smoothGrid[1][index] +
                 4732.0 * smoothGrid[2][index]) / 65536.0));
        }
        const double luminanceTarget = percentileValue(
            std::move(validBackgroundLuminances), 20, 100);
        // Spread the transition across a meaningful part of the low signal
        // range. This replaces the old hard max(0, x) contour with a smooth,
        // channel-linked shoulder.
        const double luminanceKnee = std::max(256.0, luminanceTarget * 0.15);

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
                std::array<double, 3> model = {};
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
                    model[channel] =
                        top * (1.0 - yFraction) + bottom * yFraction;
                }

                // Remove only the chromatic component of the local background
                // model. The previous max(0, model - globalTarget) changed its
                // derivative where each channel crossed the target; after a
                // strong stretch those three different zero contours became
                // visible colored rings. Centering RGB around the model's own
                // luminance is signed and continuous, and its weighted offsets
                // sum to zero, so real airglow and light-pollution brightness
                // gradients remain intact.
                const double modelLuminance =
                    (13933.0 * model[0] + 46871.0 * model[1] +
                     4732.0 * model[2]) / 65536.0;
                const double luminanceDelta =
                    modelLuminance - luminanceTarget;
                const double luminanceCorrection = 0.8 *
                    (0.5 * (luminanceDelta + std::sqrt(
                         luminanceDelta * luminanceDelta +
                         luminanceKnee * luminanceKnee)) -
                     0.5 * luminanceKnee);
                double opacity = 1.0;
                if (skyMask) {
                    const double t = (*skyMask)[
                        static_cast<size_t>(y) * w + x] / 255.0;
                    // Ease into and out of the feathered boundary. A
                    // conservative strength retains real atmospheric glow
                    // instead of forcing a nightscape horizon to neutral.
                    constexpr double kNightscapeStrength = 0.6;
                    opacity = t * t * (3.0 - 2.0 * t) *
                        kNightscapeStrength;
                }
                for (size_t channel = 0; channel < 3; ++channel) {
                    const double chromaOffset =
                        model[channel] - modelLuminance;
                    output[base + channel] = clampToUint16(
                        src[base + channel] -
                        (chromaOffset + luminanceCorrection) * opacity);
                }
            }
        }
        dst = std::move(output);
        return true;
    }

    // Prefer the upper 75% of a nightscape so a dark foreground does not drive
    // sky color estimation. This is also harmless for all-sky/deep-sky frames.
    const size_t regionPixels = skyMask
        ? pixelCount
        : static_cast<size_t>(w) *
              std::max<size_t>(1, static_cast<size_t>(h) * 3 / 4);
    constexpr size_t kMaxSamples = 262144;
    const size_t step = std::max<size_t>(
        1, (regionPixels - 1) / kMaxSamples + 1);

    std::vector<size_t> indices;
    std::vector<uint16_t> luminanceSamples;
    indices.reserve(std::min(regionPixels, kMaxSamples + 1));
    luminanceSamples.reserve(indices.capacity());
    for (size_t pixel = 0; pixel < regionPixels; pixel += step) {
        if (skyMask && (*skyMask)[pixel] < 224) continue;
        indices.push_back(pixel);
        luminanceSamples.push_back(luminanceAt(src, pixel));
    }
    if (indices.empty()) {
        dst = src;
        return true;
    }

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
        double opacity = 1.0;
        if (skyMask) {
            const double t = (*skyMask)[pixel] / 255.0;
            constexpr double kNightscapeStrength = 0.6;
            opacity = t * t * (3.0 - 2.0 * t) * kNightscapeStrength;
        }
        for (size_t channel = 0; channel < 3; ++channel) {
            output[base + channel] = clampToUint16(
                static_cast<double>(src[base + channel]) -
                offsets[channel] * opacity);
        }
    }
    dst = std::move(output);
    return true;
}

bool AutoOptimizeEngine::restoreModifiedCameraColorRgb(
    const std::vector<uint16_t>& src, int w, int h,
    std::vector<uint16_t>& dst, ModifiedCameraColorStats* outputStats,
    const std::vector<uint8_t>* skyMask,
    const ModifiedCameraColorOptions& options) {
    ModifiedCameraColorStats stats;
    size_t pixelCount = 0;
    if (!rgbPixelCount(src, w, h, pixelCount) ||
        (skyMask && skyMask->size() != pixelCount) ||
        options.strength < 0 || options.strength > 100 ||
        (options.neutralMode == ModifiedCameraNeutralMode::ManualPoint &&
         (!std::isfinite(options.manualPointX) ||
          !std::isfinite(options.manualPointY) ||
          options.manualPointX < 0.0 || options.manualPointX > 1.0 ||
          options.manualPointY < 0.0 || options.manualPointY > 1.0))) {
        return false;
    }
    if (options.strength == 0) {
        dst = src;
        if (outputStats) *outputStats = stats;
        return true;
    }

    std::array<std::vector<uint16_t>, 3> neutralChannels;
    if (options.neutralMode == ModifiedCameraNeutralMode::ManualPoint) {
        stats.usedManualPoint = true;
        stats.samplePointX = options.manualPointX;
        stats.samplePointY = options.manualPointY;
        const auto pointToPixel = [](double coordinate, int extent) {
            const double bounded = std::clamp(
                coordinate, 0.0, std::nextafter(1.0, 0.0));
            return std::min(
                extent - 1, static_cast<int>(std::floor(bounded * extent)));
        };
        const int centerX = pointToPixel(options.manualPointX, w);
        const int centerY = pointToPixel(options.manualPointY, h);
        // A robust neighborhood behaves like a multi-pixel Camera Raw
        // eyedropper and maps consistently between preview resolutions.
        const int radius = std::clamp(
            static_cast<int>(std::lround(std::min(w, h) * 0.004)), 2, 64);
        const int radiusSquared = radius * radius;
        for (int y = std::max(0, centerY - radius);
             y <= std::min(h - 1, centerY + radius); ++y) {
            for (int x = std::max(0, centerX - radius);
                 x <= std::min(w - 1, centerX + radius); ++x) {
                const int dx = x - centerX;
                const int dy = y - centerY;
                if (dx * dx + dy * dy > radiusSquared) continue;
                const size_t pixel = static_cast<size_t>(y) * w + x;
                const size_t base = pixel * 3;
                if (luminanceAt(src, pixel) < 16 ||
                    std::max({src[base], src[base + 1], src[base + 2]}) >=
                        64500) {
                    continue;
                }
                for (size_t channel = 0; channel < 3; ++channel) {
                    neutralChannels[channel].push_back(src[base + channel]);
                }
            }
        }
        if (neutralChannels[0].size() < 9) return false;
    } else {
        constexpr size_t kMaximumSamples = 262144;
        const size_t step = std::max<size_t>(
            1, (pixelCount - 1) / kMaximumSamples + 1);
        std::vector<size_t> candidateIndices;
        std::vector<uint16_t> candidateLuminance;
        candidateIndices.reserve(std::min(pixelCount, kMaximumSamples + 1));
        candidateLuminance.reserve(candidateIndices.capacity());

        // Without a sky mask, omit the lowest quarter of a conventional
        // nightscape. Mountains, buildings and vegetation are poor gray-card
        // substitutes and otherwise dominate a wide-angle frame.
        const int samplingHeight = skyMask ? h : std::max(1, h * 3 / 4);
        for (int y = 0; y < samplingHeight; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t pixel = static_cast<size_t>(y) * w + x;
                if (pixel % step != 0 ||
                    (skyMask && (*skyMask)[pixel] < 160)) {
                    continue;
                }
                candidateIndices.push_back(pixel);
                candidateLuminance.push_back(luminanceAt(src, pixel));
            }
        }
        if (candidateIndices.size() < 64) return false;

        const uint16_t low = percentileValue(candidateLuminance, 12, 100);
        const uint16_t high = percentileValue(candidateLuminance, 58, 100);
        for (auto& channel : neutralChannels) {
            channel.reserve(candidateIndices.size() / 2);
        }
        for (size_t sample = 0; sample < candidateIndices.size(); ++sample) {
            const uint16_t luminance = candidateLuminance[sample];
            if (luminance < low || luminance > high || luminance < 16) continue;
            const size_t base = candidateIndices[sample] * 3;
            // Exclude clipped stars and artificial lights even when a dense
            // field pushes them below the global luminance cutoff.
            if (std::max({src[base], src[base + 1], src[base + 2]}) >= 64500) {
                continue;
            }
            for (size_t channel = 0; channel < 3; ++channel) {
                neutralChannels[channel].push_back(src[base + channel]);
            }
        }
        if (neutralChannels[0].size() < 64) return false;
    }

    stats.sampleCount = neutralChannels[0].size();
    for (size_t channel = 0; channel < 3; ++channel) {
        stats.neutralSample[channel] =
            medianValue(std::move(neutralChannels[channel]));
        if (stats.neutralSample[channel] < 16) return false;
    }

    const double neutralLuminance =
        (13933.0 * stats.neutralSample[0] +
         46871.0 * stats.neutralSample[1] +
         4732.0 * stats.neutralSample[2]) / 65536.0;
    if (!std::isfinite(neutralLuminance) || neutralLuminance <= 0.0) {
        return false;
    }
    constexpr double kMinimumGain = 0.35;
    constexpr double kMaximumGain = 2.80;
    const double mix = options.strength / 100.0;
    for (size_t channel = 0; channel < 3; ++channel) {
        const double targetGain = std::clamp(
            neutralLuminance / stats.neutralSample[channel],
            kMinimumGain, kMaximumGain);
        stats.gains[channel] = 1.0 + (targetGain - 1.0) * mix;
    }

    std::vector<uint16_t> output(src.size());
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            const double corrected = src[base + channel] * stats.gains[channel];
            if (corrected > 65535.0) ++stats.clippedChannelValues;
            output[base + channel] = clampToUint16(corrected);
        }
    }
    stats.applied = true;
    dst = std::move(output);
    if (outputStats) *outputStats = stats;
    return true;
}

bool AutoOptimizeEngine::enhanceGroundDetail(
    std::vector<uint16_t>& image, int w, int h,
    const std::vector<uint8_t>& skyMask, int strength) {
    size_t pixelCount = 0;
    if (!rgbPixelCount(image, w, h, pixelCount) ||
        skyMask.size() != pixelCount || strength < 0) {
        return false;
    }
    if (strength == 0) return true;
    strength = std::min(strength, 100);

    std::vector<uint16_t> luminance(pixelCount);
    std::vector<uint16_t> horizontal(pixelCount);
    std::vector<int> horizon(static_cast<size_t>(w), h);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        luminance[pixel] = luminanceAt(image, pixel);
    }
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            if (skyMask[static_cast<size_t>(y) * w + x] < 128) {
                horizon[static_cast<size_t>(x)] = y;
                break;
            }
        }
    }

    // Binomial [1 4 6 4 1] is a compact Gaussian approximation. An atrous
    // step lets the same kernel recover both fine texture and medium-scale
    // clarity without allocating additional full-resolution planes.
    constexpr std::array<uint32_t, 5> kernel = {1, 4, 6, 4, 1};
    const auto applyDetailPass = [&](int sampleStep, double amountScale,
                                     double thresholdFloor,
                                     double thresholdFraction,
                                     double minimumRatio,
                                     double maximumRatio,
                                     bool favorDistantGround) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                uint32_t sum = 0;
                for (int k = -2; k <= 2; ++k) {
                    const int sampleX =
                        std::clamp(x + k * sampleStep, 0, w - 1);
                    sum += kernel[static_cast<size_t>(k + 2)] *
                        luminance[static_cast<size_t>(y) * w + sampleX];
                }
                horizontal[static_cast<size_t>(y) * w + x] =
                    static_cast<uint16_t>((sum + 8) / 16);
            }
        }

        const double amount = strength / 100.0 * amountScale;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const size_t pixel = static_cast<size_t>(y) * w + x;
                const double groundWeight =
                    (255.0 - skyMask[pixel]) / 255.0;
                if (groundWeight <= 0.0) continue;
                double distanceWeight = 1.0;
                if (favorDistantGround) {
                    const int boundary = horizon[static_cast<size_t>(x)];
                    const double depth = boundary < h
                        ? std::clamp(
                              static_cast<double>(y - boundary) /
                                  std::max(1, h - boundary),
                              0.0, 1.0)
                        : 0.0;
                    // Distant ridges need medium-scale contrast more than the
                    // already textured near foreground. Tapering toward the
                    // bottom also limits amplified grass and sensor noise.
                    distanceWeight = 1.0 - 0.6 * depth;
                }
                uint32_t sum = 0;
                for (int k = -2; k <= 2; ++k) {
                    const int sampleY =
                        std::clamp(y + k * sampleStep, 0, h - 1);
                    sum += kernel[static_cast<size_t>(k + 2)] *
                        horizontal[static_cast<size_t>(sampleY) * w + x];
                }
                const double blurred = (sum + 8) / 16.0;
                const double original = luminance[pixel];
                const double detail = original - blurred;
                const double threshold = std::max(
                    thresholdFloor, original * thresholdFraction);
                const double retained = std::copysign(
                    std::max(0.0, std::abs(detail) - threshold), detail);
                const double current = luminanceAt(image, pixel);
                const double target = std::clamp(
                    current + retained * amount * groundWeight * distanceWeight,
                    0.0, 65535.0);
                if (current <= 1.0 || target == current) continue;
                const double ratio = std::clamp(
                    target / current, minimumRatio, maximumRatio);
                const size_t base = pixel * 3;
                for (size_t channel = 0; channel < 3; ++channel) {
                    image[base + channel] = clampToUint16(
                        image[base + channel] * ratio);
                }
            }
        }
    };

    applyDetailPass(1, 1.15, 24.0, 0.0015, 0.80, 1.25, false);
    // Step 4 corresponds to roughly 4 px sigma at full resolution. This layer
    // makes hazy ridges and buildings read at normal viewing size; the stronger
    // threshold and tighter ratio limits avoid turning sensor noise into grit.
    applyDetailPass(4, 2.5, 64.0, 0.0030, 0.86, 1.18, true);
    return true;
}

bool AutoOptimizeEngine::stretchRgb(const std::vector<uint16_t>& src,
                                    int w, int h,
                                    std::vector<uint16_t>& dst,
                                    const std::vector<uint8_t>* skyMask) {
    size_t pixelCount = 0;
    if (!rgbPixelCount(src, w, h, pixelCount) ||
        (skyMask && skyMask->size() != pixelCount)) {
        return false;
    }

    std::vector<uint16_t> neutralized;
    if (!neutralizeBackgroundRgb(
            src, w, h, neutralized, skyMask)) {
        return false;
    }

    constexpr size_t kMaxSamples = 262144;
    const size_t step =
        std::max<size_t>(1, (pixelCount - 1) / kMaxSamples + 1);
    std::vector<uint16_t> samples;
    samples.reserve(std::min(pixelCount, kMaxSamples + 1));
    for (size_t pixel = 0; pixel < pixelCount; pixel += step) {
        samples.push_back(luminanceAt(neutralized, pixel));
    }

    const uint16_t black = percentileValue(samples, 1, 1000);
    const uint16_t background = percentileValue(samples, 1, 2);
    const uint16_t highlight = percentileValue(samples, 9999, 10000);
    if (highlight <= black + 32 || background <= black) {
        dst = std::move(neutralized);
        return true;
    }

    // Solve an asinh curve whose robust background maps near 16% while the
    // 99.99th percentile retains highlight headroom. The previous fixed curve
    // mapped a typical 25% input percentile to roughly 39%, washing out normal
    // nightscapes and clipping too many stars at the 99.95th percentile.
    constexpr double kTargetBackground = 0.16;
    constexpr double kTargetHighlight = 0.85;
    const double backgroundSignal =
        static_cast<double>(background) - black;
    const double highlightSignal =
        static_cast<double>(highlight) - black;
    const double whiteSignal = std::max(
        highlightSignal,
        backgroundSignal / kTargetBackground * 1.05);
    const double normalizedBackgroundTarget =
        kTargetBackground / kTargetHighlight;
    auto mappedBackground = [&](double softening) {
        return std::asinh(backgroundSignal / softening) /
            std::asinh(whiteSignal / softening);
    };
    double lowSoftening = std::max(1e-6, whiteSignal * 1e-9);
    double highSoftening = whiteSignal * 1e6;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double middle = std::sqrt(lowSoftening * highSoftening);
        if (mappedBackground(middle) > normalizedBackgroundTarget) {
            lowSoftening = middle;
        } else {
            highSoftening = middle;
        }
    }
    const double softening = std::sqrt(lowSoftening * highSoftening);
    const double denominator = std::asinh(whiteSignal / softening);
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
            std::clamp(inputLuminance / whiteSignal, 0.0, 1.0);
        // whiteSignal maps to 85%, but brighter samples continue into the
        // remaining headroom instead of all collapsing onto one flat 85%
        // plateau. This matters for bright nebula cores such as M42.
        constexpr double kOutputLuminanceCeiling = 0.985;
        const double mappedLuminance =
            std::asinh(inputLuminance / softening) / denominator *
            kTargetHighlight;
        const double outputLuminance =
            std::min(kOutputLuminanceCeiling, mappedLuminance) * 65535.0;
        const double ratio = outputLuminance / inputLuminance;

        // Suppress unstable chroma in the deepest shadows while retaining full
        // star and Milky Way color once the signal clears roughly 5% of range.
        double chromaRetention = std::clamp(normalized * 20.0, 0.0, 1.0);
        const std::array<double, 3> scaled = {
            shifted[0] * ratio,
            shifted[1] * ratio,
            shifted[2] * ratio
        };
        // A linked luminance curve can still push one highly saturated color
        // channel beyond 16-bit even when luminance has headroom. First form
        // the shadow-stabilized RGB triplet, then begin with one common
        // highlight scale. A soft shoulder above 90% preserves hue and remains
        // monotonic; a bounded second step below handles colors that cannot fit
        // without losing too much luminance.
        std::array<double, 3> candidate = {};
        for (size_t channel = 0; channel < 3; ++channel) {
            candidate[channel] =
                outputLuminance +
                (scaled[channel] - outputLuminance) * chromaRetention;
        }
        constexpr double kHighlightKnee = 65535.0 * 0.90;
        constexpr double kChannelCeiling = 65535.0 * 0.995;
        const double maximumChannel = *std::max_element(
            candidate.begin(), candidate.end());
        double huePreservingScale = 1.0;
        if (maximumChannel > kHighlightKnee) {
            const double shoulder = kChannelCeiling - kHighlightKnee;
            const double compressedMaximum = kHighlightKnee + shoulder *
                (1.0 - std::exp(
                    -(maximumChannel - kHighlightKnee) / shoulder));
            huePreservingScale = compressedMaximum / maximumChannel;
        }

        // Fully hue-preserving compression can make an extremely saturated
        // red core darker than its neutral halo: high luminance and pure red
        // cannot both fit inside linear sRGB. Retain at least 65% of the mapped
        // luminance, then reduce only the remaining out-of-gamut chroma. This
        // avoids both a dark center and the nearly white result produced by
        // preserving 100% luminance at any cost.
        constexpr double kMinimumLuminanceRetention = 0.65;
        const double baseScale = std::max(
            huePreservingScale, kMinimumLuminanceRetention);
        for (double& channel : candidate) channel *= baseScale;
        const double retainedLuminance = outputLuminance * baseScale;
        double finalChromaRetention = 1.0;
        for (double channel : candidate) {
            const double delta = channel - retainedLuminance;
            if (delta > 0.0) {
                finalChromaRetention = std::min(
                    finalChromaRetention,
                    std::max(0.0, kChannelCeiling - retainedLuminance) /
                        delta);
            } else if (delta < 0.0) {
                finalChromaRetention = std::min(
                    finalChromaRetention, retainedLuminance / -delta);
            }
        }
        finalChromaRetention = std::clamp(
            finalChromaRetention, 0.0, 1.0);
        for (size_t channel = 0; channel < 3; ++channel) {
            output[base + channel] = clampToUint16(
                retainedLuminance +
                (candidate[channel] - retainedLuminance) *
                    finalChromaRetention);
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
