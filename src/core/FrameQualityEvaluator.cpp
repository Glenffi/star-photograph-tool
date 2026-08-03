#include "FrameQualityEvaluator.h"

#include "StarDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const double lower =
        *std::max_element(values.begin(), values.begin() + middle);
    return (lower + values[middle]) * 0.5;
}

double medianAbsoluteDeviation(const std::vector<double>& values,
                               double center) {
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values) deviations.push_back(std::abs(value - center));
    return median(std::move(deviations));
}

double robustZ(double value, double center, double mad, double scaleFloor) {
    const double scale = std::max(mad * 1.4826, scaleFloor);
    return std::clamp((value - center) / scale, -4.0, 4.0);
}

struct SequenceCenters {
    double logStars = 0.0;
    double logStarsMad = 0.0;
    double fwhm = 0.0;
    double fwhmMad = 0.0;
    double ellipticity = 0.0;
    double ellipticityMad = 0.0;
    double noise = 0.0;
    double noiseMad = 0.0;
    double clipped = 0.0;
    double clippedMad = 0.0;
    double starCount = 0.0;
};

bool removeLocalBackground(const std::vector<uint16_t>& source,
                           int width, int height,
                           std::vector<uint16_t>& output) {
    const size_t integralWidth = static_cast<size_t>(width) + 1;
    if (integralWidth > std::numeric_limits<size_t>::max() /
                            (static_cast<size_t>(height) + 1)) {
        return false;
    }
    std::vector<uint64_t> integral(
        integralWidth * (static_cast<size_t>(height) + 1), 0);
    for (int y = 0; y < height; ++y) {
        uint64_t rowSum = 0;
        for (int x = 0; x < width; ++x) {
            rowSum += source[static_cast<size_t>(y) * width + x];
            integral[static_cast<size_t>(y + 1) * integralWidth + x + 1] =
                integral[static_cast<size_t>(y) * integralWidth + x + 1] +
                rowSum;
        }
    }

    constexpr int kRadius = 15;
    constexpr int kBaseline = 8192;
    output.resize(source.size());
    for (int y = 0; y < height; ++y) {
        const int top = std::max(0, y - kRadius);
        const int bottom = std::min(height, y + kRadius + 1);
        for (int x = 0; x < width; ++x) {
            const int left = std::max(0, x - kRadius);
            const int right = std::min(width, x + kRadius + 1);
            const uint64_t sum =
                integral[static_cast<size_t>(bottom) * integralWidth + right] -
                integral[static_cast<size_t>(top) * integralWidth + right] -
                integral[static_cast<size_t>(bottom) * integralWidth + left] +
                integral[static_cast<size_t>(top) * integralWidth + left];
            const uint64_t area =
                static_cast<uint64_t>(right - left) * (bottom - top);
            const int localBackground = static_cast<int>(sum / area);
            const int corrected = kBaseline +
                static_cast<int>(source[static_cast<size_t>(y) * width + x]) -
                localBackground;
            output[static_cast<size_t>(y) * width + x] =
                static_cast<uint16_t>(std::clamp(corrected, 0, 65535));
        }
    }
    return true;
}

SequenceCenters sequenceCenters(const std::vector<FrameQualityMetrics>& metrics,
                                const std::vector<size_t>& validIndices) {
    std::vector<double> logStars;
    std::vector<double> fwhm;
    std::vector<double> ellipticity;
    std::vector<double> noise;
    std::vector<double> clipped;
    std::vector<double> starCounts;
    logStars.reserve(validIndices.size());
    fwhm.reserve(validIndices.size());
    ellipticity.reserve(validIndices.size());
    noise.reserve(validIndices.size());
    clipped.reserve(validIndices.size());
    starCounts.reserve(validIndices.size());
    for (size_t index : validIndices) {
        const FrameQualityMetrics& item = metrics[index];
        logStars.push_back(std::log1p(item.usableStars));
        fwhm.push_back(item.medianFwhm);
        ellipticity.push_back(item.medianEllipticity);
        noise.push_back(item.backgroundNoise);
        clipped.push_back(item.clippedFraction);
        starCounts.push_back(item.usableStars);
    }

    SequenceCenters centers;
    centers.logStars = median(logStars);
    centers.logStarsMad = medianAbsoluteDeviation(logStars, centers.logStars);
    centers.fwhm = median(fwhm);
    centers.fwhmMad = medianAbsoluteDeviation(fwhm, centers.fwhm);
    centers.ellipticity = median(ellipticity);
    centers.ellipticityMad =
        medianAbsoluteDeviation(ellipticity, centers.ellipticity);
    centers.noise = median(noise);
    centers.noiseMad = medianAbsoluteDeviation(noise, centers.noise);
    centers.clipped = median(clipped);
    centers.clippedMad = medianAbsoluteDeviation(clipped, centers.clipped);
    centers.starCount = median(starCounts);
    return centers;
}

} // namespace

