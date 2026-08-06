#include "TemporalPhotometricSmoother.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

double median(std::vector<double>& values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t middle = values.size() / 2;
    if (values.size() % 2 != 0) return values[middle];
    return (values[middle - 1] + values[middle]) * 0.5;
}

bool validModel(const PhotometricModel& model) {
    if (!std::isfinite(model.gain) || model.gain < 0.5 || model.gain > 2.0) {
        return false;
    }
    return std::all_of(model.offsets.begin(), model.offsets.end(),
                       [](double offset) {
                           return std::isfinite(offset) &&
                                  std::abs(offset) <= 16384.0;
                       });
}

} // namespace

bool TemporalPhotometricSmoother::correctionForFrame(
    const std::vector<Sample>& samples,
    size_t frameIndex,
    const Options& options,
    PhotometricModel& correction) {
    correction = {};
    if (samples.empty() || frameIndex >= samples.size() ||
        options.windowSize < 3 || options.windowSize % 2 == 0 ||
        !std::isfinite(options.strength) || options.strength < 0.0 ||
        options.strength > 100.0 ||
        !std::isfinite(options.maximumGainChange) ||
        options.maximumGainChange < 0.0 ||
        options.maximumGainChange >= 0.5 ||
        !std::isfinite(options.maximumOffsetChange) ||
        options.maximumOffsetChange < 0.0 ||
        !samples[frameIndex].valid ||
        !validModel(samples[frameIndex].model)) {
        return false;
    }

    const size_t radius = options.windowSize / 2;
    std::vector<double> logGains;
    std::array<std::vector<double>, 3> offsets;
    logGains.reserve(options.windowSize);
    for (auto& channel : offsets) channel.reserve(options.windowSize);

    // Clamped indices duplicate the edge frame. A genuinely monotonic dawn or
    // moonrise therefore remains unchanged at both ends of the sequence.
    for (size_t offset = 0; offset < options.windowSize; ++offset) {
        const long long relative = static_cast<long long>(offset) -
            static_cast<long long>(radius);
        const long long unclamped =
            static_cast<long long>(frameIndex) + relative;
        const size_t index = static_cast<size_t>(std::clamp<long long>(
            unclamped, 0, static_cast<long long>(samples.size() - 1)));
        if (!samples[index].valid || !validModel(samples[index].model)) {
            continue;
        }
        logGains.push_back(std::log(samples[index].model.gain));
        for (int channel = 0; channel < 3; ++channel) {
            offsets[channel].push_back(
                samples[index].model.offsets[channel]);
        }
    }
    if (logGains.size() < 3) return false;

    const double smoothGain = std::exp(median(logGains));
    std::array<double, 3> smoothOffsets = {};
    for (int channel = 0; channel < 3; ++channel) {
        smoothOffsets[channel] = median(offsets[channel]);
    }
    if (!std::isfinite(smoothGain) || smoothGain <= 0.0) return false;

    const PhotometricModel& current = samples[frameIndex].model;
    const double amount = options.strength / 100.0;
    const double fullGain = current.gain / smoothGain;
    if (!std::isfinite(fullGain) || fullGain <= 0.0) return false;
    const double minimumGain = 1.0 - options.maximumGainChange;
    const double maximumGain = 1.0 + options.maximumGainChange;
    correction.gain = std::clamp(
        std::exp(std::log(fullGain) * amount), minimumGain, maximumGain);
    for (int channel = 0; channel < 3; ++channel) {
        const double fullOffset =
            (current.offsets[channel] - smoothOffsets[channel]) / smoothGain;
        correction.offsets[channel] = std::clamp(
            fullOffset * amount,
            -options.maximumOffsetChange,
            options.maximumOffsetChange);
    }
    correction.sampleCount = logGains.size();
    correction.inlierCount = logGains.size();
    return validModel(correction);
}
