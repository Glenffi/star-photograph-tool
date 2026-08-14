#include "StarReducer.h"

#include "StarDetector.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>

namespace {

uint16_t median(std::vector<uint16_t>& values) {
    if (values.empty()) return 0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const uint16_t lower =
        *std::max_element(values.begin(), values.begin() + middle);
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(lower) + values[middle]) / 2);
}

uint16_t rgbLuminance(const uint16_t* rgb) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(rgb[0]) * 13933 +
         static_cast<uint32_t>(rgb[1]) * 46871 +
         static_cast<uint32_t>(rgb[2]) * 4732) /
        65536);
}

uint16_t positiveLayerLuminance(const int32_t* rgb) {
    const int64_t weighted =
        static_cast<int64_t>(rgb[0]) * 13933 +
        static_cast<int64_t>(rgb[1]) * 46871 +
        static_cast<int64_t>(rgb[2]) * 4732;
    return static_cast<uint16_t>(std::clamp<int64_t>(
        weighted / 65536, 0, 65535));
}

struct BackgroundColor {
    bool valid = false;
    uint16_t luminance = 0;
    std::array<uint16_t, 3> rgb = {};
};

BackgroundColor localBackground(const std::vector<uint16_t>& luminance,
                                const std::vector<uint16_t>& rgb,
                                int width, int height,
                                const StarPoint& star,
                                double innerRadius, double outerRadius) {
    const int radius = static_cast<int>(std::ceil(outerRadius));
    const int centerX = static_cast<int>(std::lround(star.x));
    const int centerY = static_cast<int>(std::lround(star.y));
    const int x0 = std::max(0, centerX - radius);
    const int x1 = std::min(width - 1, centerX + radius);
    const int y0 = std::max(0, centerY - radius);
    const int y1 = std::min(height - 1, centerY + radius);
    const double innerSquared = innerRadius * innerRadius;
    const double outerSquared = outerRadius * outerRadius;
    std::vector<uint16_t> luminanceSamples;
    std::array<std::vector<uint16_t>, 3> channelSamples;
    const size_t capacity = static_cast<size_t>(radius * radius * 3);
    luminanceSamples.reserve(capacity);
    for (auto& samples : channelSamples) samples.reserve(capacity);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x - star.x;
            const double dy = y - star.y;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared < innerSquared ||
                distanceSquared > outerSquared) {
                continue;
            }
            const size_t pixel = static_cast<size_t>(y) * width + x;
            luminanceSamples.push_back(luminance[pixel]);
            for (int channel = 0; channel < 3; ++channel) {
                channelSamples[channel].push_back(
                    rgb[pixel * 3 + channel]);
            }
        }
    }

    BackgroundColor background;
    if (luminanceSamples.size() < 8) return background;
    background.valid = true;
    background.luminance = median(luminanceSamples);
    for (int channel = 0; channel < 3; ++channel) {
        background.rgb[channel] = median(channelSamples[channel]);
    }
    return background;
}