bool FrameQualityEvaluator::evaluate(
    const std::vector<uint16_t>& luminance, int width, int height,
    FrameQualityMetrics& metrics) {
    metrics = {};
    if (width <= 0 || height <= 0 ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() / static_cast<size_t>(height)) {
        return false;
    }
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (luminance.size() != pixelCount || pixelCount < 64 * 64) return false;

    const size_t step = std::max<size_t>(1, pixelCount / 65536);
    std::vector<double> samples;
    samples.reserve(std::min<size_t>(pixelCount, 65536) + 1);
    size_t clipped = 0;
    for (size_t index = 0; index < pixelCount; index += step) {
        const double value = luminance[index];
        samples.push_back(value);
        if (value >= 65000.0) ++clipped;
    }
    const double background = median(samples);
    const double noise =
        medianAbsoluteDeviation(samples, background) * 1.4826;

    DetectionOptions options;
    options.maxCandidates = 1200;
    options.maxStars = 800;
    options.spatiallyBalanced = true;
    options.gridCols = 8;
    options.gridRows = 6;
    options.fitGaussian = true;
    options.thresholdSigma = 4.0;
    StarDetector detector;
    std::vector<StarPoint> stars;
    std::vector<uint16_t> starSignal;
    if (!removeLocalBackground(luminance, width, height, starSignal) ||
        !detector.detect(starSignal, width, height, stars, options)) {
        return false;
    }

    std::vector<double> fwhm;
    std::vector<double> ellipticity;
    std::vector<double> flux;
    fwhm.reserve(stars.size());
    ellipticity.reserve(stars.size());
    flux.reserve(stars.size());
    for (const StarPoint& star : stars) {
        if (!std::isfinite(star.fwhm) || !std::isfinite(star.ellipticity) ||
            !std::isfinite(star.flux) || star.fwhm < 0.6 || star.fwhm > 20.0 ||
            star.ellipticity < 0.0 || star.ellipticity > 0.95 ||
            star.flux <= 0.0) {
            continue;
        }
        fwhm.push_back(star.fwhm);
        ellipticity.push_back(star.ellipticity);
        flux.push_back(star.flux);
    }

    metrics.detectedStars = static_cast<int>(stars.size());
    metrics.usableStars = static_cast<int>(fwhm.size());
    metrics.backgroundMedian = background;
    metrics.backgroundNoise = noise;
    metrics.clippedFraction = samples.empty()
        ? 0.0 : static_cast<double>(clipped) / samples.size();
    if (fwhm.size() < 8) return false;
    metrics.medianFwhm = median(std::move(fwhm));
    metrics.medianEllipticity = median(std::move(ellipticity));
    metrics.medianFlux = median(std::move(flux));
    metrics.valid = true;
    return true;
}

