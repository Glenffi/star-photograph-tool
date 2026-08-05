#include "TimelapseEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double kMadToStandardDeviation = 1.4826;
constexpr double kTargetTimeTolerance = 1e-12;

struct WeightedSample {
    uint16_t rgb[3];
    double weight;
};

bool checkedRgbSize(int width, int height, size_t& valueCount) {
    if (width <= 0 || height <= 0) return false;

    const size_t unsignedWidth = static_cast<size_t>(width);
    const size_t unsignedHeight = static_cast<size_t>(height);
    if (unsignedWidth > std::numeric_limits<size_t>::max() / unsignedHeight) {
        return false;
    }

    const size_t pixelCount = unsignedWidth * unsignedHeight;
    if (pixelCount > std::numeric_limits<size_t>::max() / 3) return false;
    valueCount = pixelCount * 3;
    return true;
}

double median(std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if (values.size() % 2 != 0) return values[middle];
    return (values[middle - 1] + values[middle]) * 0.5;
}

double robustEstimate(const std::vector<WeightedSample>& samples,
                      int channel,
                      const TimelapseEngine::Options& options,
                      uint16_t fallback,
                      std::vector<double>& values,
                      std::vector<double>& deviations) {
    values.clear();
    for (const WeightedSample& sample : samples) {
        values.push_back(sample.rgb[channel]);
    }
    if (values.empty()) return fallback;

    const double center = median(values);
    deviations.clear();
    for (double value : values) {
        deviations.push_back(std::abs(value - center));
    }

    // 1.4826 把正态分布的 MAD 换算为标准差估计。minimumDeviation 负责
    // MAD=0 的小窗口，避免把 1~2 DN 的正常量化变化全部裁掉。
    const double robustSigma = std::max(
        options.minimumDeviation,
        kMadToStandardDeviation * median(deviations));
    const double rejectionRadius = options.madThreshold * robustSigma;

    double weightedSum = 0.0;
    double totalWeight = 0.0;
    for (const WeightedSample& sample : samples) {
        const double value = sample.rgb[channel];
        if (std::abs(value - center) <= rejectionRadius && sample.weight > 0.0) {
            weightedSum += value * sample.weight;
            totalWeight += sample.weight;
        }
    }

    // 极大的时间距离可能让所有高斯权重下溢为 0；目标帧仍是确定的回退。
    return totalWeight > 0.0 ? weightedSum / totalWeight : fallback;
}

uint16_t blend(uint16_t target, double estimate, double amount) {
    const double value = static_cast<double>(target) * (1.0 - amount) +
                         estimate * amount;
    return static_cast<uint16_t>(std::clamp(std::lround(value), 0L, 65535L));
}
} // namespace

