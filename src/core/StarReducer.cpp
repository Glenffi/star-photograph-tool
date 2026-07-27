#include "StarReducer.h"

#include "StarDetector.h"

#include <algorithm>
#include <climits>
#include <cmath>

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

double sampleBilinear(const std::vector<uint16_t>& image,
                      int width, int height, double x, double y) {
    if (x < 0.0 || y < 0.0 ||
        x >= static_cast<double>(width - 1) ||
        y >= static_cast<double>(height - 1)) {
        return 0.0;
    }
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const double top =
        image[static_cast<size_t>(y0) * width + x0] * (1.0 - fx) +
        image[static_cast<size_t>(y0) * width + x0 + 1] * fx;
    const double bottom =
        image[static_cast<size_t>(y0 + 1) * width + x0] * (1.0 - fx) +
        image[static_cast<size_t>(y0 + 1) * width + x0 + 1] * fx;
    return top * (1.0 - fy) + bottom * fy;
}

uint16_t localBackground(const std::vector<uint16_t>& luminance,
                         int width, int height, const StarPoint& star,
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
    std::vector<uint16_t> samples;
    samples.reserve(static_cast<size_t>(radius * radius * 3));

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double dx = x - star.x;
            const double dy = y - star.y;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared >= innerSquared &&
                distanceSquared <= outerSquared) {
                samples.push_back(
                    luminance[static_cast<size_t>(y) * width + x]);
            }
        }
    }
    return median(samples);
}

double smoothMask(double distance, double radius) {
    if (distance >= radius) return 0.0;
    const double normalized = distance / radius;
    const double smoothstep =
        normalized * normalized * (3.0 - 2.0 * normalized);
    return 1.0 - smoothstep;
}

double smoothstep01(double value) {
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

} // namespace