bool FrameQualityEvaluator::selectSequence(
    std::vector<FrameQualityMetrics>& metrics, size_t preferredCenterIndex,
    bool rejectSevereOutliers, FrameQualitySelection& selection) {
    selection = {};
    if (metrics.empty()) return false;
    preferredCenterIndex = std::min(preferredCenterIndex, metrics.size() - 1);

    std::vector<size_t> validIndices;
    for (size_t index = 0; index < metrics.size(); ++index) {
        if (metrics[index].valid) validIndices.push_back(index);
    }
    if (validIndices.empty()) return false;

    const SequenceCenters centers = sequenceCenters(metrics, validIndices);
    const double centerDenominator =
        std::max<size_t>(1, std::max(preferredCenterIndex,
                                    metrics.size() - 1 - preferredCenterIndex));
    for (size_t index : validIndices) {
        FrameQualityMetrics& item = metrics[index];
        const double starsZ = robustZ(
            std::log1p(item.usableStars), centers.logStars,
            centers.logStarsMad, 0.08);
        const double fwhmZ = robustZ(
            item.medianFwhm, centers.fwhm, centers.fwhmMad,
            std::max(0.05, centers.fwhm * 0.03));
        const double ellipticityZ = robustZ(
            item.medianEllipticity, centers.ellipticity,
            centers.ellipticityMad, 0.02);
        const double noiseZ = robustZ(
            item.backgroundNoise, centers.noise, centers.noiseMad,
            std::max(1.0, centers.noise * 0.05));
        const double clippedZ = robustZ(
            item.clippedFraction, centers.clipped, centers.clippedMad, 0.002);
        const double centrality = 1.0 -
            std::abs(static_cast<double>(index) - preferredCenterIndex) /
                centerDenominator;
        item.score = 0.45 * starsZ - 0.35 * fwhmZ -
            0.15 * ellipticityZ - 0.05 * noiseZ -
            0.10 * clippedZ + 0.15 * centrality;
    }

    selection.rejected.assign(metrics.size(), false);
    if (rejectSevereOutliers && validIndices.size() >= 4) {
        std::vector<double> scores;
        scores.reserve(validIndices.size());
        for (size_t index : validIndices) scores.push_back(metrics[index].score);
        const double scoreCenter = median(scores);
        const double scoreMad = medianAbsoluteDeviation(scores, scoreCenter);
        const double scoreCutoff =
            scoreCenter - std::max(1.75, scoreMad * 1.4826 * 2.5);

        for (size_t index : validIndices) {
            const FrameQualityMetrics& item = metrics[index];
            const bool severe =
                item.usableStars < std::max(8.0, centers.starCount * 0.35) ||
                item.medianFwhm > centers.fwhm * 1.65 ||
                item.medianEllipticity >
                    std::max(0.65, centers.ellipticity + 0.25) ||
                (item.backgroundNoise > centers.noise * 2.5 &&
                 item.usableStars < centers.starCount * 0.70) ||
                (item.clippedFraction >
                     std::max(0.12, centers.clipped + 0.08) &&
                 item.usableStars < centers.starCount * 0.70);
            selection.rejected[index] = severe && item.score < scoreCutoff;
        }
    }

    // Quality scoring must never make a valid sequence unprocessable.
    size_t acceptedCount = metrics.size() - static_cast<size_t>(
        std::count(selection.rejected.begin(), selection.rejected.end(), true));
    if (acceptedCount < 2) {
        std::vector<size_t> ranked = validIndices;
        std::sort(ranked.begin(), ranked.end(), [&](size_t left, size_t right) {
            return metrics[left].score > metrics[right].score;
        });
        for (size_t index : ranked) {
            if (!selection.rejected[index]) continue;
            selection.rejected[index] = false;
            if (++acceptedCount >= 2) break;
        }
    }

    const bool hasAcceptedValid = std::any_of(
        validIndices.begin(), validIndices.end(), [&](size_t index) {
            return !selection.rejected[index];
        });
    if (!hasAcceptedValid) {
        const size_t bestValid = *std::max_element(
            validIndices.begin(), validIndices.end(),
            [&](size_t left, size_t right) {
                return metrics[left].score < metrics[right].score;
            });
        selection.rejected[bestValid] = false;
    }

    size_t bestIndex = validIndices.front();
    double bestScore = -std::numeric_limits<double>::infinity();
    for (size_t index : validIndices) {
        if (selection.rejected[index]) continue;
        if (metrics[index].score > bestScore) {
            bestScore = metrics[index].score;
            bestIndex = index;
        }
    }
    selection.referenceIndex = bestIndex;
    selection.validMetricCount = validIndices.size();
    return true;
}
