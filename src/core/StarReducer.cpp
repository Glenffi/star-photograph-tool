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

void erodeRound(const std::vector<int32_t>& layer,
                const std::vector<uint16_t>& luminance,
                int width, int height, int radius,
                std::vector<int32_t>& eroded) {
    eroded.resize(layer.size());
    const int radiusSquared = radius * radius;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            size_t darkestPixel = pixel;
            uint16_t darkest = luminance[pixel];
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > radiusSquared) continue;
                    const int sampleX = x + dx;
                    const int sampleY = y + dy;
                    if (sampleX < 0 || sampleX >= width ||
                        sampleY < 0 || sampleY >= height) {
                        continue;
                    }
                    const size_t sample =
                        static_cast<size_t>(sampleY) * width + sampleX;
                    if (luminance[sample] < darkest) {
                        darkest = luminance[sample];
                        darkestPixel = sample;
                    }
                }
            }
            for (int channel = 0; channel < 3; ++channel) {
                eroded[pixel * 3 + channel] =
                    layer[darkestPixel * 3 + channel];
            }
        }
    }
}

int32_t blendLayerSample(int32_t first, int32_t second, double amount) {
    return static_cast<int32_t>(std::lround(
        first * (1.0 - amount) + second * amount));
}

uint16_t blendImageSample(uint16_t first, uint16_t second, double amount) {
    return static_cast<uint16_t>(std::lround(
        first * (1.0 - amount) + second * amount));
}

} // namespace

bool StarReducer::reduce(std::vector<uint16_t>& image, int width, int height,
                         int strength, StarReductionStats* stats,
                         const std::vector<uint8_t>* processingMask) {
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
        const bool prominent =
            starIndex < std::min<size_t>(2000, stars.size() / 20);
        const bool compact =
            star.fwhm <= 7.5 && star.ellipticity <= 0.68;
        if (star.fwhm < 0.7 || star.fwhm > 20.0 ||
            star.ellipticity > 0.7 || star.flux < 100.0 ||
            (!compact && !prominent)) {
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

    std::vector<int32_t> erodedRadiusOne;
    erodeRound(starLayer, starLayerLuminance,
               width, height, 1, erodedRadiusOne);

    const double normalizedStrength = strength / 100.0;
    // Values around 0.3-1.2 px cover normal photographic reduction. The upper
    // end extends to 2 px so the existing 70-100 "strong/near-clear" range can
    // remove undersampled stars instead of merely lowering their peaks.
    const double effectiveRadius = normalizedStrength * 2.0;
    const bool useSecondRadius = effectiveRadius > 1.0;
    const double interpolation = useSecondRadius
        ? effectiveRadius - 1.0 : effectiveRadius;

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

    if (useSecondRadius) {
        for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
            starLayerLuminance[pixel] =
                positiveLayerLuminance(&erodedRadiusOne[pixel * 3]);
        }
        // Reuse the original layer buffer for radius 2. Applying the radius-1
        // disk twice produces the same round diamond footprint at this scale.
        erodeRound(erodedRadiusOne, starLayerLuminance,
                   width, height, 1, starLayer);
    }

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        std::array<int32_t, 3> blended = {};
        for (int channel = 0; channel < 3; ++channel) {
            const size_t index = pixel * 3 + channel;
            if (useSecondRadius) {
                blended[channel] = blendLayerSample(
                    erodedRadiusOne[index], starLayer[index], interpolation);
            } else {
                blended[channel] = blendLayerSample(
                    starLayer[index], erodedRadiusOne[index], interpolation);
            }
        }
        const uint16_t blendedLuminance =
            positiveLayerLuminance(blended.data());
        const double retainedFraction = blendedLuminance <= residualFloor
            ? 0.0
            : (blendedLuminance - residualFloor) / blendedLuminance;
        for (int channel = 0; channel < 3; ++channel) {
            starLayer[pixel * 3 + channel] =
                static_cast<int32_t>(std::lround(
                    blended[channel] * retainedFraction));
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
