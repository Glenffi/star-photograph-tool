#include "RawCalibrationEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr size_t kMaximumMedianSamplesPerPhase = 65536;
constexpr double kMinimumFlatResponse = 0.05;
constexpr double kMaximumFlatResponse = 20.0;

bool checkedPixelCount(int width, int height, size_t& pixelCount) {
    if (width <= 0 || height <= 0 ||
        static_cast<size_t>(width) >
            std::numeric_limits<size_t>::max() /
                static_cast<size_t>(height)) {
        return false;
    }
    pixelCount = static_cast<size_t>(width) * height;
    return true;
}

double median(std::vector<float>& values) {
    if (values.empty()) return 0.0;
    const size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values[middle];
    if (values.size() % 2 != 0) return upper;
    const double lower = *std::max_element(values.begin(),
                                           values.begin() + middle);
    return (lower + upper) * 0.5;
}

bool phaseMedians(const std::vector<float>& values, int width, int height,
                  std::array<double, 4>& medians) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        values.size() != pixelCount) {
        return false;
    }

    std::array<size_t, 4> totals = {};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            ++totals[static_cast<size_t>((y & 1) * 2 + (x & 1))];
        }
    }
    std::array<size_t, 4> strides = {};
    std::array<size_t, 4> seen = {};
    std::array<std::vector<float>, 4> samples;
    for (size_t phase = 0; phase < 4; ++phase) {
        strides[phase] = std::max<size_t>(
            1, (totals[phase] + kMaximumMedianSamplesPerPhase - 1) /
                   kMaximumMedianSamplesPerPhase);
        samples[phase].reserve(std::min(
            totals[phase], kMaximumMedianSamplesPerPhase));
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t phase = static_cast<size_t>(
                (y & 1) * 2 + (x & 1));
            const float value = values[static_cast<size_t>(y) * width + x];
            if (std::isfinite(value) && value > 0.0f &&
                seen[phase]++ % strides[phase] == 0 &&
                samples[phase].size() < kMaximumMedianSamplesPerPhase) {
                samples[phase].push_back(value);
            }
        }
    }
    for (size_t phase = 0; phase < 4; ++phase) {
        medians[phase] = median(samples[phase]);
        if (!std::isfinite(medians[phase]) || medians[phase] <= 0.0) {
            return false;
        }
    }
    return true;
}

} // namespace

bool RawCalibrationEngine::MasterFrames::complete() const noexcept {
    size_t pixelCount = 0;
    return checkedPixelCount(width, height, pixelCount) &&
        rawWidth > 0 && rawHeight > 0 && topMargin >= 0 && leftMargin >= 0 &&
        saturation > 0 && bias.size() == pixelCount &&
        dark.size() == pixelCount && flat.size() == pixelCount;
}

RawCalibrationEngine::MeanAccumulator::MeanAccumulator(size_t valueCount)
    : m_sum(valueCount, 0.0)
    , m_minimum(valueCount, std::numeric_limits<float>::infinity())
    , m_maximum(valueCount, -std::numeric_limits<float>::infinity()) {}

bool RawCalibrationEngine::MeanAccumulator::add(
    const std::vector<uint16_t>& frame) {
    if (frame.size() != m_sum.size() || frame.empty()) return false;
    for (size_t i = 0; i < frame.size(); ++i) {
        const float value = frame[i];
        m_sum[i] += value;
        m_minimum[i] = std::min(m_minimum[i], value);
        m_maximum[i] = std::max(m_maximum[i], value);
    }
    ++m_frameCount;
    return true;
}

bool RawCalibrationEngine::MeanAccumulator::add(
    const std::vector<float>& frame) {
    if (frame.size() != m_sum.size() || frame.empty()) return false;
    for (size_t i = 0; i < frame.size(); ++i) {
        if (!std::isfinite(frame[i])) return false;
        m_sum[i] += frame[i];
        m_minimum[i] = std::min(m_minimum[i], frame[i]);
        m_maximum[i] = std::max(m_maximum[i], frame[i]);
    }
    ++m_frameCount;
    return true;
}

bool RawCalibrationEngine::MeanAccumulator::finish(
    std::vector<float>& mean) const {
    if (m_sum.empty() || m_frameCount == 0) return false;
    mean.resize(m_sum.size());
    // With five or more captures, reject one low and one high realization at
    // each pixel. Persistent dark-current structure remains in every frame,
    // while one-off cosmic rays or read glitches do not enter the master.
    const bool rejectExtremes = m_frameCount >= 5;
    const double divisor = static_cast<double>(
        rejectExtremes ? m_frameCount - 2 : m_frameCount);
    for (size_t i = 0; i < m_sum.size(); ++i) {
        const double sum = rejectExtremes
            ? m_sum[i] - m_minimum[i] - m_maximum[i]
            : m_sum[i];
        mean[i] = static_cast<float>(sum / divisor);
    }
    return true;
}

