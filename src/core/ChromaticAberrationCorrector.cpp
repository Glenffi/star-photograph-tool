#include "ChromaticAberrationCorrector.h"

#include "StarDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr uint16_t kSaturationLimit = 64500;

bool validRgb(const std::vector<uint16_t>& rgb, int width, int height,
              size_t& pixelCount) {
    if (width <= 0 || height <= 0) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h) return false;
    pixelCount = w * h;
    return pixelCount <= std::numeric_limits<size_t>::max() / 3 &&
        rgb.size() == pixelCount * 3;
}

uint16_t luminance(const uint16_t* rgb) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(rgb[0]) * 13933U +
         static_cast<uint32_t>(rgb[1]) * 46871U +
         static_cast<uint32_t>(rgb[2]) * 4732U) /
        65536U);
}

double median(std::vector<double>& values) {
    if (values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const double upper = values[middle];
    const double lower = *std::max_element(values.begin(),
                                           values.begin() + middle);
    return (lower + upper) * 0.5;
}

struct ChannelCentroid {
    bool valid = false;
    double x = 0.0;
    double y = 0.0;
    double signal = 0.0;
};

struct StarCentroids {
    bool saturated = false;
    std::array<ChannelCentroid, 3> channels;
};

StarCentroids measureCentroids(const std::vector<uint16_t>& rgb,
                               int width, int height,
                               const StarPoint& star) {
    StarCentroids result;
    const int radius = std::clamp(
        static_cast<int>(std::ceil(star.fwhm * 1.6 + 3.0)), 5, 9);
    const int centerX = static_cast<int>(std::lround(star.x));
    const int centerY = static_cast<int>(std::lround(star.y));
    if (centerX - radius < 0 || centerX + radius >= width ||
        centerY - radius < 0 || centerY + radius >= height) {
        return result;
    }

    const double sampleRadius = radius - 2.0;
    const double annulusInner = radius - 1.5;
    const double radiusSquared = static_cast<double>(radius) * radius;
    const double sampleRadiusSquared = sampleRadius * sampleRadius;
    const double annulusInnerSquared = annulusInner * annulusInner;
    std::array<std::vector<double>, 3> backgrounds;
    for (auto& values : backgrounds) {
        values.reserve(static_cast<size_t>(radius * radius * 2));
    }

    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const double dx = x - star.x;
            const double dy = y - star.y;
            const double distanceSquared = dx * dx + dy * dy;
            if (distanceSquared > radiusSquared) continue;
            const size_t base =
                (static_cast<size_t>(y) * width + x) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                result.saturated = result.saturated ||
                    rgb[base + channel] >= kSaturationLimit;
                if (distanceSquared >= annulusInnerSquared) {
                    backgrounds[channel].push_back(rgb[base + channel]);
                }
            }
        }
    }
    if (result.saturated) return result;

    std::array<double, 3> background = {};
    std::array<double, 3> backgroundNoise = {};
    for (int channel = 0; channel < 3; ++channel) {
        if (backgrounds[channel].size() < 12) return result;
        background[channel] = median(backgrounds[channel]);
        std::vector<double> deviations;
        deviations.reserve(backgrounds[channel].size());
        for (double value : backgrounds[channel]) {
            deviations.push_back(std::abs(value - background[channel]));
        }
        backgroundNoise[channel] = std::max(0.5, median(deviations) * 1.4826);
    }

    std::array<double, 3> weightedX = {};
    std::array<double, 3> weightedY = {};
    std::array<double, 3> weightSum = {};
    std::array<double, 3> peakSignal = {};
    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const double dx = x - star.x;
            const double dy = y - star.y;
            if (dx * dx + dy * dy > sampleRadiusSquared) continue;
            const size_t base =
                (static_cast<size_t>(y) * width + x) * 3;
            for (int channel = 0; channel < 3; ++channel) {
                const double signal = std::max(
                    0.0, rgb[base + channel] - background[channel]);
                peakSignal[channel] = std::max(peakSignal[channel], signal);
                weightSum[channel] += signal;
                weightedX[channel] += signal * x;
                weightedY[channel] += signal * y;
            }
        }
    }

    for (int channel = 0; channel < 3; ++channel) {
        // A weak channel in a strongly colored star has an unstable centroid.
        // Such stars are omitted for that channel instead of forcing a result.
        if (weightSum[channel] < 2500.0 ||
            peakSignal[channel] < std::max(180.0,
                                            backgroundNoise[channel] * 6.0)) {
            continue;
        }
        result.channels[channel].valid = true;
        result.channels[channel].x = weightedX[channel] / weightSum[channel];
        result.channels[channel].y = weightedY[channel] / weightSum[channel];
        result.channels[channel].signal = weightSum[channel];
    }
    return result;
}

