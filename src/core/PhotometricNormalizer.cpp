#include "PhotometricNormalizer.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>
#include <utility>

namespace {

uint16_t luminanceOf(const uint16_t* rgb) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(rgb[0]) * 13933 +
         static_cast<uint32_t>(rgb[1]) * 46871 +
         static_cast<uint32_t>(rgb[2]) * 4732) /
        65536);
}

bool validRgb(const std::vector<uint16_t>& rgb, int width, int height,
              size_t& pixelCount) {
    if (width <= 0 || height <= 0 || width > INT_MAX / height) return false;
    pixelCount = static_cast<size_t>(width) * height;
    return pixelCount <= std::numeric_limits<size_t>::max() / 3 &&
        rgb.size() == pixelCount * 3;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    if (values.size() % 2 != 0) return values[middle];
    const double lower =
        *std::max_element(values.begin(), values.begin() + middle);
    return (lower + values[middle]) * 0.5;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) return 0.0;
    const size_t index = static_cast<size_t>(std::clamp(
        fraction * static_cast<double>(values.size() - 1),
        0.0, static_cast<double>(values.size() - 1)));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

struct SamplePair {
    const PhotometricReferenceSample* reference = nullptr;
    const uint16_t* sourceRgb = nullptr;
    double sourceLuminance = 0.0;
    double referenceLuminance = 0.0;
};

bool fitSymmetricLinear(const std::vector<const SamplePair*>& pairs,
                        double& gain, double& offset) {
    if (pairs.size() < 128) return false;
    double sourceMean = 0.0;
    double referenceMean = 0.0;
    for (const SamplePair* pair : pairs) {
        sourceMean += pair->sourceLuminance;
        referenceMean += pair->referenceLuminance;
    }
    sourceMean /= pairs.size();
    referenceMean /= pairs.size();

    double variance = 0.0;
    double referenceVariance = 0.0;
    double covariance = 0.0;
    for (const SamplePair* pair : pairs) {
        const double sourceDelta = pair->sourceLuminance - sourceMean;
        const double referenceDelta =
            pair->referenceLuminance - referenceMean;
        variance += sourceDelta * sourceDelta;
        referenceVariance += referenceDelta * referenceDelta;
        covariance += sourceDelta * referenceDelta;
    }
    // A nearly flat field cannot distinguish multiplicative exposure from an
    // additive offset. Let the caller skip normalization instead of guessing.
    if (variance / pairs.size() < 64.0 * 64.0 ||
        referenceVariance / pairs.size() < 64.0 * 64.0 ||
        covariance <= 0.0) {
        return false;
    }
    // Both RAW frames contain sensor noise. Ordinary y-on-x least squares
    // treats the source as noise-free and systematically pulls gains toward
    // zero in dark skies. Reduced-major-axis regression is symmetric and
    // estimates the relative photometric scale from both dispersions.
    gain = std::sqrt(referenceVariance / variance);
    offset = referenceMean - gain * sourceMean;
    return std::isfinite(gain) && std::isfinite(offset);
}

bool fitRobustInitial(const std::vector<const SamplePair*>& pairs,
                      double& gain, double& offset) {
    if (pairs.size() < 128) return false;

    // Estimate a Theil-Sen-style slope from several deterministic, widely
    // separated pair sets. A local cloud or moving foreground then corrupts
    // fewer than half of the slopes as long as most of the frame is stable.
    std::vector<double> slopes;
    slopes.reserve(pairs.size() * 3);
    const std::array<size_t, 3> gaps = {
        pairs.size() / 2, pairs.size() / 3, pairs.size() / 5
    };
    for (size_t gap : gaps) {
        if (gap == 0) continue;
        for (size_t index = 0; index + gap < pairs.size(); ++index) {
            const double sourceDelta =
                pairs[index + gap]->sourceLuminance -
                pairs[index]->sourceLuminance;
            if (std::abs(sourceDelta) < 256.0) continue;
            const double referenceDelta =
                pairs[index + gap]->referenceLuminance -
                pairs[index]->referenceLuminance;
            const double slope = referenceDelta / sourceDelta;
            if (std::isfinite(slope) && slope >= 0.25 && slope <= 4.0) {
                slopes.push_back(slope);
            }
        }
    }
    if (slopes.size() < 128) return false;
    gain = median(std::move(slopes));

    std::vector<double> offsets;
    offsets.reserve(pairs.size());
    for (const SamplePair* pair : pairs) {
        offsets.push_back(pair->referenceLuminance -
                          gain * pair->sourceLuminance);
    }
    offset = median(std::move(offsets));
    return std::isfinite(gain) && std::isfinite(offset);
}

} // namespace

bool PhotometricNormalizer::buildReferenceProfile(
    const std::vector<uint16_t>& reference, int width, int height,
    PhotometricReferenceProfile& profile, size_t maxSamples) {
    size_t pixelCount = 0;
    if (!validRgb(reference, width, height, pixelCount) || maxSamples < 256) {
        return false;
    }

    const double samplesRatio =
        static_cast<double>(pixelCount) / static_cast<double>(maxSamples);
    const int step = std::max(1, static_cast<int>(
        std::ceil(std::sqrt(std::max(1.0, samplesRatio)))));
    PhotometricReferenceProfile candidate;
    candidate.width = width;
    candidate.height = height;
    candidate.samples.reserve(std::min(pixelCount, maxSamples));
    for (int y = step / 2; y < height; y += step) {
        for (int x = step / 2; x < width; x += step) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            PhotometricReferenceSample sample;
            sample.pixelIndex = pixel;
            for (int channel = 0; channel < 3; ++channel) {
                sample.rgb[channel] = reference[pixel * 3 + channel];
            }
            sample.luminance = luminanceOf(sample.rgb.data());
            candidate.samples.push_back(sample);
        }
    }
    if (candidate.samples.size() < 256) return false;
    profile = std::move(candidate);
    return true;
}