bool StarReducer::reduce(std::vector<uint16_t>& image, int width, int height,
                         int strength, StarReductionStats* stats) {
    if (stats) *stats = {};
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536 ||
        width > INT_MAX / height) {
        return false;
    }

    const int pixelCount = width * height;
    const size_t expectedSize = static_cast<size_t>(pixelCount) * 3;
    if (image.size() != expectedSize || strength < 0) return false;
    if (strength == 0) return true;
    strength = std::min(strength, 100);

    std::vector<uint16_t> originalLuminance(pixelCount);
    for (int pixel = 0; pixel < pixelCount; ++pixel) {
        const uint32_t red = image[pixel * 3];
        const uint32_t green = image[pixel * 3 + 1];
        const uint32_t blue = image[pixel * 3 + 2];
        originalLuminance[pixel] = static_cast<uint16_t>(
            (red * 299 + green * 587 + blue * 114) / 1000);
    }

    StarDetector detector;
    std::vector<StarPoint> stars;
    DetectionOptions options;
    options.maxCandidates = 12000;
    options.maxStars = 12000;
    options.thresholdSigma = 4.0;
    if (!detector.detect(
            originalLuminance, width, height, stars, options)) {
        return true;
    }
    if (stats) stats->detectedStars = stars.size();

    std::vector<StarPoint> filteredStars;
    filteredStars.reserve(stars.size());
    double fwhmSum = 0.0;
    for (const StarPoint& star : stars) {
        if (star.fwhm < 0.7 || star.fwhm > 20.0 ||
            star.ellipticity > 0.7 || star.flux < 100.0) {
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

    const double normalizedStrength = strength / 100.0;
    // Radius contraction alone does not remove tiny stars because their center
    // remains a bright sample. Reduce the common bright-star amplitude and add
    // a rank-aware fade for the numerous faint stars. This mirrors the normal
    // photographic goal: retain prominent stars while simplifying dense fields.
    const double radiusScale = 1.0 - normalizedStrength * 0.75;
    const double brightStarAmplitude = 1.0 - normalizedStrength * 0.55;
    const double fadeProgress = std::clamp(
        (normalizedStrength - 0.20) / 0.65, 0.0, 1.0);
    if (stats) stats->radiusScale = radiusScale;

    // Every proposal is derived from the immutable source. Overlapping masks
    // keep the strongest reduction, so processing order cannot create rings.
    std::vector<uint16_t> outputLuminance = originalLuminance;
    for (size_t starIndex = 0;
         starIndex < filteredStars.size(); ++starIndex) {
        const StarPoint& star = filteredStars[starIndex];
        const double rank = filteredStars.size() <= 1
            ? 0.0
            : static_cast<double>(starIndex) /
                static_cast<double>(filteredStars.size() - 1);
        // The detector returns stars in descending flux order. The smooth
        // transition avoids a visible threshold between retained and removed
        // stars. Compactness helps suppress genuinely tiny stars without
        // erasing an unusually bright compact star outright.
        const double faintness = smoothstep01((rank - 0.15) / 0.70);
        const double compactness =
            1.0 - smoothstep01((star.fwhm - 1.5) / 4.0);
        const double smallStarScore = std::max(
            faintness, compactness * (0.25 + 0.75 * faintness));
        const double survival = std::clamp(
            1.0 - fadeProgress * 1.35 * smallStarScore, 0.0, 1.0);
        const double amplitudeScale =
            brightStarAmplitude * survival;
        if (stats && survival <= 0.15) {
            ++stats->stronglySuppressedStars;
        }

        const double maskRadius = std::clamp(star.fwhm * 2.5, 3.0, 24.0);
        const double backgroundInner =
            std::clamp(star.fwhm * 1.6, 1.5, maskRadius * 0.8);
        const uint16_t background = localBackground(
            originalLuminance, width, height, star,
            backgroundInner, maskRadius);
        const double noiseFloor = std::max(2.0, background * 0.002);
        const int radius = static_cast<int>(std::ceil(maskRadius));
        const int centerX = static_cast<int>(std::lround(star.x));
        const int centerY = static_cast<int>(std::lround(star.y));
        const int x0 = std::max(0, centerX - radius);
        const int x1 = std::min(width - 1, centerX + radius);
        const int y0 = std::max(0, centerY - radius);
        const int y1 = std::min(height - 1, centerY + radius);

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double dx = x - star.x;
                const double dy = y - star.y;
                const double distance = std::hypot(dx, dy);
                if (distance >= maskRadius) continue;

                const size_t index = static_cast<size_t>(y) * width + x;
                const double original = originalLuminance[index];
                // Never alter dark dust or texture at/below the local annulus
                // background. This is the black-hole guard.
                if (original <= background + noiseFloor) continue;

                const double sourceX = star.x + dx / radiusScale;
                const double sourceY = star.y + dy / radiusScale;
                const double sampled =
                    sampleBilinear(originalLuminance, width, height,
                                   sourceX, sourceY);
                const double starSignal =
                    std::max(0.0, sampled - background);
                const double contracted =
                    background + starSignal * amplitudeScale;
                const double maskWeight =
                    smoothMask(distance, maskRadius);
                const double proposal = std::min(
                    original,
                    original * (1.0 - maskWeight) +
                        contracted * maskWeight);
                outputLuminance[index] = std::min(
                    outputLuminance[index],
                    static_cast<uint16_t>(std::lround(
                        std::clamp(proposal, 0.0, 65535.0))));
            }
        }
    }

    if (stats) {
        for (size_t pixel = 0; pixel < outputLuminance.size(); ++pixel) {
            if (outputLuminance[pixel] < originalLuminance[pixel]) {
                ++stats->affectedPixels;
            }
        }
    }

    std::vector<uint16_t> outputRgb;
    rebuildRgb(image, originalLuminance, outputLuminance,
               width, height, outputRgb);
    image = std::move(outputRgb);
    return true;
}

void StarReducer::rebuildRgb(const std::vector<uint16_t>& rgb,
                             const std::vector<uint16_t>& originalL,
                             const std::vector<uint16_t>& outputL,
                             int width, int height,
                             std::vector<uint16_t>& outRgb) {
    const int pixelCount = width * height;
    outRgb.resize(static_cast<size_t>(pixelCount) * 3);

    constexpr float minimumRatio = 0.1f;
    constexpr float epsilon = 1.0f;
    for (int pixel = 0; pixel < pixelCount; ++pixel) {
        const float original = static_cast<float>(originalL[pixel]);
        const float output = static_cast<float>(outputL[pixel]);
        const float ratio = std::clamp(
            output / std::max(original, epsilon), minimumRatio, 1.0f);

        for (int channel = 0; channel < 3; ++channel) {
            const float value = rgb[pixel * 3 + channel] * ratio;
            outRgb[pixel * 3 + channel] = static_cast<uint16_t>(
                std::clamp(static_cast<int>(value + 0.5f), 0, 65535));
        }
    }
}
