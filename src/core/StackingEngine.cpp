#include "StackingEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
bool imageSize(int width, int height, size_t& size) {
    if (width <= 0 || height <= 0) return false;
    if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() /
                                     static_cast<size_t>(height)) return false;
    size = static_cast<size_t>(width) * static_cast<size_t>(height);
    return true;
}

bool rgbImageSize(int width, int height, size_t& size) {
    if (!imageSize(width, height, size) ||
        size > std::numeric_limits<size_t>::max() / 3) {
        return false;
    }
    size *= 3;
    return true;
}

bool validFrames(const std::vector<std::vector<uint16_t>>& images,
                 size_t expectedSize) {
    if (images.empty()) return false;
    return std::all_of(images.begin(), images.end(), [expectedSize](const auto& image) {
        return image.size() == expectedSize;
    });
}

double medianOfSorted(const std::vector<uint16_t>& values,
                      size_t begin, size_t end) {
    const size_t count = end - begin;
    const size_t middle = begin + count / 2;
    if (count % 2 != 0) return values[middle];
    return (static_cast<double>(values[middle - 1]) + values[middle]) / 2.0;
}

double standardDeviation(const std::vector<uint16_t>& values,
                         size_t begin, size_t end, double center) {
    double sum = 0.0;
    for (size_t index = begin; index < end; ++index) {
        const uint16_t value = values[index];
        const double delta = static_cast<double>(value) - center;
        sum += delta * delta;
    }
    return std::sqrt(sum / (end - begin));
}

double medianAbsoluteDeviation(const std::vector<uint16_t>& values, double median,
                               std::vector<double>& deviations) {
    deviations.clear();
    for (uint16_t value : values) {
        deviations.push_back(std::abs(static_cast<double>(value) - median));
    }
    const size_t middle = deviations.size() / 2;
    std::nth_element(deviations.begin(), deviations.begin() + middle, deviations.end());
    if (deviations.size() % 2 != 0) return deviations[middle];
    return (deviations[middle] +
            *std::max_element(deviations.begin(), deviations.begin() + middle)) / 2.0;
}

bool stackSamples(const std::vector<std::vector<uint16_t>>& images,
                  size_t sampleCount, StackingEngine::Method method, double kappa,
                  std::vector<uint16_t>& result, bool ignoreZero) {
    if (method < StackingEngine::Average || method > StackingEngine::Winsorized ||
        !validFrames(images, sampleCount) ||
        ((method == StackingEngine::KappaSigma || method == StackingEngine::Winsorized) &&
         (!std::isfinite(kappa) || kappa <= 0.0))) {
        return false;
    }

    result.resize(sampleCount);
    if (method == StackingEngine::Average) {
        for (size_t sample = 0; sample < sampleCount; ++sample) {
            uint64_t sum = 0;
            size_t count = 0;
            for (const auto& image : images) {
                const uint16_t value = image[sample];
                if (!ignoreZero || value != 0) {
                    sum += value;
                    ++count;
                }
            }
            result[sample] = count == 0 ? 0 : static_cast<uint16_t>(sum / count);
        }
        return true;
    }

    // These buffers are reused for every sample. With high-resolution RGB data,
    // allocating them inside the pixel loop would cause hundreds of millions of
    // small heap operations.
    std::vector<uint16_t> values;
    std::vector<double> deviations;
    values.reserve(images.size());
    deviations.reserve(images.size());

    constexpr double kMadToStdDev = 1.4826;
    for (size_t sample = 0; sample < sampleCount; ++sample) {
        values.clear();
        for (const auto& image : images) {
            const uint16_t value = image[sample];
            if (!ignoreZero || value != 0) values.push_back(value);
        }
        if (values.empty()) {
            result[sample] = 0;
            continue;
        }
        std::sort(values.begin(), values.end());

        if (method == StackingEngine::Median) {
            result[sample] = static_cast<uint16_t>(
                std::lround(medianOfSorted(values, 0, values.size())));
            continue;
        }

        if (method == StackingEngine::Winsorized) {
            const double median = medianOfSorted(values, 0, values.size());
            const double threshold = kappa *
                medianAbsoluteDeviation(values, median, deviations) * kMadToStdDev;
            const double lower = median - threshold;
            const double upper = median + threshold;
            double sum = 0.0;
            for (uint16_t value : values) {
                sum += std::clamp(static_cast<double>(value), lower, upper);
            }
            result[sample] = static_cast<uint16_t>(std::clamp(
                std::lround(sum / values.size()), 0L, 65535L));
            continue;
        }

        // Values stay sorted while sigma clipping shrinks the accepted range.
        // This replaces three median copies/nth_element calls and two filter
        // buffers per sample with index updates into one reusable vector.
        size_t activeBegin = 0;
        size_t activeEnd = values.size();
        for (int iteration = 0; iteration < 3; ++iteration) {
            const double median = medianOfSorted(values, activeBegin, activeEnd);
            const double deviation =
                standardDeviation(values, activeBegin, activeEnd, median);
            if (deviation == 0.0) break;
            const double threshold = kappa * deviation;
            const double lower = median - threshold;
            const double upper = median + threshold;
            const auto beginIterator = values.begin() +
                static_cast<std::ptrdiff_t>(activeBegin);
            const auto endIterator = values.begin() +
                static_cast<std::ptrdiff_t>(activeEnd);
            const size_t clippedBegin = static_cast<size_t>(
                std::lower_bound(beginIterator, endIterator, lower) - values.begin());
            const size_t clippedEnd = static_cast<size_t>(
                std::upper_bound(beginIterator, endIterator, upper) - values.begin());
            if (clippedBegin == clippedEnd ||
                (clippedBegin == activeBegin && clippedEnd == activeEnd)) {
                break;
            }
            activeBegin = clippedBegin;
            activeEnd = clippedEnd;
        }
        uint64_t sum = 0;
        for (size_t index = activeBegin; index < activeEnd; ++index) {
            sum += values[index];
        }
        result[sample] = static_cast<uint16_t>(
            sum / (activeEnd - activeBegin));
    }
    return true;
}
}