double featheredFootprint(double distance, double radius) {
    if (distance >= radius) return 0.0;
    const double feather = std::min(1.25, radius * 0.45);
    const double innerRadius = std::max(0.0, radius - feather);
    if (distance <= innerRadius) return 1.0;
    const double t = std::clamp(
        (radius - distance) / std::max(0.01, feather), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double smoothstep01(double value) {
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

double positiveRgbLuminance(const std::array<double, 3>& rgb) {
    return rgb[0] * (13933.0 / 65536.0) +
           rgb[1] * (46871.0 / 65536.0) +
           rgb[2] * (4732.0 / 65536.0);
}

double medianDouble(std::vector<double>& values) {
    if (values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const double upper = values[middle];
    std::nth_element(values.begin(), values.begin() + middle - 1,
                     values.begin() + middle);
    return (values[middle - 1] + upper) * 0.5;
}

int32_t blendLayerSample(int32_t first, int32_t second, double amount);

size_t reduceStarColorFringes(
        const std::vector<uint16_t>& source,
        const std::vector<uint16_t>& starless,
        std::vector<int32_t>& starLayer,
        int width, int height,
        const std::vector<StarPoint>& stars,
        int strength,
        const std::vector<uint8_t>* processingMask) {
    const size_t pixelCount = static_cast<size_t>(width) * height;
    std::vector<uint8_t> appliedWeights(pixelCount, 0);
    size_t affectedPixels = 0;
    const double maximumCorrection =
        0.85 * smoothstep01(strength / 100.0);

    // Every proposal is derived from the immutable source/starless pair. When
    // footprints overlap, the strongest proposal wins, so traversal order does
    // not repeatedly desaturate the same pixel.
    for (const StarPoint& star : stars) {
        if (star.fwhm < 1.0 || star.fwhm > 8.0 ||
            star.ellipticity > 0.60) {
            continue;
        }
        const double coreRadius = std::clamp(star.fwhm * 0.35, 1.25, 2.5);
        const double outerRadius = std::clamp(star.fwhm * 1.5, 2.0, 12.0);
        if (outerRadius <= coreRadius + 0.5) continue;

        const int centerX = static_cast<int>(std::lround(star.x));
        const int centerY = static_cast<int>(std::lround(star.y));
        const int radius = static_cast<int>(std::ceil(outerRadius));
        const int x0 = std::max(0, centerX - radius);
        const int x1 = std::min(width - 1, centerX + radius);
        const int y0 = std::max(0, centerY - radius);
        const int y1 = std::min(height - 1, centerY + radius);

        std::array<std::vector<double>, 3> coreShares;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (std::hypot(x - star.x, y - star.y) > coreRadius) continue;
                const size_t pixel = static_cast<size_t>(y) * width + x;
                if (processingMask && (*processingMask)[pixel] < 128) continue;
                bool saturated = false;
                double signalTotal = 0.0;
                std::array<double, 3> signal = {};
                for (int channel = 0; channel < 3; ++channel) {
                    const size_t index = pixel * 3 + channel;
                    saturated = saturated || source[index] >= 65000;
                    signal[channel] = std::max(
                        0, static_cast<int>(source[index]) -
                               static_cast<int>(starless[index]));
                    signalTotal += signal[channel];
                }
                if (saturated || signalTotal < 100.0) continue;
                for (int channel = 0; channel < 3; ++channel) {
                    coreShares[channel].push_back(
                        signal[channel] / signalTotal);
                }
            }
        }
        if (coreShares[0].size() < 4) continue;

        std::array<double, 3> coreShare = {};
        double coreShareTotal = 0.0;
        for (int channel = 0; channel < 3; ++channel) {
            coreShare[channel] = medianDouble(coreShares[channel]);
            coreShareTotal += coreShare[channel];
        }
        if (coreShareTotal <= 1e-9) continue;
        for (double& value : coreShare) value /= coreShareTotal;
        const double coreShareLuminance = positiveRgbLuminance(coreShare);
        if (coreShareLuminance <= 1e-9) continue;
        const double coreMaximum = *std::max_element(
            coreShare.begin(), coreShare.end());
        const double coreMinimum = *std::min_element(
            coreShare.begin(), coreShare.end());
        const double coreSaturation = coreMaximum > 0.0
            ? (coreMaximum - coreMinimum) / coreMaximum : 0.0;
        const double peakRadius = std::clamp(
            star.fwhm * 0.85, coreRadius + 0.25, outerRadius - 0.25);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double distance = std::hypot(x - star.x, y - star.y);
                if (distance <= coreRadius || distance >= outerRadius) continue;
                const size_t pixel = static_cast<size_t>(y) * width + x;

                std::array<int32_t, 3> signedSignal = {};
                std::array<double, 3> positiveSignal = {};
                double signalTotal = 0.0;
                for (int channel = 0; channel < 3; ++channel) {
                    const size_t index = pixel * 3 + channel;
                    signedSignal[channel] =
                        static_cast<int32_t>(source[index]) -
                        static_cast<int32_t>(starless[index]);
                    positiveSignal[channel] =
                        std::max(0, signedSignal[channel]);
                    signalTotal += positiveSignal[channel];
                }
                if (signalTotal < 24.0) continue;

                std::array<double, 3> signalShare = {};
                double maximumShare = 0.0;
                double minimumShare = 1.0;
                double maximumDeviation = 0.0;
                for (int channel = 0; channel < 3; ++channel) {
                    signalShare[channel] =
                        positiveSignal[channel] / signalTotal;
                    maximumShare = std::max(maximumShare, signalShare[channel]);
                    minimumShare = std::min(minimumShare, signalShare[channel]);
                    maximumDeviation = std::max(
                        maximumDeviation,
                        std::abs(signalShare[channel] - coreShare[channel]));
                }
                const double saturation = maximumShare > 0.0
                    ? (maximumShare - minimumShare) / maximumShare : 0.0;
                const double saturationExcess = saturation - coreSaturation;
                if (maximumDeviation <= 0.025 || saturationExcess <= 0.01) {
                    continue;
                }

                const double rise = smoothstep01(
                    (distance - coreRadius) /
                    std::max(0.01, peakRadius - coreRadius));
                const double fall = smoothstep01(
                    (outerRadius - distance) /
                    std::max(0.01, outerRadius - peakRadius));
                const double chromaWeight =
                    smoothstep01((maximumDeviation - 0.025) / 0.12) *
                    smoothstep01((saturationExcess - 0.01) / 0.12);
                const double maskWeight = processingMask
                    ? (*processingMask)[pixel] / 255.0 : 1.0;
                const double amount = maximumCorrection *
                    std::min(rise, fall) * chromaWeight * maskWeight;
                const uint8_t quantizedWeight = static_cast<uint8_t>(
                    std::clamp(std::lround(amount * 255.0), 0L, 255L));
                if (quantizedWeight <= appliedWeights[pixel]) continue;

                const double signalLuminance =
                    positiveRgbLuminance(positiveSignal);
                std::array<int32_t, 3> proposal = {};
                for (int channel = 0; channel < 3; ++channel) {
                    const double target = signalLuminance *
                        coreShare[channel] / coreShareLuminance;
                    proposal[channel] =
                        blendLayerSample(signedSignal[channel],
                                         static_cast<int32_t>(std::lround(target)),
                                         amount);
                }
                std::array<double, 3> proposalPositive = {};
                for (int channel = 0; channel < 3; ++channel) {
                    proposalPositive[channel] =
                        std::max(0, proposal[channel]);
                }
                const double proposalLuminance =
                    positiveRgbLuminance(proposalPositive);
                const double luminanceScale = proposalLuminance > 1e-9
                    ? signalLuminance / proposalLuminance : 0.0;
                for (int channel = 0; channel < 3; ++channel) {
                    starLayer[pixel * 3 + channel] =
                        static_cast<int32_t>(std::lround(
                            proposal[channel] * luminanceScale));
                }
                if (appliedWeights[pixel] == 0) ++affectedPixels;
                appliedWeights[pixel] = quantizedWeight;
            }
        }
    }
    return affectedPixels;
}