bool RawCalibrationEngine::compatible(
    const RawImageLoader::CfaImageData& reference,
    const RawImageLoader::CfaImageData& candidate,
    std::string& reason) {
    reason.clear();
    if (reference.width != candidate.width ||
        reference.height != candidate.height ||
        reference.rawWidth != candidate.rawWidth ||
        reference.rawHeight != candidate.rawHeight ||
        reference.topMargin != candidate.topMargin ||
        reference.leftMargin != candidate.leftMargin) {
        reason = "sensor geometry differs";
    } else if (reference.cameraModel != candidate.cameraModel) {
        reason = "camera model differs";
    } else if (reference.iso != candidate.iso) {
        reason = "ISO/gain differs";
    } else if (reference.cfaPattern != candidate.cfaPattern) {
        reason = "Bayer pattern differs";
    } else if (reference.data.size() != candidate.data.size()) {
        reason = "CFA buffer size differs";
    }
    return reason.empty();
}

bool RawCalibrationEngine::normalizeFlat(
    const RawImageLoader::CfaImageData& flat,
    const std::vector<float>& masterBias,
    std::vector<float>& normalized,
    std::array<double, 4>* outputPhaseMedians) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(flat.width, flat.height, pixelCount) ||
        flat.data.size() != pixelCount || masterBias.size() != pixelCount) {
        return false;
    }
    normalized.resize(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        normalized[i] = std::max(
            0.0f, static_cast<float>(flat.data[i]) - masterBias[i]);
    }

    std::array<double, 4> medians = {};
    if (!phaseMedians(normalized, flat.width, flat.height, medians)) {
        normalized.clear();
        return false;
    }
    // Reject underexposed or nearly clipped flats. Both states make division
    // unstable even if a numeric median can still be calculated.
    for (double value : medians) {
        if (value < 64.0 ||
            value > static_cast<double>(flat.saturation) * 0.95) {
            normalized.clear();
            return false;
        }
    }
    for (int y = 0; y < flat.height; ++y) {
        for (int x = 0; x < flat.width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * flat.width + x;
            const size_t phase = static_cast<size_t>(
                (y & 1) * 2 + (x & 1));
            normalized[pixel] = static_cast<float>(
                normalized[pixel] / medians[phase]);
        }
    }
    if (outputPhaseMedians) *outputPhaseMedians = medians;
    return true;
}

bool RawCalibrationEngine::finalizeMasterFlat(
    std::vector<float>& masterFlat, int width, int height) {
    std::array<double, 4> medians = {};
    if (!phaseMedians(masterFlat, width, height, medians)) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            const size_t phase = static_cast<size_t>(
                (y & 1) * 2 + (x & 1));
            masterFlat[pixel] = static_cast<float>(
                masterFlat[pixel] / medians[phase]);
        }
    }
    return true;
}

bool RawCalibrationEngine::calibrateLight(
    const RawImageLoader::CfaImageData& light,
    const MasterFrames& masters,
    RawImageLoader::CfaImageData& calibrated,
    CalibrationStats* outputStats) {
    CalibrationStats stats;
    size_t pixelCount = 0;
    if (!masters.complete() ||
        !checkedPixelCount(light.width, light.height, pixelCount) ||
        light.data.size() != pixelCount ||
        light.width != masters.width || light.height != masters.height ||
        light.rawWidth != masters.rawWidth ||
        light.rawHeight != masters.rawHeight ||
        light.topMargin != masters.topMargin ||
        light.leftMargin != masters.leftMargin ||
        light.iso != masters.iso ||
        light.cameraModel != masters.cameraModel ||
        light.cfaPattern != masters.cfaPattern) {
        return false;
    }

    calibrated = light;
    calibrated.data.resize(light.data.size());
    calibrated.blackLevel = 0;
    calibrated.saturation = masters.saturation;
    for (size_t i = 0; i < light.data.size(); ++i) {
        const double flat = masters.flat[i];
        if (!std::isfinite(flat) || flat < kMinimumFlatResponse ||
            flat > kMaximumFlatResponse) {
            calibrated.data[i] = 0;
            ++stats.invalidFlatPixels;
            continue;
        }
        // Master Dark contains the same Bias pedestal as the Light. Subtracting
        // Master Bias here as well would clip the shadow signal twice.
        const double signal = static_cast<double>(light.data[i]) -
            masters.dark[i];
        const double corrected = signal / flat;
        if (corrected <= 0.0) {
            calibrated.data[i] = 0;
            ++stats.clippedLowPixels;
        } else if (corrected >= masters.saturation) {
            calibrated.data[i] = masters.saturation;
            ++stats.clippedHighPixels;
        } else {
            calibrated.data[i] = static_cast<uint16_t>(
                std::lround(corrected));
        }
    }
    if (outputStats) *outputStats = stats;
    return true;
}