struct RadialObservation {
    double radialX = 0.0;
    double radialY = 0.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double radius = 0.0;
    double radialDelta = 0.0;
    double tangentialDelta = 0.0;
    int quadrant = 0;
    int angularSector = 0;
};

struct RadialFit {
    double offsetX = 0.0;
    double offsetY = 0.0;
    double slope = 0.0;
};

RadialFit solveRadialFit(
        const std::vector<const RadialObservation*>& observations) {
    RadialFit fit;
    if (observations.empty()) return fit;
    for (const RadialObservation* observation : observations) {
        fit.offsetX += observation->offsetX;
        fit.offsetY += observation->offsetY;
    }
    fit.offsetX /= observations.size();
    fit.offsetY /= observations.size();

    double meanRadialX = 0.0;
    double meanRadialY = 0.0;
    for (const RadialObservation* observation : observations) {
        meanRadialX += observation->radialX;
        meanRadialY += observation->radialY;
    }
    meanRadialX /= observations.size();
    meanRadialY /= observations.size();

    double numerator = 0.0;
    double denominator = 0.0;
    for (const RadialObservation* observation : observations) {
        const double centeredRadialX = observation->radialX - meanRadialX;
        const double centeredRadialY = observation->radialY - meanRadialY;
        numerator += centeredRadialX *
                         (observation->offsetX - fit.offsetX) +
            centeredRadialY * (observation->offsetY - fit.offsetY);
        denominator += centeredRadialX * centeredRadialX +
            centeredRadialY * centeredRadialY;
    }
    if (denominator <= 0.0) return fit;
    fit.slope = numerator / denominator;
    fit.offsetX -= fit.slope * meanRadialX;
    fit.offsetY -= fit.slope * meanRadialY;
    return fit;
}

int quadrantFor(double x, double y) {
    return (y >= 0.0 ? 2 : 0) + (x >= 0.0 ? 1 : 0);
}

int angularSectorFor(double x, double y) {
    constexpr double kPi = 3.14159265358979323846;
    double angle = std::atan2(y, x);
    if (angle < 0.0) angle += 2.0 * kPi;
    return std::min(7, static_cast<int>(angle * 8.0 / (2.0 * kPi)));
}

ChromaticAberrationChannelStats fitChannel(
        const std::vector<RadialObservation>& observations,
        double halfDiagonal) {
    ChromaticAberrationChannelStats stats;
    stats.measuredStars = static_cast<int>(observations.size());
    if (observations.size() < 20 || halfDiagonal <= 0.0) return stats;

    std::vector<const RadialObservation*> all;
    all.reserve(observations.size());
    for (const RadialObservation& observation : observations) {
        all.push_back(&observation);
    }
    const RadialFit initialFit = solveRadialFit(all);

    std::vector<double> residualMagnitudes;
    residualMagnitudes.reserve(observations.size());
    for (const RadialObservation& observation : observations) {
        const double residualX = observation.offsetX - initialFit.offsetX -
            initialFit.slope * observation.radialX;
        const double residualY = observation.offsetY - initialFit.offsetY -
            initialFit.slope * observation.radialY;
        residualMagnitudes.push_back(std::hypot(residualX, residualY));
    }
    const double residualMedian = median(residualMagnitudes);
    std::vector<double> residualDeviations;
    residualDeviations.reserve(residualMagnitudes.size());
    for (double residual : residualMagnitudes) {
        residualDeviations.push_back(std::abs(residual - residualMedian));
    }
    const double residualMad = median(residualDeviations);
    const double residualThreshold = std::max(
        0.30, residualMedian + residualMad * 1.4826 * 3.0);

    std::vector<const RadialObservation*> inliers;
    inliers.reserve(observations.size());
    std::array<bool, 4> quadrants = {};
    std::array<bool, 8> angularSectors = {};
    double maximumRadius = 0.0;
    int outerInlierStars = 0;
    for (const RadialObservation& observation : observations) {
        const double residualX = observation.offsetX - initialFit.offsetX -
            initialFit.slope * observation.radialX;
        const double residualY = observation.offsetY - initialFit.offsetY -
            initialFit.slope * observation.radialY;
        if (std::hypot(residualX, residualY) > residualThreshold) {
            continue;
        }
        inliers.push_back(&observation);
        quadrants[static_cast<size_t>(observation.quadrant)] = true;
        angularSectors[static_cast<size_t>(observation.angularSector)] = true;
        maximumRadius = std::max(maximumRadius, observation.radius);
        if (observation.radius >= halfDiagonal * 0.55) {
            ++outerInlierStars;
        }
    }
    stats.inlierStars = static_cast<int>(inliers.size());
    stats.coveredQuadrants = static_cast<int>(std::count(
        quadrants.begin(), quadrants.end(), true));
    stats.coveredAngularSectors = static_cast<int>(std::count(
        angularSectors.begin(), angularSectors.end(), true));
    stats.inlierFraction = static_cast<double>(inliers.size()) /
        observations.size();
    stats.outerInlierStars = outerInlierStars;
    if (inliers.size() < 20) return stats;
    const RadialFit fit = solveRadialFit(inliers);
    stats.nuisanceOffsetX = fit.offsetX;
    stats.nuisanceOffsetY = fit.offsetY;

    double radialSquared = 0.0;
    double uncorrectedRadialSquared = 0.0;
    double tangentialSquared = 0.0;
    for (const RadialObservation* observation : inliers) {
        const double unitX = observation->radialX / observation->radius;
        const double unitY = observation->radialY / observation->radius;
        const double adjustedX = observation->offsetX - fit.offsetX;
        const double adjustedY = observation->offsetY - fit.offsetY;
        const double residualX =
            adjustedX - fit.slope * observation->radialX;
        const double residualY =
            adjustedY - fit.slope * observation->radialY;
        const double radialResidual = residualX * unitX + residualY * unitY;
        const double tangentialResidual =
            -residualX * unitY + residualY * unitX;
        const double uncorrectedRadial =
            adjustedX * unitX + adjustedY * unitY;
        radialSquared += radialResidual * radialResidual;
        uncorrectedRadialSquared +=
            uncorrectedRadial * uncorrectedRadial;
        tangentialSquared += tangentialResidual * tangentialResidual;
    }
    stats.sourceScale = 1.0 + fit.slope;
    stats.edgeShiftPixels = fit.slope * halfDiagonal;
    stats.uncorrectedRadialRms =
        std::sqrt(uncorrectedRadialSquared / inliers.size());
    stats.radialResidualRms = std::sqrt(radialSquared / inliers.size());
    stats.tangentialResidualRms =
        std::sqrt(tangentialSquared / inliers.size());

    stats.reliable = stats.inlierFraction >= 0.70 &&
        stats.coveredQuadrants >= 3 &&
        stats.coveredAngularSectors >= 5 &&
        maximumRadius >= halfDiagonal * 0.55 &&
        stats.outerInlierStars >= 12 &&
        std::abs(stats.edgeShiftPixels) >= 0.15 &&
        std::abs(stats.edgeShiftPixels) <= 3.0 &&
        stats.sourceScale >= 0.997 && stats.sourceScale <= 1.003 &&
        stats.radialResidualRms <= 0.40 &&
        stats.tangentialResidualRms <= 0.40 &&
        stats.radialResidualRms <= stats.uncorrectedRadialRms * 0.90;
    return stats;
}