double sampleLuminanceBilinear(const std::vector<uint16_t>& luminance,
                               int width, int height,
                               double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const double tx = x - x0;
    const double ty = y - y0;
    const auto at = [&](int sampleX, int sampleY) {
        return static_cast<double>(
            luminance[static_cast<size_t>(sampleY) * width + sampleX]);
    };
    const double top = at(x0, y0) * (1.0 - tx) + at(x1, y0) * tx;
    const double bottom = at(x0, y1) * (1.0 - tx) + at(x1, y1) * tx;
    return top * (1.0 - ty) + bottom * ty;
}

void erodeRoundLuminanceSubpixel(
        const std::vector<uint16_t>& luminance,
        int width, int height, double radius,
        std::vector<uint16_t>& eroded) {
    if (radius <= 1e-6) {
        eroded = luminance;
        return;
    }

    eroded.assign(luminance.size(), 0);
    constexpr int sampleCount = 16;
    constexpr double twoPi = 6.28318530717958647692;
    std::array<double, sampleCount> offsetX = {};
    std::array<double, sampleCount> offsetY = {};
    for (int sample = 0; sample < sampleCount; ++sample) {
        const double angle = twoPi * sample / sampleCount;
        offsetX[sample] = std::cos(angle) * radius;
        offsetY[sample] = std::sin(angle) * radius;
    }

    // The star layer is zero outside detected footprints, so the branch keeps
    // full-frame work bounded to a cheap scan while the subpixel samples are
    // evaluated only where a detected star actually contributed signal.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            if (luminance[pixel] == 0) continue;
            double darkest = luminance[pixel];
            for (int sample = 0; sample < sampleCount; ++sample) {
                darkest = std::min(
                    darkest,
                    sampleLuminanceBilinear(
                        luminance, width, height,
                        x + offsetX[sample], y + offsetY[sample]));
            }
            eroded[pixel] = static_cast<uint16_t>(std::clamp(
                std::lround(darkest), 0L, 65535L));
        }
    }
}

