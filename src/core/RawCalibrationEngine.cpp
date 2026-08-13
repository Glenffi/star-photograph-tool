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

bool validSensorGeometry(int width, int height, int rawWidth, int rawHeight,
                         int topMargin, int leftMargin) {
    return width > 0 && height > 0 && rawWidth > 0 && rawHeight > 0 &&
        topMargin >= 0 && leftMargin >= 0 && width <= rawWidth &&
        height <= rawHeight && leftMargin <= rawWidth - width &&
        topMargin <= rawHeight - height;
}

bool exposureMatches(double first, double second) {
    if (!std::isfinite(first) || !std::isfinite(second) || first <= 0.0 ||
        second <= 0.0) {
        return false;
    }
    const double tolerance = std::max(
        0.01, std::max(first, second) * 0.01);
    return std::abs(first - second) <= tolerance;
}

bool finitePixels(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(), [](float value) {
        return std::isfinite(value);
    });
}

bool validBayerPattern(const std::array<uint8_t, 4>& pattern) {
    std::array<int, 4> counts = {};
    for (uint8_t color : pattern) {
        if (color > 3) return false;
        ++counts[color];
    }
    // LibRaw may expose the second green phase as either color 1 or color 3.
    return counts[0] == 1 && counts[2] == 1 &&
        counts[1] + counts[3] == 2;
}

