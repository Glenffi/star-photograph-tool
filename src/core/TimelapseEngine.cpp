#include "TimelapseEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {
constexpr double kMadToStandardDeviation = 1.4826;
constexpr double kTargetTimeTolerance = 1e-12;

struct WeightedSample {
    uint16_t rgb[3];
    double weight;
    bool target = false;
};

uint16_t luminanceOf(const uint16_t* rgb) {
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(rgb[0]) * 13933U +
         static_cast<uint32_t>(rgb[1]) * 46871U +
         static_cast<uint32_t>(rgb[2]) * 4732U) /
        65536U);
}

struct MotionGuide {
    int width = 0;
    std::vector<uint16_t> luminance;
};

MotionGuide buildMotionGuide(const uint16_t* rgb, int width, int height) {
    MotionGuide guide;
    guide.width = (width + 1) / 2;
    const int guideHeight = (height + 1) / 2;
    guide.luminance.resize(
        static_cast<size_t>(guide.width) * guideHeight);
    for (int guideY = 0; guideY < guideHeight; ++guideY) {
        for (int guideX = 0; guideX < guide.width; ++guideX) {
            std::array<uint16_t, 4> values = {};
            size_t count = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const int y = guideY * 2 + dy;
                if (y >= height) continue;
                for (int dx = 0; dx < 2; ++dx) {
                    const int x = guideX * 2 + dx;
                    if (x >= width) continue;
                    const size_t pixel =
                        static_cast<size_t>(y) * width + x;
                    values[count++] = luminanceOf(rgb + pixel * 3);
                }
            }
            std::sort(values.begin(), values.begin() + count);
            const size_t middle = count / 2;
            const uint32_t value = count % 2 != 0
                ? values[middle]
                : (static_cast<uint32_t>(values[middle - 1]) +
                   values[middle]) / 2U;
            guide.luminance[
                static_cast<size_t>(guideY) * guide.width + guideX] =
                static_cast<uint16_t>(value);
        }
    }
    return guide;
}

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
    if (samples.empty()) return fallback;

    values.clear();
    double targetWeight = 0.0;
    double neighborWeight = 0.0;
    for (const WeightedSample& sample : samples) {
        values.push_back(sample.rgb[channel]);
        if (sample.target) targetWeight += sample.weight;
        else neighborWeight += sample.weight;
    }
    // Small temporal windows use an ordinary median so a hot target pixel is
    // still rejected. Only when motion confidence has almost removed every
    // neighbor do we center rejection on the target's real local structure.
    const double center = targetWeight > 0.0 &&
                          neighborWeight < targetWeight * 0.4
        ? static_cast<double>(fallback) : median(values);
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
        options.strength < 0.0 || options.strength > 100.0 ||
        !std::isfinite(options.motionProtection) ||
        options.motionProtection < 0.0 || options.motionProtection > 100.0) {
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

    std::vector<MotionGuide> motionGuides;
    if (options.motionProtection > 0.0) {
        motionGuides.reserve(result.frameCount);
        for (size_t frameIndex = first; frameIndex < last; ++frameIndex) {
            motionGuides.push_back(buildMotionGuide(
                frames[frameIndex].rgb, width, height));
        }
    }

    const double blendAmount = options.strength / 100.0;
    const double protectionAmount = options.motionProtection / 100.0;
    const size_t targetGuideIndex = targetFrameIndex - first;
    for (size_t offset = 0; offset < expectedValueCount; offset += 3) {
        samples.clear();
        bool motionProtected = false;
        const size_t pixelIndex = offset / 3;
        const size_t guidePixel = motionGuides.empty() ? 0
            : static_cast<size_t>((pixelIndex / width) / 2) *
                  motionGuides[targetGuideIndex].width +
              (pixelIndex % width) / 2;
        const double targetGuide = motionGuides.empty()
            ? 0.0
            : motionGuides[targetGuideIndex].luminance[guidePixel];
        const double motionScale = std::clamp(
            std::max(options.minimumDeviation * 12.0,
                     192.0 + 4.0 * std::sqrt(targetGuide)),
            192.0, 1536.0);
        for (size_t frameIndex = first; frameIndex < last; ++frameIndex) {
            const uint16_t* pixel = frames[frameIndex].rgb + offset;
            // 几何重采样通常用 (0,0,0) 标记画布外区域。只忽略三通道全零；
            // (0,G,B) 等颜色仍是合法样本，不能按单通道分别丢弃。
            if (frameIndex != targetFrameIndex &&
                pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) {
                continue;
            }
            double weight = frameWeights[frameIndex - first];
            if (!motionGuides.empty() && frameIndex != targetFrameIndex) {
                const double difference = std::abs(
                    motionGuides[frameIndex - first].luminance[guidePixel] -
                    targetGuide);
                const double normalized = difference / motionScale;
                const double similarity = std::exp(
                    -0.5 * normalized * normalized);
                weight *= 1.0 - protectionAmount * (1.0 - similarity);
                motionProtected = motionProtected || similarity < 0.5;
            }
            samples.push_back({
                {pixel[0], pixel[1], pixel[2]},
                weight,
                frameIndex == targetFrameIndex
            });
        }
        if (motionProtected) ++result.motionProtectedPixels;

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