bool hasSaturatedCore(const std::vector<uint16_t>& rgb,
                      int width, int height,
                      const StarPoint& star) {
    const int centerX = std::clamp(
        static_cast<int>(std::lround(star.x)), 0, width - 1);
    const int centerY = std::clamp(
        static_cast<int>(std::lround(star.y)), 0, height - 1);
    const int radius = std::clamp(
        static_cast<int>(std::ceil(star.fwhm * 0.5)), 1, 5);
    for (int y = std::max(0, centerY - radius);
         y <= std::min(height - 1, centerY + radius); ++y) {
        for (int x = std::max(0, centerX - radius);
             x <= std::min(width - 1, centerX + radius); ++x) {
            const size_t index =
                (static_cast<size_t>(y) * width + x) * 3;
            if (rgb[index] >= 65000 || rgb[index + 1] >= 65000 ||
                rgb[index + 2] >= 65000) {
                return true;
            }
        }
    }
    return false;
}

int32_t blendLayerSample(int32_t first, int32_t second, double amount) {
    return static_cast<int32_t>(std::lround(
        first * (1.0 - amount) + second * amount));
}

uint16_t blendImageSample(uint16_t first, uint16_t second, double amount) {
    return static_cast<uint16_t>(std::lround(
        first * (1.0 - amount) + second * amount));
}