TimelapseEngine::Result TimelapseEngine::denoise(
    const std::vector<FrameView>& frames,
    int width,
    int height,
    size_t targetFrameIndex,
    const Options& options) {
    Result result;
    if (frames.empty()) {
        result.error = Error::EmptyInput;
        return result;
    }
    if (targetFrameIndex >= frames.size()) {
        result.error = Error::InvalidTargetIndex;
        return result;
    }
    if (options.windowSize < 3 || options.windowSize % 2 == 0) {
        result.error = Error::InvalidWindowSize;
        return result;
    }
    if (!std::isfinite(options.temporalSigma) || options.temporalSigma <= 0.0 ||
        !std::isfinite(options.madThreshold) || options.madThreshold < 0.0 ||
        !std::isfinite(options.minimumDeviation) || options.minimumDeviation < 0.0 ||
        !std::isfinite(options.strength) ||
        options.strength < 0.0 || options.strength > 100.0) {
        result.error = Error::InvalidOptions;
        return result;
    }

    size_t expectedValueCount = 0;
    if (!checkedRgbSize(width, height, expectedValueCount)) {
        // 正数尺寸失败只可能来自 size_t 乘法溢出。
        result.error = (width <= 0 || height <= 0)
            ? Error::InvalidDimensions : Error::SizeOverflow;
        return result;
    }
    if (expectedValueCount > result.rgb.max_size()) {
        result.error = Error::SizeOverflow;
        return result;
    }

    double previousDistance = -std::numeric_limits<double>::infinity();
    for (const FrameView& frame : frames) {
        if (frame.rgb == nullptr || frame.valueCount != expectedValueCount) {
            result.error = Error::InvalidFrame;
            return result;
        }
        if (!std::isfinite(frame.timeDistance)) {
            result.error = Error::InvalidTimeDistance;
            return result;
        }
        if (frame.timeDistance < previousDistance) {
            result.error = Error::InvalidTimeOrder;
            return result;
        }
        previousDistance = frame.timeDistance;
    }
    if (std::abs(frames[targetFrameIndex].timeDistance) > kTargetTimeTolerance) {
        result.error = Error::InvalidTimeDistance;
        return result;
    }

    const size_t radius = options.windowSize / 2;
    const size_t first = targetFrameIndex > radius
        ? targetFrameIndex - radius : 0;
    const size_t remainingAfterTarget = frames.size() - 1 - targetFrameIndex;
    const size_t last = targetFrameIndex + std::min(radius, remainingAfterTarget) + 1;
    result.firstFrameIndex = first;
    result.frameCount = last - first;

    const uint16_t* target = frames[targetFrameIndex].rgb;
    result.rgb.resize(expectedValueCount);
    if (options.strength == 0.0 || result.frameCount == 1) {
        std::copy(target, target + expectedValueCount, result.rgb.begin());
        return result;
    }

    std::vector<double> frameWeights;
    frameWeights.reserve(result.frameCount);
    for (size_t frameIndex = first; frameIndex < last; ++frameIndex) {
        const double distance = frames[frameIndex].timeDistance;
        if (distance == 0.0) {
            // 即使 sigma 极小到其平方下溢，目标帧的高斯权重也必须精确为 1。
            frameWeights.push_back(1.0);
            continue;
        }
        const double normalizedDistance = distance / options.temporalSigma;
        const double exponent = -0.5 * normalizedDistance * normalizedDistance;
        frameWeights.push_back(std::isfinite(exponent) ? std::exp(exponent) : 0.0);
    }

    std::vector<WeightedSample> samples;
    std::vector<double> values;
    std::vector<double> deviations;
    samples.reserve(result.frameCount);
    values.reserve(result.frameCount);
    deviations.reserve(result.frameCount);

    const double blendAmount = options.strength / 100.0;
    for (size_t offset = 0; offset < expectedValueCount; offset += 3) {
        samples.clear();
        for (size_t frameIndex = first; frameIndex < last; ++frameIndex) {
            const uint16_t* pixel = frames[frameIndex].rgb + offset;
            // 几何重采样通常用 (0,0,0) 标记画布外区域。只忽略三通道全零；
            // (0,G,B) 等颜色仍是合法样本，不能按单通道分别丢弃。
            if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) continue;
            samples.push_back({
                {pixel[0], pixel[1], pixel[2]},
                frameWeights[frameIndex - first]
            });
        }

        for (int channel = 0; channel < 3; ++channel) {
            const uint16_t fallback = target[offset + static_cast<size_t>(channel)];
            const double estimate = robustEstimate(
                samples, channel, options, fallback, values, deviations);
            result.rgb[offset + static_cast<size_t>(channel)] =
                blend(fallback, estimate, blendAmount);
        }
    }
    return result;
}

TimelapseEngine::Result TimelapseEngine::denoise(
    const std::vector<FrameView>& frames,
    int width,
    int height,
    size_t targetFrameIndex) {
    return denoise(frames, width, height, targetFrameIndex, Options{});
}

const char* TimelapseEngine::errorMessage(Error error) noexcept {
    switch (error) {
    case Error::None: return "success";
    case Error::EmptyInput: return "no input frames";
    case Error::InvalidDimensions: return "width and height must be positive";
    case Error::SizeOverflow: return "RGB image size overflows addressable memory";
    case Error::InvalidTargetIndex: return "target frame index is out of range";
    case Error::InvalidWindowSize: return "window size must be an odd number of at least 3";
    case Error::InvalidOptions: return "denoise options are outside their valid ranges";
    case Error::InvalidFrame: return "a frame has a null pointer or unexpected RGB size";
    case Error::InvalidTimeDistance: return "time distances must be finite and target distance must be zero";
    case Error::InvalidTimeOrder: return "frames must be ordered by increasing time distance";
    }
    return "unknown timelapse error";
}