uint16_t sampleChannel(const std::vector<uint16_t>& rgb,
                       int width, int height, int channel,
                       double x, double y) {
    x = std::clamp(x, 0.0, static_cast<double>(width - 1));
    y = std::clamp(y, 0.0, static_cast<double>(height - 1));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto value = [&](int sx, int sy) {
        return rgb[(static_cast<size_t>(sy) * width + sx) * 3 + channel];
    };
    const double top = value(x0, y0) * (1.0 - fx) + value(x1, y0) * fx;
    const double bottom = value(x0, y1) * (1.0 - fx) + value(x1, y1) * fx;
    return static_cast<uint16_t>(std::clamp(
        std::lround(top * (1.0 - fy) + bottom * fy), 0L, 65535L));
}

} // namespace

bool ChromaticAberrationCorrector::estimate(
    const std::vector<uint16_t>& rgb, int width, int height,
    ChromaticAberrationModel& model, ChromaticAberrationStats* stats) {
    model = {};
    if (stats) *stats = {};
    size_t pixelCount = 0;
    if (!validRgb(rgb, width, height, pixelCount)) return false;

    model.centerX = (width - 1) * 0.5;
    model.centerY = (height - 1) * 0.5;
    std::vector<uint16_t> brightness(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        brightness[pixel] = luminance(rgb.data() + pixel * 3);
    }

    DetectionOptions options;
    options.spatiallyBalanced = true;
    options.gridCols = 12;
    options.gridRows = 8;
    options.maxCandidates = 3000;
    options.maxStars = 1500;
    options.thresholdSigma = 4.5;
    StarDetector detector;
    std::vector<StarPoint> stars;
    if (!detector.detect(brightness, width, height, stars, options)) {
        return true;
    }
    if (stats) stats->detectedStars = static_cast<int>(stars.size());

    const double halfDiagonal = std::hypot(model.centerX, model.centerY);
    const double minimumRadius = halfDiagonal * 0.20;
    std::array<std::vector<RadialObservation>, 2> observations;
    for (auto& channel : observations) channel.reserve(stars.size());

    int eligibleStars = 0;
    for (size_t starIndex = 0; starIndex < stars.size(); ++starIndex) {
        const StarPoint& star = stars[starIndex];
        if (star.fwhm < 1.0 || star.fwhm > 7.0 ||
            star.ellipticity > 0.45 || star.flux < 200.0) {
            continue;
        }
        bool crowded = false;
        const double isolationRadius = std::max(8.0, star.fwhm * 2.5);
        for (size_t otherIndex = 0;
             otherIndex < stars.size() && !crowded; ++otherIndex) {
            if (otherIndex == starIndex) continue;
            const double dx = stars[otherIndex].x - star.x;
            const double dy = stars[otherIndex].y - star.y;
            crowded = dx * dx + dy * dy <
                isolationRadius * isolationRadius;
        }
        if (crowded) continue;
        const StarCentroids centroids =
            measureCentroids(rgb, width, height, star);
        if (centroids.saturated || !centroids.channels[1].valid) continue;
        const double radialX = centroids.channels[1].x - model.centerX;
        const double radialY = centroids.channels[1].y - model.centerY;
        const double radius = std::hypot(radialX, radialY);
        if (radius < minimumRadius) continue;
        const double unitX = radialX / radius;
        const double unitY = radialY / radius;
        ++eligibleStars;

        for (int channelIndex = 0; channelIndex < 2; ++channelIndex) {
            const int rgbChannel = channelIndex == 0 ? 0 : 2;
            if (!centroids.channels[rgbChannel].valid) continue;
            const double offsetX =
                centroids.channels[rgbChannel].x - centroids.channels[1].x;
            const double offsetY =
                centroids.channels[rgbChannel].y - centroids.channels[1].y;
            const double radialDelta = offsetX * unitX + offsetY * unitY;
            const double tangentialDelta = -offsetX * unitY + offsetY * unitX;
            if (std::abs(radialDelta) > 6.0 ||
                std::abs(tangentialDelta) > 2.0) {
                continue;
            }
            observations[channelIndex].push_back({
                radialX, radialY, offsetX, offsetY,
                radius, radialDelta, tangentialDelta,
                quadrantFor(radialX, radialY),
                angularSectorFor(radialX, radialY)});
        }
    }
    if (stats) stats->eligibleStars = eligibleStars;

    const ChromaticAberrationChannelStats red =
        fitChannel(observations[0], halfDiagonal);
    const ChromaticAberrationChannelStats blue =
        fitChannel(observations[1], halfDiagonal);
    model.redSourceScale = red.sourceScale;
    model.blueSourceScale = blue.sourceScale;
    model.correctRed = red.reliable;
    model.correctBlue = blue.reliable;
    if (stats) {
        stats->red = red;
        stats->blue = blue;
    }
    return true;
}