bool StackingEngine::stack(const std::vector<std::vector<uint16_t>>& images,
                             int width, int height,
                             Method method,
                             double kappa,
                             std::vector<uint16_t>& result,
                             bool ignoreZero) {
    size_t expectedSize = 0;
    if (images.empty() || !imageSize(width, height, expectedSize)) {
        return false;
    }

    return stackSamples(images, expectedSize, method, kappa, result, ignoreZero);
}

bool StackingEngine::stackRgb(const std::vector<std::vector<uint16_t>>& images,
                              int width, int height, Method method, double kappa,
                              std::vector<uint16_t>& result, bool ignoreZero) {
    size_t expectedSize = 0;
    return rgbImageSize(width, height, expectedSize) &&
        stackSamples(images, expectedSize, method, kappa, result, ignoreZero);
}

StackingEngine::Method StackingEngine::recommendMethod(int frameCount) {
    if (frameCount <= 5) return Median;
    if (frameCount <= 15) return KappaSigma;
    return Winsorized;
}

bool StackingEngine::stackWithMask(
    const std::vector<std::vector<uint16_t>>& images,
    const std::vector<std::vector<uint16_t>>& originalImages,
    int width, int height,
    Method method, double kappa,
    const std::vector<uint8_t>& mask,
    std::vector<uint16_t>& result,
    GroundMethod groundMethod)
{
    size_t expectedSize = 0;
    if (images.empty() || originalImages.empty() ||
        !imageSize(width, height, expectedSize)) {
        return false;
    }
    if (groundMethod != GroundReferenceFrame &&
        images.size() != originalImages.size()) {
        return false;
    }
    if (mask.size() != expectedSize) {
        return false;
    }
    for (const auto& img : images) {
        if (img.size() != expectedSize) {
            return false;
        }
    }
    for (const auto& img : originalImages) {
        if (img.size() != expectedSize) {
            return false;
        }
    }

    // The worker has already zeroed terrain samples whose transformed source
    // mask does not cover the reference sky. Ignore those per-frame invalid
    // samples here, then use the reference mask only for the final soft blend.
    std::vector<uint16_t> skyResult;
    if (!stack(images, width, height, method, kappa, skyResult, true)) {
        return false;
    }

    std::vector<uint16_t> groundResult;
    if (groundMethod == GroundReferenceFrame) {
        groundResult = originalImages.front();
    } else {
        const Method groundStackMethod = groundMethod == GroundMedian
            ? Median : Average;
        if (!stack(originalImages, width, height, groundStackMethod, kappa,
                   groundResult, false)) {
            return false;
        }
    }

    // 3. 融合：result = sky * (mask/255) + ground * (1 - mask/255)
    result.resize(width * height);
    for (size_t i = 0; i < result.size(); ++i) {
        double alpha = mask[i] / 255.0;
        double val = skyResult[i] * alpha + groundResult[i] * (1.0 - alpha);
        result[i] = static_cast<uint16_t>(std::min(65535.0, val));
    }
    return true;
}
