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

double medianOf(const std::vector<uint16_t>& values,
                std::vector<uint16_t>& ordered) {
    ordered.assign(values.begin(), values.end());
    const size_t middle = ordered.size() / 2;
    std::nth_element(ordered.begin(), ordered.begin() + middle, ordered.end());
    double median = ordered[middle];
    if (ordered.size() % 2 == 0) {
        median = (median + *std::max_element(ordered.begin(),
                                             ordered.begin() + middle)) / 2.0;
    }
    return median;
}

double standardDeviation(const std::vector<uint16_t>& values, double center) {
    double sum = 0.0;
    for (uint16_t value : values) {
        const double delta = static_cast<double>(value) - center;
        sum += delta * delta;
    }
    return std::sqrt(sum / values.size());
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
    std::vector<uint16_t> active;
    std::vector<uint16_t> ordered;
    std::vector<uint16_t> filtered;
    std::vector<double> deviations;
    for (auto* buffer : {&values, &active, &ordered, &filtered}) {
        buffer->reserve(images.size());
    }
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

        if (method == StackingEngine::Median) {
            result[sample] = static_cast<uint16_t>(std::lround(medianOf(values, ordered)));
            continue;
        }

        if (method == StackingEngine::Winsorized) {
            const double median = medianOf(values, ordered);
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

        active.assign(values.begin(), values.end());
        for (int iteration = 0; iteration < 3; ++iteration) {
            const double median = medianOf(active, ordered);
            const double deviation = standardDeviation(active, median);
            if (deviation == 0.0) break;
            const double threshold = kappa * deviation;
            filtered.clear();
            for (uint16_t value : active) {
                if (std::abs(static_cast<double>(value) - median) <= threshold) {
                    filtered.push_back(value);
                }
            }
            if (filtered.empty() || filtered.size() == active.size()) break;
            active.swap(filtered);
        }
        uint64_t sum = 0;
        for (uint16_t value : active) sum += value;
        result[sample] = static_cast<uint16_t>(sum / active.size());
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
    std::vector<uint16_t>& result)
{
    size_t expectedSize = 0;
    if (images.empty() || originalImages.empty() ||
        !imageSize(width, height, expectedSize)) {
        return false;
    }
    if (images.size() != originalImages.size()) {
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

    // Each output pixel is stacked independently, so masking every input frame
    // first only duplicates memory. Stack both sources once, then blend them.
    std::vector<uint16_t> skyResult;
    if (!stack(images, width, height, method, kappa, skyResult, true)) {
        return false;
    }

    std::vector<uint16_t> groundResult;
    if (!stack(originalImages, width, height, Average, kappa, groundResult, false)) {
        return false;
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