bool PhotometricNormalizer::estimate(
    const PhotometricReferenceProfile& reference,
    const std::vector<uint16_t>& source, PhotometricModel& model) {
    model = {};
    size_t pixelCount = 0;
    if (!validRgb(source, reference.width, reference.height, pixelCount) ||
        reference.samples.size() < 256) {
        return false;
    }

    std::vector<SamplePair> pairs;
    pairs.reserve(reference.samples.size());
    std::vector<double> sourceValues;
    std::vector<double> referenceValues;
    sourceValues.reserve(reference.samples.size());
    referenceValues.reserve(reference.samples.size());
    for (const PhotometricReferenceSample& sample : reference.samples) {
        if (sample.pixelIndex >= pixelCount) return false;
        const uint16_t* sourceRgb = &source[sample.pixelIndex * 3];
        const uint16_t sourceLuminance = luminanceOf(sourceRgb);
        if (sourceLuminance < 256 || sample.luminance < 256 ||
            sourceLuminance > 64500 || sample.luminance > 64500) {
            continue;
        }
        pairs.push_back({&sample, sourceRgb,
                         static_cast<double>(sourceLuminance),
                         static_cast<double>(sample.luminance)});
        sourceValues.push_back(sourceLuminance);
        referenceValues.push_back(sample.luminance);
    }
    if (pairs.size() < 512) return false;

    const double sourceLow = percentile(sourceValues, 0.05);
    const double sourceHigh = percentile(sourceValues, 0.90);
    const double referenceLow = percentile(referenceValues, 0.05);
    const double referenceHigh = percentile(referenceValues, 0.90);
    std::vector<const SamplePair*> fittingPairs;
    fittingPairs.reserve(pairs.size());
    for (const SamplePair& pair : pairs) {
        if (pair.sourceLuminance >= sourceLow &&
            pair.sourceLuminance <= sourceHigh &&
            pair.referenceLuminance >= referenceLow &&
            pair.referenceLuminance <= referenceHigh) {
            fittingPairs.push_back(&pair);
        }
    }

    double gain = 1.0;
    double luminanceOffset = 0.0;
    if (!fitRobustInitial(fittingPairs, gain, luminanceOffset) ||
        gain < 0.5 || gain > 2.0) {
        return false;
    }

    std::vector<double> residuals;
    residuals.reserve(fittingPairs.size());
    for (const SamplePair* pair : fittingPairs) {
        residuals.push_back(pair->referenceLuminance -
            (gain * pair->sourceLuminance + luminanceOffset));
    }
    const double residualMedian = median(residuals);
    std::vector<double> absoluteDeviations;
    absoluteDeviations.reserve(residuals.size());
    for (double residual : residuals) {
        absoluteDeviations.push_back(std::abs(residual - residualMedian));
    }
    const double residualMad = median(absoluteDeviations);
    const double residualLimit = std::max(48.0, residualMad * 1.4826 * 3.5);

    std::vector<const SamplePair*> inliers;
    inliers.reserve(fittingPairs.size());
    for (size_t index = 0; index < fittingPairs.size(); ++index) {
        if (std::abs(residuals[index] - residualMedian) <= residualLimit) {
            inliers.push_back(fittingPairs[index]);
        }
    }
    if (inliers.size() < 256 ||
        inliers.size() * 2 < fittingPairs.size() ||
        !fitSymmetricLinear(inliers, gain, luminanceOffset) ||
        gain < 0.5 || gain > 2.0) {
        return false;
    }

    std::array<double, 3> offsets = {};
    for (int channel = 0; channel < 3; ++channel) {
        std::vector<double> channelOffsets;
        channelOffsets.reserve(inliers.size());
        for (const SamplePair* pair : inliers) {
            channelOffsets.push_back(
                static_cast<double>(pair->reference->rgb[channel]) -
                gain * pair->sourceRgb[channel]);
        }
        offsets[channel] = median(std::move(channelOffsets));
        if (!std::isfinite(offsets[channel]) ||
            std::abs(offsets[channel]) > 16384.0) {
            return false;
        }
    }

    model.gain = gain;
    model.offsets = offsets;
    model.sampleCount = fittingPairs.size();
    model.inlierCount = inliers.size();
    model.residualMad = residualMad;
    return true;
}

bool PhotometricNormalizer::applyInPlace(
    std::vector<uint16_t>& rgb, int width, int height,
    const PhotometricModel& model) {
    size_t pixelCount = 0;
    if (!validRgb(rgb, width, height, pixelCount) ||
        !std::isfinite(model.gain) || model.gain < 0.5 || model.gain > 2.0) {
        return false;
    }
    for (double offset : model.offsets) {
        if (!std::isfinite(offset) || std::abs(offset) > 16384.0) {
            return false;
        }
    }

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        // RGB warping uses an all-zero triplet to mark pixels outside the
        // source image. Keep that sentinel intact so stacking can ignore it.
        if (rgb[base] == 0 && rgb[base + 1] == 0 && rgb[base + 2] == 0) {
            continue;
        }
        for (int channel = 0; channel < 3; ++channel) {
            const size_t index = base + channel;
            const double corrected =
                model.gain * rgb[index] + model.offsets[channel];
            rgb[index] = static_cast<uint16_t>(std::lround(
                std::clamp(corrected, 0.0, 65535.0)));
        }
    }
    return true;
}