bool ChromaticAberrationCorrector::apply(
    const std::vector<uint16_t>& rgb, int width, int height,
    const ChromaticAberrationModel& model,
    std::vector<uint16_t>& corrected) {
    size_t pixelCount = 0;
    if (!validRgb(rgb, width, height, pixelCount) ||
        !std::isfinite(model.centerX) || !std::isfinite(model.centerY) ||
        !std::isfinite(model.redSourceScale) ||
        !std::isfinite(model.blueSourceScale) ||
        model.redSourceScale <= 0.0 || model.blueSourceScale <= 0.0) {
        return false;
    }
    if (!model.active()) {
        corrected = rgb;
        return true;
    }

    corrected.assign(rgb.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t base =
                (static_cast<size_t>(y) * width + x) * 3;
            const double dx = x - model.centerX;
            const double dy = y - model.centerY;
            const double redX = model.centerX + dx * model.redSourceScale;
            const double redY = model.centerY + dy * model.redSourceScale;
            const double blueX = model.centerX + dx * model.blueSourceScale;
            const double blueY = model.centerY + dy * model.blueSourceScale;
            const auto validSample = [&](bool enabled, double sx, double sy) {
                return !enabled ||
                    (sx >= 0.0 && sx < static_cast<double>(width - 1) &&
                     sy >= 0.0 && sy < static_cast<double>(height - 1));
            };
            if (!validSample(model.correctRed, redX, redY) ||
                !validSample(model.correctBlue, blueX, blueY)) {
                continue;
            }
            corrected[base] = model.correctRed
                ? sampleChannel(rgb, width, height, 0, redX, redY)
                : rgb[base];
            corrected[base + 1] = rgb[base + 1];
            corrected[base + 2] = model.correctBlue
                ? sampleChannel(rgb, width, height, 2, blueX, blueY)
                : rgb[base + 2];
        }
    }
    return true;
}

bool ChromaticAberrationCorrector::correctInPlace(
    std::vector<uint16_t>& rgb, int width, int height,
    const ChromaticAberrationModel& model) {
    if (!model.active()) return true;
    std::vector<uint16_t> corrected;
    if (!apply(rgb, width, height, model, corrected)) return false;
    rgb = std::move(corrected);
    return true;
}