bool processStars(std::vector<uint16_t>& image, int width, int height,
                  int strength, StarReductionStats* stats,
                  const std::vector<uint8_t>* processingMask,
                  bool reduceSize, bool applyDefringe) {
    if (stats) *stats = {};
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536 ||
        width > INT_MAX / height) {
        return false;
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (pixelCount > std::numeric_limits<size_t>::max() / 3 ||
        image.size() != pixelCount * 3 || strength < 0 ||
        (processingMask && processingMask->size() != pixelCount)) {
        return false;
    }
    if (strength == 0) return true;
    strength = std::min(strength, 100);

    std::vector<uint16_t> originalLuminance(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        originalLuminance[pixel] = rgbLuminance(&image[pixel * 3]);
    }

    StarDetector detector;
    std::vector<StarPoint> stars;
    DetectionOptions options;
    options.maxCandidates = 100000;
    options.maxStars = 100000;
    options.thresholdSigma = 1.5;
    if (!detector.detect(
            originalLuminance, width, height, stars, options)) {
        return true;
    }
    if (stats) stats->detectedStars = stars.size();

    std::vector<StarPoint> filteredStars;
    filteredStars.reserve(stars.size());
    double fwhmSum = 0.0;
    for (size_t starIndex = 0; starIndex < stars.size(); ++starIndex) {
        const StarPoint& star = stars[starIndex];
        const int centerX = std::clamp(
            static_cast<int>(std::lround(star.x)), 0, width - 1);
        const int centerY = std::clamp(
            static_cast<int>(std::lround(star.y)), 0, height - 1);
        if (processingMask &&
            (*processingMask)[static_cast<size_t>(centerY) * width + centerX] < 128) {
            continue;
        }
        if (star.fwhm < 0.7 || star.fwhm > 8.0 ||
            star.ellipticity > 0.60 || star.flux < 100.0 ||
            hasSaturatedCore(image, width, height, star)) {
            continue;
        }
        filteredStars.push_back(star);
        fwhmSum += star.fwhm;
    }
    if (stats) {
        stats->processedStars = filteredStars.size();
        stats->averageInputFwhm = filteredStars.empty()
            ? 0.0 : fwhmSum / filteredStars.size();
    }
    if (filteredStars.empty()) return true;

    // Saturated and large stars were excluded above: replacing their broad,
    // clipped cores with an annulus estimate can leave a false ring once the
    // reconstructed star layer is contracted.
    // Build an explicit starless layer from immutable input. The center of each
    // footprint is fully replaced with its local annulus median; only the outer
    // ~1 px is feathered. For overlaps, keep the darkest valid proposal.
    std::vector<uint16_t> starless = image;
    std::vector<uint16_t> starlessLuminance = originalLuminance;
    for (const StarPoint& star : filteredStars) {
        const double footprintRadius =
            std::clamp(star.fwhm * 1.5, 2.0, 12.0);
        const double annulusInner = footprintRadius + 1.0;
        const double annulusOuter =
            annulusInner + std::max(2.0, star.fwhm * 0.75);
        const BackgroundColor background = localBackground(
            originalLuminance, image, width, height, star,
            annulusInner, annulusOuter);
        if (!background.valid) continue;

        const double noiseFloor =
            std::max(8.0, background.luminance * 0.002);
        const int radius = static_cast<int>(std::ceil(footprintRadius));
        const int centerX = static_cast<int>(std::lround(star.x));
        const int centerY = static_cast<int>(std::lround(star.y));
        const int x0 = std::max(0, centerX - radius);
        const int x1 = std::min(width - 1, centerX + radius);
        const int y0 = std::max(0, centerY - radius);
        const int y1 = std::min(height - 1, centerY + radius);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double distance = std::hypot(x - star.x, y - star.y);
                double weight = featheredFootprint(distance, footprintRadius);
                if (processingMask) {
                    weight *= (*processingMask)[
                        static_cast<size_t>(y) * width + x] / 255.0;
                }
                if (weight <= 0.0) continue;
                const size_t pixel = static_cast<size_t>(y) * width + x;
                if (originalLuminance[pixel] <=
                    background.luminance + noiseFloor) {
                    continue;
                }

                std::array<uint16_t, 3> proposal = {};
                for (int channel = 0; channel < 3; ++channel) {
                    proposal[channel] = blendImageSample(
                        image[pixel * 3 + channel],
                        background.rgb[channel], weight);
                }
                const uint16_t proposalLuminance =
                    rgbLuminance(proposal.data());
                if (proposalLuminance >= starlessLuminance[pixel]) continue;
                starlessLuminance[pixel] = proposalLuminance;
                for (int channel = 0; channel < 3; ++channel) {
                    starless[pixel * 3 + channel] = proposal[channel];
                }
            }
        }
    }

    // Keep the layer signed. A local RGB background can be brighter than the
    // source in one channel even when its luminance is lower. Preserving that
    // negative component makes starless + stars an exact color reconstruction
    // before the Minimum operation.
    std::vector<int32_t> starLayer(image.size());
    std::vector<uint16_t> starLayerLuminance(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        for (int channel = 0; channel < 3; ++channel) {
            starLayer[pixel * 3 + channel] =
                static_cast<int32_t>(image[pixel * 3 + channel]) -
                static_cast<int32_t>(starless[pixel * 3 + channel]);
        }
        starLayerLuminance[pixel] =
            positiveLayerLuminance(&starLayer[pixel * 3]);
    }

    const size_t defringedPixels = applyDefringe
        ? reduceStarColorFringes(image, starless, starLayer, width, height,
                                 filteredStars, strength, processingMask)
        : 0;
    if (stats) stats->defringedPixels = defringedPixels;
    if (defringedPixels > 0) {
        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            starLayerLuminance[pixel] =
                positiveLayerLuminance(&starLayer[pixel * 3]);
        }
    }

    if (!reduceSize) {
        size_t affectedPixels = 0;
        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            bool changed = false;
            for (int channel = 0; channel < 3; ++channel) {
                const size_t index = pixel * 3 + channel;
                const int64_t recombined =
                    static_cast<int64_t>(starless[index]) + starLayer[index];
                const uint16_t corrected = static_cast<uint16_t>(
                    std::clamp<int64_t>(recombined, 0, 65535));
                changed = changed || corrected != image[index];
                image[index] = corrected;
            }
            if (changed) ++affectedPixels;
        }
        if (stats) {
            stats->affectedPixels = affectedPixels;
            stats->radiusScale = 1.0;
        }
        return true;
    }

    const double normalizedStrength = strength / 100.0;
    // A continuously sampled disk approximates Photoshop's round Minimum
    // behavior without snapping the operation to a 4-neighbour pixel cross.
    // Faint-star removal is handled separately below, so a radius above 1.2 px
    // is unnecessary and would damage undersampled or slightly trailed stars.
    const double effectiveRadius = normalizedStrength * 1.2;
    std::vector<uint16_t> erodedLuminance;
    erodeRoundLuminanceSubpixel(
        starLayerLuminance, width, height,
        effectiveRadius, erodedLuminance);

    std::vector<uint16_t> starPeaks;
    starPeaks.reserve(filteredStars.size());
    for (const StarPoint& star : filteredStars) {
        const int x = std::clamp(
            static_cast<int>(std::lround(star.x)), 0, width - 1);
        const int y = std::clamp(
            static_cast<int>(std::lround(star.y)), 0, height - 1);
        starPeaks.push_back(
            starLayerLuminance[static_cast<size_t>(y) * width + x]);
    }
    std::vector<uint16_t> orderedStarPeaks = starPeaks;
    const double typicalStarPeak = median(orderedStarPeaks);
    const double clearProgress =
        smoothstep01((normalizedStrength - 0.55) / 0.45);
    const double residualFloor =
        typicalStarPeak * 0.75 * clearProgress;

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const double retainedLuminance = erodedLuminance[pixel] <= residualFloor
            ? 0.0
            : erodedLuminance[pixel] - residualFloor;
        const double retainedFraction = starLayerLuminance[pixel] > 0
            ? retainedLuminance / starLayerLuminance[pixel] : 0.0;
        for (int channel = 0; channel < 3; ++channel) {
            starLayer[pixel * 3 + channel] =
                static_cast<int32_t>(std::lround(
                    starLayer[pixel * 3 + channel] * retainedFraction));
        }
    }

    for (size_t index = 0; index < image.size(); ++index) {
        const int64_t recombined =
            static_cast<int64_t>(starless[index]) + starLayer[index];
        image[index] = static_cast<uint16_t>(
            std::clamp<int64_t>(recombined, 0, 65535));
    }

    if (stats) {
        stats->radiusScale = std::max(
            0.0, 1.0 - effectiveRadius /
                std::max(1.0, stats->averageInputFwhm));
        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            if (rgbLuminance(&image[pixel * 3]) <
                originalLuminance[pixel]) {
                ++stats->affectedPixels;
            }
        }
        for (size_t starIndex = 0;
             starIndex < filteredStars.size(); ++starIndex) {
            const StarPoint& star = filteredStars[starIndex];
            const int x = std::clamp(
                static_cast<int>(std::lround(star.x)), 0, width - 1);
            const int y = std::clamp(
                static_cast<int>(std::lround(star.y)), 0, height - 1);
            const size_t pixel = static_cast<size_t>(y) * width + x;
            const uint16_t processedPeak =
                positiveLayerLuminance(&starLayer[pixel * 3]);
            if (starPeaks[starIndex] > 0 &&
                processedPeak <= starPeaks[starIndex] * 0.15) {
                ++stats->stronglySuppressedStars;
            }
        }
    }

    return true;
}

} // namespace

bool StarReducer::reduce(std::vector<uint16_t>& image, int width, int height,
                         int strength, StarReductionStats* stats,
                         const std::vector<uint8_t>* processingMask,
                         bool applyDefringe) {
    return processStars(image, width, height, strength, stats, processingMask,
                        true, applyDefringe);
}

bool StarReducer::defringe(std::vector<uint16_t>& image, int width, int height,
                           int strength, StarReductionStats* stats,
                           const std::vector<uint8_t>* processingMask) {
    return processStars(image, width, height, strength, stats, processingMask,
                        false, true);
}