bool matchingGeometry(const RawImageLoader::CfaImageData& reference,
                      const RawCalibrationEngine::MasterFrame& master,
                      std::string& reason) {
    if (reference.width != master.width ||
        reference.height != master.height ||
        reference.rawWidth != master.rawWidth ||
        reference.rawHeight != master.rawHeight ||
        reference.topMargin != master.topMargin ||
        reference.leftMargin != master.leftMargin) {
        reason = "sensor geometry differs";
    } else if (reference.cameraModel != master.cameraModel) {
        reason = "camera model differs";
    } else if (reference.iso != master.iso) {
        reason = "ISO/gain differs";
    } else if (reference.cfaPattern != master.cfaPattern) {
        reason = "Bayer pattern differs";
    } else if (reference.saturation == 0 || master.saturation == 0 ||
               reference.saturation != master.saturation) {
        reason = "sensor saturation differs";
    }
    return reason.empty();
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

bool RawCalibrationEngine::MasterFrame::complete() const noexcept {
    size_t pixelCount = 0;
    return checkedPixelCount(width, height, pixelCount) &&
        validSensorGeometry(width, height, rawWidth, rawHeight, topMargin,
                            leftMargin) && iso > 0 &&
        std::isfinite(exposureTime) && exposureTime > 0.0 &&
        !cameraModel.empty() && saturation > 0 && data.size() == pixelCount &&
        validBayerPattern(cfaPattern) && finitePixels(data) &&
        (role == MasterRole::Flat || !normalizedFlat);
}

bool RawCalibrationEngine::MasterFrames::complete() const noexcept {
    size_t pixelCount = 0;
    return checkedPixelCount(width, height, pixelCount) &&
        validSensorGeometry(width, height, rawWidth, rawHeight, topMargin,
                            leftMargin) &&
        iso > 0 && std::isfinite(lightExposureTime) &&
        lightExposureTime > 0.0 && !cameraModel.empty() && saturation > 0 &&
        validBayerPattern(cfaPattern) &&
        dark.size() == pixelCount &&
        flat.size() == pixelCount &&
        (darkIncludesBiasPedestal ?
             (bias.empty() || bias.size() == pixelCount) :
             bias.size() == pixelCount) &&
        finitePixels(dark) && finitePixels(flat) && finitePixels(bias);
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
    if (frame.size() != m_sum.size() || frame.empty() ||
        !finitePixels(frame)) {
        return false;
    }
    // Validate the complete input before touching accumulator state. A bad
    // imported master must never leave a half-added frame behind.
    for (size_t i = 0; i < frame.size(); ++i) {
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

bool RawCalibrationEngine::createMasterFrame(
    MasterRole role,
    const RawImageLoader::CfaImageData& source,
    const std::vector<float>& data,
    bool normalizedFlat,
    MasterFrame& master) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(source.width, source.height, pixelCount) ||
        source.data.size() != pixelCount || data.size() != pixelCount ||
        !validSensorGeometry(source.width, source.height, source.rawWidth,
                             source.rawHeight, source.topMargin,
                             source.leftMargin) || source.iso <= 0 ||
        !std::isfinite(source.exposureTime) || source.exposureTime <= 0.0 ||
        source.cameraModel.empty() || source.saturation == 0 ||
        !finitePixels(data) ||
        (role != MasterRole::Flat && normalizedFlat)) {
        return false;
    }

    MasterFrame candidate;
    candidate.role = role;
    candidate.width = source.width;
    candidate.height = source.height;
    candidate.rawWidth = source.rawWidth;
    candidate.rawHeight = source.rawHeight;
    candidate.topMargin = source.topMargin;
    candidate.leftMargin = source.leftMargin;
    candidate.iso = source.iso;
    candidate.exposureTime = source.exposureTime;
    candidate.cameraModel = source.cameraModel;
    candidate.cfaPattern = source.cfaPattern;
    candidate.saturation = source.saturation;
    candidate.normalizedFlat = normalizedFlat;
    candidate.data = data;
    if (!candidate.complete()) return false;
    master = std::move(candidate);
    return true;
}

bool RawCalibrationEngine::validateMasterFrame(
    const RawImageLoader::CfaImageData& reference,
    const MasterFrame& master,
    std::string& reason) {
    reason.clear();
    size_t pixelCount = 0;
    if (!checkedPixelCount(reference.width, reference.height, pixelCount) ||
        reference.data.size() != pixelCount) {
        reason = "reference CFA buffer is invalid";
    } else if (!master.complete()) {
        reason = "master frame is incomplete";
    } else if (!matchingGeometry(reference, master, reason)) {
        // matchingGeometry supplies the actionable reason.
    } else if (master.role == MasterRole::Flat &&
               !master.normalizedFlat) {
        reason = "Master Flat is not calibrated and normalized";
    }
    return reason.empty();
}

bool RawCalibrationEngine::installMasterFrame(
    const RawImageLoader::CfaImageData& lightReference,
    const MasterFrame& master,
    MasterFrames& masters,
    std::string& reason) {
    reason.clear();
    if (!validateMasterFrame(lightReference, master, reason)) return false;
    if (master.role == MasterRole::DarkFlat) {
        reason = "Master Dark Flat calibrates Flat frames and cannot be "
                 "installed as a Light master";
        return false;
    }
    if (master.role == MasterRole::Dark &&
        !exposureMatches(master.exposureTime,
                         lightReference.exposureTime)) {
        reason = "Master Dark exposure differs from Light";
        return false;
    }
    if (master.role == MasterRole::Bias) {
        const double maximumBiasExposure =
            std::min(0.1, lightReference.exposureTime * 0.01);
        if (master.exposureTime > maximumBiasExposure) {
            reason = "Master Bias exposure is too long";
            return false;
        }
    }

    MasterFrames candidate = masters;
    candidate.width = lightReference.width;
    candidate.height = lightReference.height;
    candidate.rawWidth = lightReference.rawWidth;
    candidate.rawHeight = lightReference.rawHeight;
    candidate.topMargin = lightReference.topMargin;
    candidate.leftMargin = lightReference.leftMargin;
    candidate.iso = lightReference.iso;
    candidate.lightExposureTime = lightReference.exposureTime;
    candidate.cameraModel = lightReference.cameraModel;
    candidate.cfaPattern = lightReference.cfaPattern;
    candidate.saturation = lightReference.saturation;
    switch (master.role) {
    case MasterRole::Bias:
        candidate.bias = master.data;
        break;
    case MasterRole::Dark:
        candidate.dark = master.data;
        candidate.darkIncludesBiasPedestal =
            master.darkIncludesBiasPedestal;
        break;
    case MasterRole::Flat:
        candidate.flat = master.data;
        break;
    case MasterRole::DarkFlat:
        break;
    }
    masters = std::move(candidate);
    return true;
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
    std::vector<float> candidate(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        if (!std::isfinite(masterBias[i])) return false;
        candidate[i] = std::max(
            0.0f, static_cast<float>(flat.data[i]) - masterBias[i]);
    }

    std::array<double, 4> medians = {};
    if (!phaseMedians(candidate, flat.width, flat.height, medians)) return false;
    // Reject underexposed or nearly clipped flats. Both states make division
    // unstable even if a numeric median can still be calculated.
    for (double value : medians) {
        if (value < 64.0 ||
            value > static_cast<double>(flat.saturation) * 0.95) {
            return false;
        }
    }
    for (int y = 0; y < flat.height; ++y) {
        for (int x = 0; x < flat.width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * flat.width + x;
            const size_t phase = static_cast<size_t>(
                (y & 1) * 2 + (x & 1));
            candidate[pixel] = static_cast<float>(
                candidate[pixel] / medians[phase]);
        }
    }
    normalized = std::move(candidate);
    if (outputPhaseMedians) *outputPhaseMedians = medians;
    return true;
}

bool RawCalibrationEngine::normalizeFlat(
    const RawImageLoader::CfaImageData& flat,
    const MasterFrame& offsetMaster,
    std::vector<float>& normalized,
    std::array<double, 4>* outputPhaseMedians,
    std::string* outputReason) {
    std::string reason;
    if (offsetMaster.role != MasterRole::Bias &&
        offsetMaster.role != MasterRole::DarkFlat) {
        reason = "Flat offset must be Master Bias or Master Dark Flat";
    } else if (!validateMasterFrame(flat, offsetMaster, reason)) {
        // validateMasterFrame supplies the reason.
    } else if (offsetMaster.role == MasterRole::DarkFlat &&
               !exposureMatches(flat.exposureTime,
                                offsetMaster.exposureTime)) {
        reason = "Master Dark Flat exposure differs from Flat";
    } else if (offsetMaster.role == MasterRole::Bias) {
        const double maximumBiasExposure =
            std::min(0.1, flat.exposureTime * 0.01);
        if (offsetMaster.exposureTime > maximumBiasExposure) {
            reason = "Master Bias exposure is too long for Flat";
        }
    }
    if (!reason.empty()) {
        if (outputReason) *outputReason = reason;
        return false;
    }

    std::vector<float> candidate;
    std::array<double, 4> medians = {};
    if (!normalizeFlat(flat, offsetMaster.data, candidate, &medians)) {
        if (outputReason) *outputReason = "Flat normalization failed";
        return false;
    }
    normalized = std::move(candidate);
    if (outputPhaseMedians) *outputPhaseMedians = medians;
    if (outputReason) outputReason->clear();
    return true;
}

bool RawCalibrationEngine::finalizeMasterFlat(
    std::vector<float>& masterFlat, int width, int height) {
    if (!finitePixels(masterFlat)) return false;
    std::array<double, 4> medians = {};
    if (!phaseMedians(masterFlat, width, height, medians)) return false;
    std::vector<float> candidate = masterFlat;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            const size_t phase = static_cast<size_t>(
                (y & 1) * 2 + (x & 1));
            candidate[pixel] = static_cast<float>(
                candidate[pixel] / medians[phase]);
        }
    }
    masterFlat = std::move(candidate);
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
        // Camera-generated Dark retains the same Bias pedestal as Light and is
        // therefore sufficient by itself. An explicitly bias-corrected
        // imported Dark contains only dark current, so Master Bias is added to
        // the offset exactly once.
        const double offset = masters.dark[i] +
            (masters.darkIncludesBiasPedestal ? 0.0 : masters.bias[i]);
        const double signal = static_cast<double>(light.data[i]) -
            offset;
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
