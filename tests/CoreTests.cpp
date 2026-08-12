#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/MinimumFilter.h"
#include "core/AutoOptimizeEngine.h"
#include "core/DeepSkyCalibrationPreflight.h"
#include "core/FrameQualityEvaluator.h"
#include "core/NoiseReductionEngine.h"
#include "core/PhotometricNormalizer.h"
#include "core/PresetManager.h"
#include "core/RawCalibrationEngine.h"
#include "core/RawImageLoader.h"
#include "core/ProcessingMemoryEstimator.h"
#include "core/PreviewToneMapper.h"
#include "core/SkyGroundMask.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarReducer.h"
#include "core/TemporalPhotometricSmoother.h"
#include "core/TimelapseEngine.h"
#include "core/UpdateManifest.h"

#include <QByteArray>
#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <tiffio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
}

std::vector<float> bruteForceMinimumFilter(
    const std::vector<float>& source, int width, int height, int radius) {
    std::vector<float> output(source.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float minimum = source[static_cast<size_t>(y) * width + x];
            for (int yy = std::max(0, y - radius);
                 yy <= std::min(height - 1, y + radius); ++yy) {
                for (int xx = std::max(0, x - radius);
                     xx <= std::min(width - 1, x + radius); ++xx) {
                    minimum = std::min(
                        minimum,
                        source[static_cast<size_t>(yy) * width + xx]);
                }
            }
            output[static_cast<size_t>(y) * width + x] = minimum;
        }
    }
    return output;
}

void testMinimumFilter() {
    std::mt19937 random(0x53544152U);
    std::uniform_real_distribution<float> value(-1.0f, 2.0f);
    for (int width : {1, 2, 7, 16}) {
        for (int height : {1, 3, 9}) {
            std::vector<float> source(
                static_cast<size_t>(width) * height);
            for (float& sample : source) sample = value(random);
            for (int radius : {0, 1, 3, 8}) {
                std::vector<float> actual;
                check(MinimumFilter::applySquare(
                          source, width, height, radius, actual),
                      "Minimum filter should accept valid dimensions");
                check(actual == bruteForceMinimumFilter(
                                    source, width, height, radius),
                      "Separable minimum filter must exactly match the "
                      "clipped-edge brute-force oracle");
            }
        }
    }

    std::vector<float> hugeRadius;
    check(MinimumFilter::applySquare(
              {4.0f, 2.0f}, 2, 1, std::numeric_limits<int>::max(),
              hugeRadius) && hugeRadius == std::vector<float>({2.0f, 2.0f}),
          "Minimum filter should safely clamp an extreme radius to the image");

    std::vector<float> unchanged = {42.0f};
    check(!MinimumFilter::applySquare({1.0f}, 2, 1, 1, unchanged) &&
              unchanged == std::vector<float>({42.0f}),
          "Rejected minimum-filter input should leave output unchanged");
}

void addGaussianStar(std::vector<uint16_t>& image, int width, int height,
                     double cx, double cy, double sigma, double peak) {
    const int radius = static_cast<int>(std::ceil(sigma * 4.0));
    for (int y = std::max(0, static_cast<int>(cy) - radius);
         y <= std::min(height - 1, static_cast<int>(cy) + radius); ++y) {
        for (int x = std::max(0, static_cast<int>(cx) - radius);
             x <= std::min(width - 1, static_cast<int>(cx) + radius); ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            const double value = peak * std::exp(-(dx * dx + dy * dy) /
                                                 (2.0 * sigma * sigma));
            const size_t index = static_cast<size_t>(y) * width + x;
            image[index] = static_cast<uint16_t>(std::min(65535.0, image[index] + value));
        }
    }
}

uint16_t legacyKappaSigma(std::vector<uint16_t> values, double kappa,
                          bool ignoreZero) {
    if (ignoreZero) {
        values.erase(std::remove(values.begin(), values.end(), 0), values.end());
    }
    if (values.empty()) return 0;

    std::vector<uint16_t> active = values;
    std::vector<uint16_t> ordered;
    std::vector<uint16_t> filtered;
    ordered.reserve(values.size());
    filtered.reserve(values.size());
    for (int iteration = 0; iteration < 3; ++iteration) {
        ordered.assign(active.begin(), active.end());
        const size_t middle = ordered.size() / 2;
        std::nth_element(ordered.begin(), ordered.begin() + middle, ordered.end());
        double median = ordered[middle];
        if (ordered.size() % 2 == 0) {
            median = (median + *std::max_element(
                                   ordered.begin(), ordered.begin() + middle)) / 2.0;
        }
        double squaredSum = 0.0;
        for (uint16_t value : active) {
            const double delta = static_cast<double>(value) - median;
            squaredSum += delta * delta;
        }
        const double deviation = std::sqrt(squaredSum / active.size());
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
    return static_cast<uint16_t>(sum / active.size());
}

void testStacking() {
    StackingEngine engine;
    const std::vector<std::vector<uint16_t>> frames = {
        {100, 200, 300, 400},
        {110, 210, 310, 410},
        {120, 220, 320, 420}
    };
    std::vector<uint16_t> result;
    check(engine.stack(frames, 2, 2, StackingEngine::Average, 2.5, result),
          "Average stacking should succeed");
    check(result == std::vector<uint16_t>({110, 210, 310, 410}),
          "Average stacking should compute pixel means");

    check(engine.stack(frames, 2, 2, StackingEngine::Median, 2.5, result),
          "Median stacking should succeed");
    check(result == std::vector<uint16_t>({110, 210, 310, 410}),
          "Median stacking should compute pixel medians");

    const std::vector<std::vector<uint16_t>> blackFrames = {{0}, {100}};
    check(engine.stack(blackFrames, 1, 1, StackingEngine::Average, 2.5, result) &&
              result == std::vector<uint16_t>({50}),
          "A real zero-valued pixel should participate in ordinary averaging");
    check(engine.stack(blackFrames, 1, 1, StackingEngine::Average, 2.5, result, true) &&
              result == std::vector<uint16_t>({100}),
          "Aligned stacking may explicitly ignore zero padding");

    const std::vector<std::vector<uint16_t>> evenFrames = {{100}, {200}};
    check(engine.stack(evenFrames, 1, 1, StackingEngine::Median, 2.5, result) &&
              result == std::vector<uint16_t>({150}),
          "Even-sized median should average the two central values");

    const std::vector<uint8_t> mask = {255, 255, 0, 0};
    const std::vector<std::vector<uint16_t>> originals = {
        {900, 900, 1000, 1100}, {900, 900, 1200, 1300}, {900, 900, 1400, 1500}
    };
    check(engine.stackWithMask(frames, originals, 2, 2, StackingEngine::Average,
                               2.5, mask, result),
          "Mask stacking should succeed with matching frame counts");
    check(result == std::vector<uint16_t>({110, 210, 1200, 1300}),
          "Mask stacking should select aligned sky and original ground");
    check(engine.stackWithMask(frames, {originals.front()}, 2, 2,
                               StackingEngine::Average, 2.5, mask, result,
                               StackingEngine::GroundReferenceFrame) &&
              result == std::vector<uint16_t>({110, 210, 1000, 1100}),
          "Reference ground mode should preserve the first ground frame");
    check(!engine.stackWithMask(frames, {originals.front()}, 2, 2,
                                StackingEngine::Average, 2.5, mask, result),
          "Mask stacking should reject mismatched frame counts");
    const std::vector<std::vector<uint16_t>> movingGround = {
        {900, 900, 1000, 1000},
        {900, 900, 1100, 60000},
        {900, 900, 1200, 1200}
    };
    check(engine.stackWithMask(frames, movingGround, 2, 2,
                               StackingEngine::Average, 2.5, mask, result,
                               StackingEngine::GroundMedian) &&
              result == std::vector<uint16_t>({110, 210, 1100, 1200}),
          "Median ground mode should reject transient foreground outliers");

    const std::vector<std::vector<uint16_t>> filteredSky = {{1000}, {0}};
    const std::vector<std::vector<uint16_t>> fixedGround = {{5000}, {5000}};
    check(engine.stackWithMask(
              filteredSky, fixedGround, 1, 1,
              StackingEngine::Average, 2.5, {255}, result) &&
              result == std::vector<uint16_t>({1000}),
          "Sky stacking should ignore a shifted-ground sample marked as zero");

    const std::vector<std::vector<uint16_t>> rgbFrames = {
        {100, 200, 300, 0, 500, 600},
        {110, 210, 310, 400, 510, 610},
        {120, 220, 320, 420, 520, 620}
    };
    check(engine.stackRgb(rgbFrames, 2, 1, StackingEngine::Average, 2.5,
                          result, true),
          "Interleaved RGB stacking should succeed");
    check(result == std::vector<uint16_t>({110, 210, 310, 410, 510, 610}),
          "Interleaved RGB stacking should preserve channels and ignore padding");

    const std::vector<std::vector<uint16_t>> outlierFrames = {
        {100}, {102}, {99}, {101}, {50000}
    };
    check(engine.stack(outlierFrames, 1, 1, StackingEngine::KappaSigma, 1.5,
                       result) && result == std::vector<uint16_t>({100}),
          "Kappa-Sigma should iteratively reject an outlier and average survivors");
    check(engine.stack(outlierFrames, 1, 1, StackingEngine::Winsorized, 1.5,
                       result) && result[0] < 200,
          "Winsorized stacking should clamp a strong positive outlier");
    const std::vector<std::vector<uint16_t>> paddedOutlierFrames = {
        {0}, {100}, {102}, {50000}
    };
    check(engine.stack(paddedOutlierFrames, 1, 1, StackingEngine::KappaSigma,
                       1.0, result, true) &&
              result == std::vector<uint16_t>({101}),
          "Kappa-Sigma should exclude alignment padding before clipping");
    check(!engine.stack(outlierFrames, 1, 1, StackingEngine::KappaSigma,
                        0.0, result),
          "Kappa-Sigma should reject a non-positive kappa");

    // Differential coverage protects the optimized sorted-range kernel against
    // subtle clipping changes across frame counts, kappa values and zero padding.
    std::mt19937 random(0x4b415050U);
    std::uniform_int_distribution<int> valueDistribution(0, 65535);
    constexpr int sampleCount = 256;
    for (int frameCount = 2; frameCount <= 32; ++frameCount) {
        std::vector<std::vector<uint16_t>> randomFrames(
            static_cast<size_t>(frameCount),
            std::vector<uint16_t>(sampleCount));
        for (auto& frame : randomFrames) {
            for (uint16_t& value : frame) {
                value = static_cast<uint16_t>(valueDistribution(random));
                if (valueDistribution(random) % 17 == 0) value = 0;
            }
        }
        for (double testKappa : {1.0, 1.5, 2.5, 4.0}) {
            for (bool ignorePadding : {false, true}) {
                check(engine.stack(randomFrames, sampleCount, 1,
                                   StackingEngine::KappaSigma, testKappa,
                                   result, ignorePadding),
                      "Random Kappa-Sigma differential input should stack");
                for (int sample = 0; sample < sampleCount; ++sample) {
                    std::vector<uint16_t> values;
                    values.reserve(static_cast<size_t>(frameCount));
                    for (const auto& frame : randomFrames) {
                        values.push_back(frame[static_cast<size_t>(sample)]);
                    }
                    check(result[static_cast<size_t>(sample)] ==
                              legacyKappaSigma(
                                  std::move(values), testKappa, ignorePadding),
                          "Optimized Kappa-Sigma should match the legacy kernel");
                }
            }
        }
    }
}

void testRawCalibration() {
    RawCalibrationEngine::MeanAccumulator robustMaster(1);
    for (uint16_t value : {uint16_t{100}, uint16_t{100}, uint16_t{100},
                           uint16_t{100}, uint16_t{4000}}) {
        check(robustMaster.add(std::vector<uint16_t>{value}),
              "Calibration master accumulator should accept matching frames");
    }
    std::vector<float> masterValue;
    check(robustMaster.finish(masterValue) && masterValue.size() == 1 &&
              std::abs(masterValue[0] - 100.0f) < 1e-6f,
          "Five-frame calibration masters should reject one extreme sample");

    constexpr int width = 4;
    constexpr int height = 4;
    constexpr size_t pixelCount = static_cast<size_t>(width) * height;
    RawImageLoader::CfaImageData geometry;
    geometry.width = width;
    geometry.height = height;
    geometry.rawWidth = width;
    geometry.rawHeight = height;
    geometry.iso = 800;
    geometry.exposureTime = 60.0;
    geometry.cameraModel = "Synthetic Bayer";
    geometry.cfaPattern = {0, 1, 1, 2};
    geometry.blackLevel = 100;
    geometry.saturation = 4000;
    geometry.data.resize(pixelCount);

    std::vector<float> bias(pixelCount, 100.0f);
    std::vector<float> dark(pixelCount, 110.0f); // Includes Bias + dark current.
    const std::array<double, 4> phaseLevels = {
        1000.0, 1200.0, 1100.0, 900.0};
    std::vector<double> response(pixelCount, 1.0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t pixel = static_cast<size_t>(y) * width + x;
            const size_t phase = static_cast<size_t>(
                (y & 1) * 2 + (x & 1));
            response[pixel] = x < 2 ? 0.5 : 1.5;
            geometry.data[pixel] = static_cast<uint16_t>(std::lround(
                100.0 + phaseLevels[phase] * response[pixel]));
        }
    }
    std::vector<float> masterFlat;
    check(RawCalibrationEngine::normalizeFlat(
              geometry, bias, masterFlat) &&
              RawCalibrationEngine::finalizeMasterFlat(
                  masterFlat, width, height),
          "Bias-corrected Bayer flats should normalize by CFA phase");

    RawCalibrationEngine::MasterFrames masters;
    masters.width = width;
    masters.height = height;
    masters.rawWidth = width;
    masters.rawHeight = height;
    masters.iso = geometry.iso;
    masters.lightExposureTime = geometry.exposureTime;
    masters.cameraModel = geometry.cameraModel;
    masters.cfaPattern = geometry.cfaPattern;
    masters.saturation = geometry.saturation;
    masters.bias = bias;
    masters.dark = dark;
    masters.flat = masterFlat;

    RawImageLoader::CfaImageData light = geometry;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        light.data[pixel] = static_cast<uint16_t>(std::lround(
            110.0 + 2000.0 * response[pixel]));
    }
    RawImageLoader::CfaImageData calibrated;
    RawCalibrationEngine::CalibrationStats stats;
    check(RawCalibrationEngine::calibrateLight(
              light, masters, calibrated, &stats) &&
              std::all_of(calibrated.data.begin(), calibrated.data.end(),
                          [](uint16_t value) {
                              return std::abs(static_cast<int>(value) - 2000) <= 1;
                          }) &&
              stats.invalidFlatPixels == 0,
          "Light calibration should subtract Dark once and remove flat response");

    RawImageLoader::CfaImageData incompatible = light;
    incompatible.iso = 1600;
    std::string reason;
    check(!RawCalibrationEngine::compatible(light, incompatible, reason) &&
              reason == "ISO/gain differs",
          "Calibration frames with a different ISO/gain should be rejected");
}

void testDeepSkyCalibrationPreflight() {
    using Preflight = DeepSkyCalibrationPreflight;
    auto metadata = [](int iso, double exposure) {
        RawImageLoader::Metadata value;
        value.width = 3888;
        value.height = 2592;
        value.iso = iso;
        value.exposureTime = exposure;
        value.cameraModel = "Canon EOS 40D";
        return value;
    };
    auto frame = [&](const QString& path, Preflight::Role role,
                     int iso, double exposure) {
        Preflight::FrameRecord value;
        value.path = path;
        value.role = role;
        value.readable = true;
        value.metadata = metadata(iso, exposure);
        return value;
    };

    std::vector<Preflight::FrameRecord> valid = {
        frame("/tmp/light-1.cr2", Preflight::Role::Light, 1600, 30.0),
        frame("/tmp/light-2.cr2", Preflight::Role::Light, 1600, 30.0)
    };
    for (int i = 0; i < 3; ++i) {
        valid.push_back(frame(
            QString("/tmp/dark-%1.cr2").arg(i),
            Preflight::Role::Dark, 1600, 30.0));
        valid.push_back(frame(
            QString("/tmp/flat-%1.cr2").arg(i),
            Preflight::Role::Flat, 1600, 0.25));
        valid.push_back(frame(
            QString("/tmp/bias-%1.cr2").arg(i),
            Preflight::Role::Bias, 1600, 0.001));
    }
    const Preflight::Report validReport = Preflight::validate(valid);
    check(!validReport.hasErrors(),
          "Matched deep-sky metadata should pass calibration preflight");
    check(validReport.warningMessages().size() == 3,
          "Small calibration sets should produce one recommendation per role");

    std::vector<Preflight::FrameRecord> incompatible = valid;
    for (Preflight::FrameRecord& item : incompatible) {
        if (item.role == Preflight::Role::Flat) {
            item.metadata.iso = 100;
            item.metadata.exposureTime = 2.5;
        } else if (item.role == Preflight::Role::Bias) {
            item.metadata.exposureTime = 30.0;
        }
    }
    const Preflight::Report incompatibleReport =
        Preflight::validate(incompatible);
    const QString incompatibleMessage = incompatibleReport.userMessage();
    check(incompatibleReport.hasErrors() &&
              incompatibleMessage.contains(QString::fromUtf8("Flat ISO 不匹配")) &&
              incompatibleMessage.contains(QString::fromUtf8("Bias 曝光过长")),
          "Preflight should aggregate mismatched flats and mislabeled biases");
    check(incompatibleMessage.contains(QString::fromUtf8("3 张")),
          "Grouped preflight diagnostics should report affected frame counts");

    std::vector<Preflight::FrameRecord> duplicate = valid;
    duplicate.back().path = duplicate.front().path;
    const Preflight::Report duplicateReport = Preflight::validate(duplicate);
    check(duplicateReport.hasErrors() &&
              duplicateReport.userMessage().contains(
                  QString::fromUtf8("重复导入")),
          "A RAW assigned to more than one role should fail preflight");

    std::vector<Preflight::FrameRecord> unreadable = valid;
    unreadable.front().readable = false;
    const Preflight::Report unreadableReport = Preflight::validate(unreadable);
    check(unreadableReport.hasErrors() &&
              unreadableReport.userMessage().contains(
                  QString::fromUtf8("无法读取 RAW 头信息")),
          "Unreadable RAW headers should be listed before CFA decoding");
}

void testTimelapseDenoise() {
    const std::vector<uint16_t> previous = {900, 900, 900};
    const std::vector<uint16_t> target = {1000, 1000, 1000};
    const std::vector<uint16_t> next = {1100, 1100, 1100};
    const std::vector<TimelapseEngine::FrameView> frames = {
        {previous.data(), previous.size(), -1.0},
        {target.data(), target.size(), 0.0},
        {next.data(), next.size(), 1.0}
    };
    TimelapseEngine::Options options;
    options.windowSize = 3;
    options.temporalSigma = 1.5;
    options.strength = 100.0;
    const TimelapseEngine::Result balanced =
        TimelapseEngine::denoise(frames, 1, 1, 1, options);
    check(balanced && balanced.rgb == target,
          "Symmetric timelapse samples should preserve the target brightness");

    const std::vector<uint16_t> padded = {0, 0, 0};
    const std::vector<TimelapseEngine::FrameView> paddedFrames = {
        {padded.data(), padded.size(), -1.0},
        {target.data(), target.size(), 0.0},
        {next.data(), next.size(), 1.0}
    };
    const TimelapseEngine::Result ignoredPadding =
        TimelapseEngine::denoise(paddedFrames, 1, 1, 1, options);
    check(ignoredPadding && ignoredPadding.rgb.front() > target.front(),
          "RGB zero padding should not darken temporal output borders");

    options.strength = 0.0;
    const TimelapseEngine::Result disabled =
        TimelapseEngine::denoise(frames, 1, 1, 1, options);
    check(disabled && disabled.rgb == target,
          "Zero temporal strength should return the target frame exactly");

    options.windowSize = 4;
    check(TimelapseEngine::denoise(frames, 1, 1, 1, options).error ==
              TimelapseEngine::Error::InvalidWindowSize,
          "Even timelapse windows should be rejected");

    constexpr int motionWidth = 5;
    constexpr int motionHeight = 5;
    const size_t motionValueCount =
        static_cast<size_t>(motionWidth) * motionHeight * 3;
    std::vector<uint16_t> motionPrevious(motionValueCount, 1000);
    std::vector<uint16_t> motionTarget(motionValueCount, 1000);
    std::vector<uint16_t> motionNext(motionValueCount, 1000);
    for (int y = 1; y <= 3; ++y) {
        for (int x = 1; x <= 3; ++x) {
            const size_t base =
                (static_cast<size_t>(y) * motionWidth + x) * 3;
            motionTarget[base] = 5000;
            motionTarget[base + 1] = 3000;
            motionTarget[base + 2] = 2000;
        }
    }
    const std::vector<TimelapseEngine::FrameView> motionFrames = {
        {motionPrevious.data(), motionPrevious.size(), -1.0},
        {motionTarget.data(), motionTarget.size(), 0.0},
        {motionNext.data(), motionNext.size(), 1.0}
    };
    TimelapseEngine::Options motionOptions;
    motionOptions.windowSize = 3;
    motionOptions.temporalSigma = 1.0;
    motionOptions.strength = 100.0;
    motionOptions.motionProtection = 0.0;
    const size_t motionCenter =
        (static_cast<size_t>(2) * motionWidth + 2) * 3;
    const TimelapseEngine::Result unprotectedMotion =
        TimelapseEngine::denoise(
            motionFrames, motionWidth, motionHeight, 1, motionOptions);
    check(unprotectedMotion &&
              unprotectedMotion.rgb[motionCenter] == 1000,
          "Unprotected temporal filtering should remove a target-only patch");

    motionOptions.motionProtection = 100.0;
    const TimelapseEngine::Result protectedMotion =
        TimelapseEngine::denoise(
            motionFrames, motionWidth, motionHeight, 1, motionOptions);
    check(protectedMotion &&
              protectedMotion.rgb[motionCenter] == 5000 &&
              protectedMotion.rgb[motionCenter + 1] == 3000 &&
              protectedMotion.rgb[motionCenter + 2] == 2000 &&
              protectedMotion.motionProtectedPixels > 0,
          "Motion protection should retain a spatially supported RGB change");

    std::vector<uint16_t> hotPixelTarget(motionValueCount, 1000);
    hotPixelTarget[motionCenter] = 65535;
    hotPixelTarget[motionCenter + 1] = 65535;
    hotPixelTarget[motionCenter + 2] = 65535;
    const std::vector<TimelapseEngine::FrameView> hotPixelFrames = {
        {motionPrevious.data(), motionPrevious.size(), -1.0},
        {hotPixelTarget.data(), hotPixelTarget.size(), 0.0},
        {motionNext.data(), motionNext.size(), 1.0}
    };
    const TimelapseEngine::Result rejectedHotPixel =
        TimelapseEngine::denoise(
            hotPixelFrames, motionWidth, motionHeight, 1, motionOptions);
    check(rejectedHotPixel &&
              rejectedHotPixel.rgb[motionCenter] == 1000,
          "Median motion guidance should not preserve an isolated hot pixel");

    motionOptions.motionProtection = 101.0;
    check(TimelapseEngine::denoise(
              motionFrames, motionWidth, motionHeight, 1, motionOptions).error ==
              TimelapseEngine::Error::InvalidOptions,
          "Motion protection should reject values above 100");
}

void testTemporalPhotometricSmoothing() {
    auto sample = [](double gain, std::array<double, 3> offsets = {}) {
        TemporalPhotometricSmoother::Sample result;
        result.valid = true;
        result.model.gain = gain;
        result.model.offsets = offsets;
        return result;
    };

    const std::vector<TemporalPhotometricSmoother::Sample> monotonic = {
        sample(0.96), sample(0.98), sample(1.00),
        sample(1.02), sample(1.04)
    };
    TemporalPhotometricSmoother::Options options;
    PhotometricModel correction;
    check(TemporalPhotometricSmoother::correctionForFrame(
              monotonic, 2, options, correction) &&
              std::abs(correction.gain - 1.0) < 1e-9,
          "Temporal smoothing should preserve a monotonic exposure trend");
    check(TemporalPhotometricSmoother::correctionForFrame(
              monotonic, 0, options, correction) &&
              std::abs(correction.gain - 1.0) < 1e-9,
          "Clamped temporal smoothing should preserve sequence edges");

    std::vector<TemporalPhotometricSmoother::Sample> spike(
        7, sample(1.0));
    spike[3] = sample(0.8, {1000.0, -500.0, 200.0});
    check(TemporalPhotometricSmoother::correctionForFrame(
              spike, 3, options, correction) &&
              std::abs(correction.gain - 0.97) < 1e-9 &&
              std::abs(correction.offsets[0] - 650.0) < 1e-9 &&
              std::abs(correction.offsets[1] + 325.0) < 1e-9,
          "Temporal smoothing should limit an isolated exposure/color jump");

    options.strength = 0.0;
    check(TemporalPhotometricSmoother::correctionForFrame(
              spike, 3, options, correction) &&
              std::abs(correction.gain - 1.0) < 1e-9 &&
              std::abs(correction.offsets[0]) < 1e-9,
          "Zero flicker strength should produce an identity correction");
}

void testImageBufferUtils() {
    const std::vector<uint16_t> rgb = {
        1000, 2000, 3000,
        4000, 5000, 6000
    };
    ImageBufferUtils::RgbChannels channels;
    check(ImageBufferUtils::splitRgb(rgb, 2, 1, channels),
          "RGB channel split should accept an exact buffer");
    check(channels.red == std::vector<uint16_t>({1000, 4000}) &&
              channels.green == std::vector<uint16_t>({2000, 5000}) &&
              channels.blue == std::vector<uint16_t>({3000, 6000}),
          "RGB channel split should preserve channel order");

    std::vector<uint16_t> merged;
    check(ImageBufferUtils::mergeRgb(channels, 2, 1, merged) && merged == rgb,
          "RGB split and merge should round-trip exactly");

    std::vector<uint16_t> luminance;
    check(ImageBufferUtils::extractLuminance(rgb, 2, 1, luminance),
          "Luminance extraction should accept an exact buffer");
    check(luminance == std::vector<uint16_t>({1815, 4815}),
          "Luminance extraction should use the documented integer weights");

    const std::vector<uint16_t> previous = {42};
    luminance = previous;
    check(!ImageBufferUtils::extractLuminance(rgb, 3, 1, luminance) &&
              luminance == previous,
          "A failed buffer conversion should leave its output unchanged");

    std::vector<uint16_t> processed = {
        100, 200, 300,
        400, 500, 600,
        700, 800, 900
    };
    const std::vector<uint16_t> protectedGround = {
        1000, 2000, 3000,
        4000, 5000, 6000,
        7000, 8000, 9000
    };
    const std::vector<uint8_t> blendMask = {255, 128, 0};
    check(ImageBufferUtils::blendSkyGroundInPlace(
              processed, protectedGround, blendMask, 3, 1),
          "Sky/ground RGB blending should accept exact buffers");
    check(processed[0] == 100 && processed[1] == 200 && processed[2] == 300,
          "Sky/ground RGB blending should preserve fully processed sky");
    check(processed[6] == 7000 && processed[7] == 8000 &&
              processed[8] == 9000,
          "Sky/ground RGB blending should restore fully protected ground");
    check(processed[3] == 2193 && processed[4] == 2741 &&
              processed[5] == 3289,
          "Sky/ground RGB blending should feather the horizon with integer rounding");

    std::vector<uint16_t> alignedSkySamples = {
        100, 200, 300,
        400, 500, 600,
        700, 800, 900
    };
    const std::vector<uint8_t> alignedSourceMask = {0, 127, 255};
    const std::vector<uint8_t> referenceMask = {255, 255, 0};
    check(ImageBufferUtils::excludeShiftedGroundInPlace(
              alignedSkySamples, alignedSourceMask, referenceMask, 3, 1),
          "Aligned sky filtering should accept exact RGB and mask buffers");
    check(alignedSkySamples == std::vector<uint16_t>({
              0, 0, 0, 0, 0, 0, 700, 800, 900}),
          "Only hard-valid source sky may enter a hard-valid reference sky pixel");
    const std::vector<uint16_t> preservedAligned = alignedSkySamples;
    check(!ImageBufferUtils::excludeShiftedGroundInPlace(
              alignedSkySamples, {0}, referenceMask, 3, 1) &&
              alignedSkySamples == preservedAligned,
          "Failed aligned sky filtering should leave RGB unchanged");

    const std::vector<uint16_t> resizeSource = {
        0, 1000, 2000,       10000, 11000, 12000,
        20000, 21000, 22000, 30000, 31000, 32000
    };
    std::vector<uint16_t> resizedRgb;
    int resizedWidth = 0;
    int resizedHeight = 0;
    check(ImageBufferUtils::resizeRgb16ToLongSide(
              resizeSource, 2, 2, 1,
              resizedRgb, resizedWidth, resizedHeight) &&
              resizedWidth == 1 && resizedHeight == 1 &&
              resizedRgb == std::vector<uint16_t>({15000, 16000, 17000}),
          "Bounded RGB16 resize should use center-aligned bilinear sampling");
    const std::vector<uint8_t> resizeMask = {0, 64, 192, 255};
    std::vector<uint8_t> resizedMask;
    check(ImageBufferUtils::resizeMask8(
              resizeMask, 2, 2, 1, 1, resizedMask) &&
              resizedMask == std::vector<uint8_t>({128}),
          "Mask resize should preserve feather values with bilinear sampling");
    const std::vector<uint16_t> preservedResize = {7};
    resizedRgb = preservedResize;
    check(!ImageBufferUtils::resizeRgb16ToLongSide(
              resizeSource, 3, 2, 1,
              resizedRgb, resizedWidth, resizedHeight) &&
              resizedRgb == preservedResize,
          "Failed RGB16 resize should leave the output unchanged");
}

void testRgbAutoOptimize() {
    constexpr int width = 8;
    constexpr int height = 8;
    std::vector<uint16_t> castRgb(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        const uint16_t detail = static_cast<uint16_t>((i % width) * 40);
        castRgb[i * 3] = static_cast<uint16_t>(6000 + detail);
        castRgb[i * 3 + 1] = static_cast<uint16_t>(2200 + detail);
        castRgb[i * 3 + 2] = static_cast<uint16_t>(1200 + detail);
    }
    castRgb[3 * 3] = 36000;
    castRgb[3 * 3 + 1] = 18000;
    castRgb[3 * 3 + 2] = 9000;

    std::vector<uint16_t> neutralized;
    check(AutoOptimizeEngine::neutralizeBackgroundRgb(
              castRgb, width, height, neutralized),
          "RGB background neutralization should accept an exact buffer");
    check(std::abs(static_cast<int>(neutralized[0]) -
                   static_cast<int>(neutralized[1])) <= 1 &&
              std::abs(static_cast<int>(neutralized[1]) -
                       static_cast<int>(neutralized[2])) <= 1,
          "Background neutralization should remove an additive channel cast");
    check(neutralized[3 * 3] > neutralized[3 * 3 + 1] &&
              neutralized[3 * 3 + 1] > neutralized[3 * 3 + 2],
          "Background neutralization should retain colored star ordering");

    std::vector<uint16_t> stretched;
    check(AutoOptimizeEngine::stretchRgb(
              castRgb, width, height, stretched),
          "Linked RGB stretch should process a valid image");
    check(stretched[3 * 3] > stretched[3 * 3 + 1] &&
              stretched[3 * 3 + 1] > stretched[3 * 3 + 2],
          "Linked RGB stretch should preserve strong star color ordering: " +
              std::to_string(stretched[3 * 3]) + "/" +
              std::to_string(stretched[3 * 3 + 1]) + "/" +
              std::to_string(stretched[3 * 3 + 2]));

    constexpr int modifiedWidth = 32;
    constexpr int modifiedHeight = 32;
    std::vector<uint16_t> modified(
        modifiedWidth * modifiedHeight * 3);
    for (int y = 0; y < modifiedHeight; ++y) {
        for (int x = 0; x < modifiedWidth; ++x) {
            const size_t base =
                (static_cast<size_t>(y) * modifiedWidth + x) * 3;
            modified[base] = 6000;
            modified[base + 1] = 2200;
            modified[base + 2] = 1200;
            if (std::hypot(x - 16.0, y - 12.0) <= 4.0) {
                modified[base] += 5000; // Localized H-alpha emission.
            }
        }
    }
    std::vector<uint16_t> restored;
    ModifiedCameraColorStats colorStats;
    check(AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              modified, modifiedWidth, modifiedHeight,
              restored, &colorStats),
          "Modified-camera color restoration should process linear RGB");
    const size_t background =
        (static_cast<size_t>(2) * modifiedWidth + 2) * 3;
    const int restoredMaximum = std::max({
        static_cast<int>(restored[background]),
        static_cast<int>(restored[background + 1]),
        static_cast<int>(restored[background + 2])});
    const int restoredMinimum = std::min({
        static_cast<int>(restored[background]),
        static_cast<int>(restored[background + 1]),
        static_cast<int>(restored[background + 2])});
    check(restoredMaximum - restoredMinimum <= 2 &&
              colorStats.gains[0] < 0.6 &&
              colorStats.gains[2] > 2.0,
          "BCF restoration should neutralize a strong red response");
    const size_t nebula =
        (static_cast<size_t>(12) * modifiedWidth + 16) * 3;
    check(static_cast<int>(restored[nebula]) - restored[nebula + 1] >= 2000,
          "BCF restoration should retain localized H-alpha contrast");

    ModifiedCameraColorOptions manualColor;
    manualColor.neutralMode = ModifiedCameraNeutralMode::ManualPoint;
    manualColor.manualPointX = 0.1;
    manualColor.manualPointY = 0.1;
    manualColor.strength = 50;
    std::vector<uint16_t> halfRestored;
    check(AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              modified, modifiedWidth, modifiedHeight,
              halfRestored, &colorStats, nullptr, manualColor) &&
              colorStats.usedManualPoint && colorStats.sampleCount >= 9 &&
              colorStats.gains[0] > 0.5 && colorStats.gains[0] < 1.0 &&
              colorStats.gains[2] > 1.0 && colorStats.gains[2] < 2.0,
          "Manual gray point should use a robust patch and interpolate strength");
    const int originalSpread = 6000 - 1200;
    const int halfMaximum = std::max({
        static_cast<int>(halfRestored[background]),
        static_cast<int>(halfRestored[background + 1]),
        static_cast<int>(halfRestored[background + 2])});
    const int halfMinimum = std::min({
        static_cast<int>(halfRestored[background]),
        static_cast<int>(halfRestored[background + 1]),
        static_cast<int>(halfRestored[background + 2])});
    check(halfMaximum - halfMinimum > 2 &&
              halfMaximum - halfMinimum < originalSpread,
          "Half-strength gray-point correction should be continuous");

    ModifiedCameraColorOptions disabledColor = manualColor;
    disabledColor.strength = 0;
    check(AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              modified, modifiedWidth, modifiedHeight,
              halfRestored, &colorStats, nullptr, disabledColor) &&
              halfRestored == modified && !colorStats.applied,
          "Zero-strength modified-camera correction should be bit exact");

    ModifiedCameraColorOptions invalidManual = manualColor;
    invalidManual.manualPointX = 1.1;
    check(!AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              modified, modifiedWidth, modifiedHeight,
              halfRestored, &colorStats, nullptr, invalidManual),
          "Manual gray point should reject out-of-range coordinates");

    std::vector<uint16_t> splitColor(
        modifiedWidth * modifiedHeight * 3);
    for (int y = 0; y < modifiedHeight; ++y) {
        for (int x = 0; x < modifiedWidth; ++x) {
            const size_t base =
                (static_cast<size_t>(y) * modifiedWidth + x) * 3;
            const bool left = x < modifiedWidth / 2;
            splitColor[base] = left ? 6000 : 1000;
            splitColor[base + 1] = 2200;
            splitColor[base + 2] = left ? 1000 : 6000;
        }
    }
    manualColor.strength = 100;
    manualColor.manualPointX = 0.0;
    check(AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              splitColor, modifiedWidth, modifiedHeight,
              halfRestored, &colorStats, nullptr, manualColor) &&
              colorStats.neutralSample[0] > colorStats.neutralSample[2],
          "Manual gray point 0 should sample the first image column");
    manualColor.manualPointX = 1.0;
    check(AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              splitColor, modifiedWidth, modifiedHeight,
              halfRestored, &colorStats, nullptr, manualColor) &&
              colorStats.neutralSample[2] > colorStats.neutralSample[0],
          "Manual gray point 1 should clamp to the final image column");

    std::vector<uint16_t> neutralInput(
        modifiedWidth * modifiedHeight * 3, 4000);
    check(AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              neutralInput, modifiedWidth, modifiedHeight,
              restored, &colorStats) && restored == neutralInput &&
              std::all_of(colorStats.gains.begin(), colorStats.gains.end(),
                          [](double gain) {
                              return std::abs(gain - 1.0) < 1e-9;
                          }),
          "Modified-camera restoration should leave neutral input unchanged");

    const std::vector<uint8_t> invalidSkyMask(
        modifiedWidth * modifiedHeight - 1, 255);
    check(!AutoOptimizeEngine::restoreModifiedCameraColorRgb(
              neutralInput, modifiedWidth, modifiedHeight,
              restored, &colorStats, &invalidSkyMask) &&
              !AutoOptimizeEngine::restoreModifiedCameraColorRgb(
                  neutralInput, modifiedWidth + 1, modifiedHeight,
                  restored, &colorStats),
          "Modified-camera restoration should reject invalid buffer shapes");

    std::vector<uint16_t> gray(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        const uint16_t value = static_cast<uint16_t>(500 + i * 500);
        gray[i * 3] = gray[i * 3 + 1] = gray[i * 3 + 2] = value;
    }
    check(AutoOptimizeEngine::stretchRgb(gray, width, height, stretched),
          "Linked RGB stretch should process neutral data");
    for (size_t i = 0; i < stretched.size(); i += 3) {
        check(stretched[i] == stretched[i + 1] &&
                  stretched[i + 1] == stretched[i + 2],
              "Linked RGB stretch should keep neutral pixels neutral");
    }

    constexpr int nightWidth = 100;
    constexpr int nightHeight = 100;
    std::vector<uint16_t> nightscape(nightWidth * nightHeight * 3);
    for (int pixel = 0; pixel < nightWidth * nightHeight; ++pixel) {
        const uint16_t value = static_cast<uint16_t>(950 + pixel % 101);
        nightscape[pixel * 3] = value;
        nightscape[pixel * 3 + 1] = value;
        nightscape[pixel * 3 + 2] = value;
    }
    for (int star = 0; star < 10; ++star) {
        const size_t base = static_cast<size_t>(star * 997) * 3;
        nightscape[base] = nightscape[base + 1] =
            nightscape[base + 2] = 30000;
    }
    check(AutoOptimizeEngine::stretchRgb(
              nightscape, nightWidth, nightHeight, stretched),
          "Linked RGB stretch should process a sparse synthetic star field");
    std::vector<uint16_t> stretchedBackground;
    stretchedBackground.reserve(nightWidth * nightHeight - 10);
    for (int pixel = 10; pixel < nightWidth * nightHeight; ++pixel) {
        stretchedBackground.push_back(stretched[pixel * 3]);
    }
    const auto middle = stretchedBackground.begin() +
        static_cast<ptrdiff_t>(stretchedBackground.size() / 2);
    std::nth_element(stretchedBackground.begin(), middle,
                     stretchedBackground.end());
    check(*middle >= 9000 && *middle <= 12000,
          "Automatic stretch should keep a typical background near 16 percent");
    check(stretched[0] >= 52000,
          "Automatic stretch should retain bright-star highlight headroom");

    std::vector<uint16_t> emissionField(
        static_cast<size_t>(nightWidth) * nightHeight * 3);
    for (int pixel = 0; pixel < nightWidth * nightHeight; ++pixel) {
        const uint16_t value = static_cast<uint16_t>(950 + pixel % 101);
        emissionField[pixel * 3] = value;
        emissionField[pixel * 3 + 1] = value;
        emissionField[pixel * 3 + 2] = value;
    }
    emissionField[0] = 60000;
    emissionField[1] = 10000;
    emissionField[2] = 5000;
    emissionField[3] = 45000;
    emissionField[4] = 9000;
    emissionField[5] = 4000;
    check(AutoOptimizeEngine::stretchRgb(
              emissionField, nightWidth, nightHeight, stretched),
          "Automatic stretch should process a high-chroma emission core");
    const uint16_t brightestChannel = *std::max_element(
        stretched.begin(), stretched.end());
    check(brightestChannel <= 65210,
          "Linked stretch should gamut-compress chroma instead of clipping "
          "a dominant channel (maximum=" +
              std::to_string(brightestChannel) + ")");
    check(stretched[0] > stretched[1] && stretched[1] > stretched[2],
          "Highlight gamut compression should retain emission color ordering");
    const auto outputLuminance = [&](size_t pixel) {
        const size_t base = pixel * 3;
        return (13933ULL * stretched[base] +
                46871ULL * stretched[base + 1] +
                4732ULL * stretched[base + 2]) >> 16;
    };
    check(outputLuminance(0) > outputLuminance(1),
          "Samples above the robust white point should keep highlight detail");

    std::vector<uint16_t> dehazed;
    check(AutoOptimizeEngine::dehazeRgb(castRgb, width, height, 0, dehazed) &&
              dehazed == castRgb,
          "Zero-strength RGB dehaze should be an exact no-op");
    std::vector<uint16_t> flatMono(32 * 32, 5000);
    std::vector<uint16_t> flatDehazed;
    check(AutoOptimizeEngine::dehaze(
              flatMono, 32, 32, 50, flatDehazed) &&
              std::all_of(flatDehazed.begin(), flatDehazed.end(),
                          [](uint16_t value) {
                              return std::abs(static_cast<int>(value) - 5000) <= 1;
                          }),
          "Guided dehaze should keep a flat field spatially uniform");
    flatDehazed = {42};
    check(!AutoOptimizeEngine::dehaze(
              flatMono, 32, 32, 101, flatDehazed) &&
              flatDehazed == std::vector<uint16_t>({42}),
          "Mono dehaze should reject strength outside its 0-100 contract");
    const std::vector<uint16_t> previous = {42};
    dehazed = previous;
    check(!AutoOptimizeEngine::dehazeRgb(
              castRgb, width + 1, height, 20, dehazed) &&
              dehazed == previous,
          "Failed RGB optimization should leave output unchanged");

    constexpr int gradientWidth = 64;
    constexpr int gradientHeight = 64;
    std::vector<uint16_t> gradient(
        gradientWidth * gradientHeight * 3);
    for (int y = 0; y < gradientHeight; ++y) {
        for (int x = 0; x < gradientWidth; ++x) {
            const size_t base =
                (static_cast<size_t>(y) * gradientWidth + x) * 3;
            gradient[base] = static_cast<uint16_t>(5000 + x * 20 + y * 8);
            gradient[base + 1] =
                static_cast<uint16_t>(2400 + x * 10 + y * 4);
            gradient[base + 2] =
                static_cast<uint16_t>(1300 + x * 5 + y * 2);
        }
    }
    check(AutoOptimizeEngine::neutralizeBackgroundRgb(
              gradient, gradientWidth, gradientHeight, neutralized),
          "Spatial background neutralization should process a smooth gradient");
    int maximumAdjacentJump = 0;
    for (int x = 1; x < gradientWidth; ++x) {
        const size_t previousPixel =
            (static_cast<size_t>(gradientHeight / 2) * gradientWidth + x - 1) * 3;
        const size_t currentPixel = previousPixel + 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            maximumAdjacentJump = std::max(
                maximumAdjacentJump,
                std::abs(static_cast<int>(neutralized[currentPixel + channel]) -
                         static_cast<int>(neutralized[previousPixel + channel])));
        }
    }
    check(maximumAdjacentJump < 64,
          "Spatial background interpolation should remain continuous at grid edges");
    int maximumSlopeChange = 0;
    std::array<int, 3> previousSlope = {};
    for (int x = 1; x < gradientWidth; ++x) {
        const size_t previousPixel =
            (static_cast<size_t>(gradientHeight / 2) * gradientWidth + x - 1) * 3;
        const size_t currentPixel = previousPixel + 3;
        for (size_t channel = 0; channel < 3; ++channel) {
            const int slope =
                static_cast<int>(neutralized[currentPixel + channel]) -
                static_cast<int>(neutralized[previousPixel + channel]);
            if (x > 1) {
                maximumSlopeChange = std::max(
                    maximumSlopeChange,
                    std::abs(slope - previousSlope[channel]));
            }
            previousSlope[channel] = slope;
        }
    }
    check(maximumSlopeChange < 16,
          "Spatial background correction should not introduce a hard contour "
          "where a channel crosses the background target (maximum slope change=" +
              std::to_string(maximumSlopeChange) + ")");

    // A stepped synthetic ridge used to contaminate the coarse low-percentile
    // grid: cells containing dark land estimated a lower sky background and
    // left a terrain-shaped residual halo. Sky-only sampling must make the
    // correction independent of the ridge while preserving land bit-exact.
    constexpr int ridgeWidth = 192;
    constexpr int ridgeHeight = 128;
    std::vector<uint16_t> ridgeRgb(
        static_cast<size_t>(ridgeWidth) * ridgeHeight * 3);
    std::vector<uint8_t> ridgeSkyMask(
        static_cast<size_t>(ridgeWidth) * ridgeHeight);
    for (int y = 0; y < ridgeHeight; ++y) {
        for (int x = 0; x < ridgeWidth; ++x) {
            const int ridgeY = x < ridgeWidth / 2 ? 72 : 96;
            const bool sky = y < ridgeY;
            const size_t pixel = static_cast<size_t>(y) * ridgeWidth + x;
            const size_t base = pixel * 3;
            ridgeSkyMask[pixel] = sky ? 255 : 0;
            if (sky) {
                ridgeRgb[base] = static_cast<uint16_t>(7600 + y * 18);
                ridgeRgb[base + 1] = static_cast<uint16_t>(4300 + y * 10);
                ridgeRgb[base + 2] = static_cast<uint16_t>(2700 + y * 6);
            } else {
                ridgeRgb[base] = static_cast<uint16_t>(900 + x % 17);
                ridgeRgb[base + 1] = static_cast<uint16_t>(1100 + x % 13);
                ridgeRgb[base + 2] = static_cast<uint16_t>(1300 + x % 11);
            }
        }
    }
    const std::vector<uint16_t> ridgeInput = ridgeRgb;
    check(AutoOptimizeEngine::neutralizeBackgroundRgb(
              ridgeRgb, ridgeWidth, ridgeHeight,
              neutralized, &ridgeSkyMask),
          "Sky-masked background neutralization should process a nightscape ridge");
    const auto ridgeIndex = [&](int x, int y, int channel) {
        return (static_cast<size_t>(y) * ridgeWidth + x) * 3 + channel;
    };
    int ridgeSkyDifference = 0;
    for (int channel = 0; channel < 3; ++channel) {
        ridgeSkyDifference = std::max(
            ridgeSkyDifference,
            std::abs(static_cast<int>(neutralized[
                         ridgeIndex(48, 60, channel)]) -
                     static_cast<int>(neutralized[
                         ridgeIndex(144, 60, channel)])));
    }
    check(ridgeSkyDifference < 96,
          "Sky correction should not inherit a stepped terrain silhouette "
          "(difference=" + std::to_string(ridgeSkyDifference) + ")");
    bool protectedGroundExact = true;
    for (int y = 96; y < ridgeHeight; ++y) {
        for (int x = 0; x < ridgeWidth; ++x) {
            for (int channel = 0; channel < 3; ++channel) {
                const size_t index = ridgeIndex(x, y, channel);
                protectedGroundExact = protectedGroundExact &&
                    neutralized[index] == ridgeInput[index];
            }
        }
    }
    check(protectedGroundExact,
          "Sky background correction should leave fully protected ground bit-exact");
    const std::vector<uint8_t> allGroundMask(
        static_cast<size_t>(ridgeWidth) * ridgeHeight, 0);
    check(AutoOptimizeEngine::neutralizeBackgroundRgb(
              ridgeRgb, ridgeWidth, ridgeHeight,
              neutralized, &allGroundMask) && neutralized == ridgeRgb,
          "An all-ground mask should safely skip background correction");
    constexpr int tinyWidth = 16;
    constexpr int tinyHeight = 12;
    const std::vector<uint16_t> tinyRgb(
        static_cast<size_t>(tinyWidth) * tinyHeight * 3, 4096);
    const std::vector<uint8_t> tinyGroundMask(
        static_cast<size_t>(tinyWidth) * tinyHeight, 0);
    check(AutoOptimizeEngine::neutralizeBackgroundRgb(
              tinyRgb, tinyWidth, tinyHeight,
              neutralized, &tinyGroundMask) && neutralized == tinyRgb,
          "Small-image fallback should respect an all-ground mask");
    const std::vector<uint8_t> wrongOptimizeMask(3, 255);
    const std::vector<uint16_t> preservedNeutralized = {17, 23, 42};
    neutralized = preservedNeutralized;
    check(!AutoOptimizeEngine::neutralizeBackgroundRgb(
              ridgeRgb, ridgeWidth, ridgeHeight,
              neutralized, &wrongOptimizeMask) &&
              neutralized == preservedNeutralized,
          "Background neutralization should reject a mismatched sky mask "
          "without changing output");

    constexpr int detailWidth = 16;
    constexpr int detailHeight = 24;
    std::vector<uint16_t> detailRgb(detailWidth * detailHeight * 3);
    std::vector<uint8_t> skyMask(detailWidth * detailHeight);
    for (int y = 0; y < detailHeight; ++y) {
        for (int x = 0; x < detailWidth; ++x) {
            const uint16_t value = x < detailWidth / 2 ? 6000 : 14000;
            const size_t pixel = static_cast<size_t>(y) * detailWidth + x;
            detailRgb[pixel * 3] = detailRgb[pixel * 3 + 1] =
                detailRgb[pixel * 3 + 2] = value;
            skyMask[pixel] = y < detailHeight / 2 ? 255 : 0;
        }
    }
    const std::vector<uint16_t> detailInput = detailRgb;
    const auto valueAt = [&](const std::vector<uint16_t>& image,
                             int x, int y) {
        return image[(static_cast<size_t>(y) * detailWidth + x) * 3];
    };
    const int contrastBefore =
        valueAt(detailRgb, 8, 14) - valueAt(detailRgb, 7, 14);
    check(AutoOptimizeEngine::enhanceGroundDetail(
              detailRgb, detailWidth, detailHeight, skyMask, 50),
          "Ground detail recovery should accept a matching RGB image and mask");
    const int nearHorizonContrast =
        valueAt(detailRgb, 8, 14) - valueAt(detailRgb, 7, 14);
    const int nearForegroundContrast =
        valueAt(detailRgb, 8, 22) - valueAt(detailRgb, 7, 22);
    check(std::equal(detailRgb.begin(),
                     detailRgb.begin() + detailWidth * (detailHeight / 2) * 3,
                     detailInput.begin()),
          "Ground detail recovery should leave the sky region bit-exact");
    check(nearHorizonContrast > contrastBefore,
          "Ground detail recovery should increase foreground edge contrast");
    check(nearHorizonContrast > nearForegroundContrast,
          "Medium-scale ground clarity should favor distant terrain near the horizon");
    bool detailStayedNeutral = true;
    for (size_t base = 0; base < detailRgb.size(); base += 3) {
        detailStayedNeutral = detailStayedNeutral &&
            detailRgb[base] == detailRgb[base + 1] &&
            detailRgb[base + 1] == detailRgb[base + 2];
    }
    check(detailStayedNeutral,
          "Ground luminance sharpening should keep neutral RGB pixels neutral");
    check(!AutoOptimizeEngine::enhanceGroundDetail(
              detailRgb, detailWidth, detailHeight,
              std::vector<uint8_t>(3), 50),
          "Ground detail recovery should reject a mismatched mask");
}

void testRgbTransform() {
    const int width = 3;
    const int height = 2;
    const std::vector<uint16_t> rgb = {
        1, 2, 3, 4, 5, 6, 7, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17, 18
    };
    ImageAligner aligner;
    AlignmentTransform identity;
    std::vector<uint16_t> transformed;
    check(aligner.applyTransformRgb(rgb, width, height, identity, transformed),
          "Interleaved RGB transform should accept a valid buffer");
    // Bilinear resampling intentionally zero-fills the last row and column,
    // where a complete 2x2 source neighborhood is unavailable.
    check(transformed == std::vector<uint16_t>({
              1, 2, 3, 4, 5, 6, 0, 0, 0,
              0, 0, 0, 0, 0, 0, 0, 0, 0}),
          "Identity RGB transform should preserve valid samples and border policy");
    check(!aligner.applyTransformRgb({1, 2, 3}, width, height, identity, transformed),
          "Interleaved RGB transform should reject a truncated buffer");

    const std::vector<uint8_t> mask = {0, 64, 128, 192, 224, 255};
    std::vector<uint8_t> transformedMask;
    check(aligner.applyTransformMask(
              mask, width, height, identity, transformedMask) &&
              transformedMask == std::vector<uint8_t>({0, 64, 0, 0, 0, 0}),
          "Mask transform should match RGB inverse mapping and border policy");
    const std::vector<uint8_t> preservedMask = {17};
    transformedMask = preservedMask;
    check(!aligner.applyTransformMask(
              {1, 2, 3}, width, height, identity, transformedMask) &&
              transformedMask == preservedMask,
          "Failed mask transform should leave output unchanged");

    constexpr int maskWidth = 5;
    constexpr int maskHeight = 4;
    const std::vector<uint8_t> confidenceMask = {
        0, 0, 64, 255, 255,
        0, 32, 128, 255, 255,
        0, 96, 192, 255, 255,
        0, 128, 224, 255, 255
    };
    std::vector<uint16_t> confidence16(confidenceMask.size());
    std::transform(
        confidenceMask.begin(), confidenceMask.end(), confidence16.begin(),
        [](uint8_t value) { return static_cast<uint16_t>(value) * 257U; });
    auto checkMaskMatchesImageTransform = [&](AlignmentTransform transform,
                                               const std::string& label) {
        std::vector<uint8_t> maskOutput;
        std::vector<uint16_t> imageOutput;
        bool matched = aligner.applyTransformMask(
                           confidenceMask, maskWidth, maskHeight,
                           transform, maskOutput) &&
            aligner.applyTransform(
                confidence16, maskWidth, maskHeight, transform, imageOutput) &&
            maskOutput.size() == imageOutput.size();
        for (size_t index = 0; matched && index < maskOutput.size(); ++index) {
            const int imageValue = static_cast<int>(
                std::lround(imageOutput[index] / 257.0));
            matched = std::abs(imageValue - maskOutput[index]) <= 1;
        }
        check(matched, label);
    };
    AlignmentTransform translated;
    translated.c = 0.35;
    translated.f = -0.2;
    checkMaskMatchesImageTransform(
        translated,
        "Translated mask and image transforms should sample the same coordinates");
    AlignmentTransform projective;
    projective.a = 0.999;
    projective.b = 0.002;
    projective.c = 0.2;
    projective.d = -0.001;
    projective.e = 1.001;
    projective.f = 0.1;
    projective.g = 0.0004;
    projective.h = -0.0003;
    projective.model = AlignmentModel::Homography;
    checkMaskMatchesImageTransform(
        projective,
        "Homography mask and image transforms should sample the same coordinates");
}

void testMemoryEstimator() {
    constexpr uint64_t frameBytes = 6000ULL * 4000ULL * 3ULL * sizeof(uint16_t);
    check(ProcessingMemoryEstimator::estimatePeakBytes(6000, 4000, 20, false) ==
              frameBytes * 8,
          "Disk-backed normal stacking should use a bounded RAM estimate");
    check(ProcessingMemoryEstimator::estimatePeakBytes(6000, 4000, 20, true) ==
              frameBytes * 10,
          "Disk-backed sky/ground stacking should use a bounded RAM estimate");
    check(ProcessingMemoryEstimator::estimateScratchDiskBytes(6000, 4000, 20, false) ==
              frameBytes * 20,
          "Normal stacking should reserve one cached frame per input");
    check(ProcessingMemoryEstimator::estimateScratchDiskBytes(6000, 4000, 20, true) ==
              frameBytes * 40,
          "Sky/ground stacking should reserve aligned and original caches");
    check(ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
              6000, 4000, 5, true) == frameBytes * 15,
          "Protected five-frame timelapse should budget both coordinate paths");
    check(ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
              6000, 4000, 3, false) == frameBytes * 8,
          "Sky-only timelapse should budget one temporal window");
    check(ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
              6000, 4000, 3, false, false) == frameBytes * 7,
          "Disabled motion protection should not reserve guide buffers");
    check(ProcessingMemoryEstimator::estimateTimelapsePeakBytes(
              6000, 4000, 4, true) == 0,
          "Timelapse estimate should reject even windows");

    ProcessingMemoryEstimator::EstimateOptions calibrated;
    calibrated.frameCount = 10;
    calibrated.rawCalibration = true;
    check(ProcessingMemoryEstimator::estimatePeakBytes(
              6000, 4000, calibrated) == frameBytes * 12,
          "Bayer calibration should reserve master and CFA working buffers");
    check(ProcessingMemoryEstimator::estimatePeakBytes(0, 4000, 20, false) == 0,
          "Memory estimator should reject invalid dimensions");
    constexpr uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    const uint64_t reserve16GiB = (16 * gib * 10) / 100;
    check(ProcessingMemoryEstimator::calculateSafeBudgetBytes(16 * gib, 12 * gib) ==
              ((12 * gib - reserve16GiB) * 85) / 100,
          "Memory budget should reserve OS headroom from current availability");
    check(ProcessingMemoryEstimator::calculateSafeBudgetBytes(16 * gib, 4 * gib) ==
              ((4 * gib - reserve16GiB) * 85) / 100,
          "Memory budget should shrink when current availability is lower");
    check(ProcessingMemoryEstimator::calculateSafeBudgetBytes(
              16 * gib, gib / 2) == 0,
          "Memory budget should saturate at zero below the system reserve");
    check(ProcessingMemoryEstimator::calculateSafeBudgetBytes(0, 0) == 8 * gib,
          "Memory budget should use the documented fallback when queries fail");
    check(ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
              4 * gib, 10 * gib) == 4 * gib &&
              ProcessingMemoryEstimator::calculateEffectiveBudgetBytes(
                  4 * gib, 2 * gib) == 2 * gib,
          "User memory limits should tighten but never bypass the platform budget");
    ProcessingMemoryEstimator::EstimateOptions baseOptions;
    baseOptions.frameCount = 20;
    const uint64_t baseEstimate =
        ProcessingMemoryEstimator::estimatePeakBytes(6000, 4000, baseOptions);
    ProcessingMemoryEstimator::EstimateOptions dehazeOptions = baseOptions;
    dehazeOptions.dehaze = true;
    check(ProcessingMemoryEstimator::estimatePeakBytes(
              6000, 4000, dehazeOptions) == frameBytes * 9,
          "Dehaze should reserve its reduced guided-filter working set");
    ProcessingMemoryEstimator::EstimateOptions denoiseOptions = baseOptions;
    denoiseOptions.noiseReduction = true;
    check(ProcessingMemoryEstimator::estimatePeakBytes(
              6000, 4000, denoiseOptions) >= baseEstimate,
          "Denoise should never reduce the estimated pipeline peak");
    ProcessingMemoryEstimator::EstimateOptions starOptions = baseOptions;
    starOptions.starReduction = true;
    check(ProcessingMemoryEstimator::estimatePeakBytes(
              6000, 4000, starOptions) >= frameBytes * 8,
          "Starless separation should reserve signed layer working buffers");
    ProcessingMemoryEstimator::EstimateOptions largerChunkOptions = baseOptions;
    largerChunkOptions.frameCount = 200;
    largerChunkOptions.chunkRows = 128;
    check(ProcessingMemoryEstimator::estimatePeakBytes(
              6000, 4000, largerChunkOptions) >= baseEstimate,
          "Chunk memory estimate should grow monotonically with frames and rows");
    const ProcessingMemoryEstimator::SystemMemoryInfo memoryInfo =
        ProcessingMemoryEstimator::systemMemoryInfo();
    if (memoryInfo.availableBytes == 0) {
        check(memoryInfo.safeBudgetBytes > 0,
              "Memory budget should fall back when availability is unavailable");
    } else {
        check(memoryInfo.safeBudgetBytes <= memoryInfo.availableBytes,
              "System memory budget should not exceed available RAM");
        if (memoryInfo.totalBytes > 0) {
            check(memoryInfo.safeBudgetBytes <= memoryInfo.totalBytes,
                  "System memory budget should not exceed total RAM");
        }
    }
}

void testPreviewToneMapper() {
    const std::vector<uint16_t> mono = {0, 1000, 2000, 65535};
    const PreviewImage8 monoPreview = PreviewToneMapper::mapMono16(mono, 4, 1, 2);
    check(monoPreview.width == 2 && monoPreview.height == 1,
          "Preview mapper should enforce its long-side limit");
    check(monoPreview.rgb.size() == 6 && monoPreview.rgb[0] == 0,
          "Mono preview should produce an RGB buffer with a black black-point");
    const PreviewImage8 fullMonoPreview = PreviewToneMapper::mapMono16(mono, 4, 1, 4);
    check(fullMonoPreview.rgb.back() == 255,
          "Preview tone mapping should map its white point to full display white");

    std::vector<uint16_t> rgb(8 * 4 * 3, 0);
    rgb[rgb.size() - 3] = 65535;
    rgb[rgb.size() - 2] = 32768;
    rgb[rgb.size() - 1] = 16384;
    const PreviewImage8 rgbPreview = PreviewToneMapper::mapRgb16(rgb, 8, 4, 4);
    check(rgbPreview.width == 4 && rgbPreview.height == 2,
          "RGB preview should preserve aspect ratio while downsampling");
    check(rgbPreview.rgb.size() == 24,
          "RGB preview should return exactly three bytes per output pixel");
    check(PreviewToneMapper::mapRgb16(rgb, 7, 4).rgb.empty(),
          "Preview mapper should reject a mismatched RGB buffer");

    const std::vector<uint16_t> rangedRgb = {
        0, 0, 0,
        32768, 32768, 32768,
        65535, 65535, 65535,
    };
    const PreviewImage8 rangedPreview = PreviewToneMapper::mapRgb16WithRange(
        rangedRgb, 3, 1, 0, 65535, 3);
    check(rangedPreview.blackPoint == 0 && rangedPreview.whitePoint == 65535,
          "Explicit preview range should be retained for comparable images");
    check(rangedPreview.rgb.front() == 0 && rangedPreview.rgb.back() == 255
              && rangedPreview.rgb[3] > 0 && rangedPreview.rgb[3] < 255,
          "Explicit preview range should map black, midtone, and white consistently");
    check(PreviewToneMapper::mapRgb16WithRange(
              rangedRgb, 3, 1, 1000, 1000, 3).rgb.empty(),
          "Preview mapper should reject an empty explicit display range");
}

void testTransformDirection() {
    constexpr int width = 8;
    constexpr int height = 6;
    std::vector<uint16_t> source(width * height, 0);
    source[2 * width + 2] = 50000;

    AlignmentTransform transform;
    transform.c = 2.0; // source x=2 maps to destination x=4
    std::vector<uint16_t> destination;
    ImageAligner aligner;
    check(aligner.applyTransform(source, width, height, transform, destination),
          "Affine resampling should succeed");
    check(destination[2 * width + 4] == 50000,
          "Affine resampling should use the documented source-to-reference direction");

    AlignmentTransform singular;
    singular.a = 0.0;
    singular.e = 0.0;
    check(!aligner.applyTransform(source, width, height, singular, destination),
          "Affine resampling should reject a singular transform");
    check(!aligner.applyTransform(source, width + 1, height, transform, destination),
          "Affine resampling should reject a mismatched source buffer");

    std::vector<uint16_t> projectiveSource(width * height, 0);
    projectiveSource[2] = 42000;
    AlignmentTransform projective;
    projective.a = 1.0;
    projective.c = 2.8;
    projective.g = 0.1;
    projective.model = AlignmentModel::Homography;
    check(aligner.applyTransform(projectiveSource, width, height,
                                 projective, destination),
          "Projective resampling should accept a valid homography");
    check(destination[4] > 41000,
          "Projective resampling should use the inverse source mapping");

    std::vector<uint16_t> rgbSource(
        static_cast<size_t>(width) * height * 3, 1000);
    std::vector<uint16_t> rgbDestination;
    AlignmentTransform denominatorAtPixel;
    denominatorAtPixel.g = 0.25;
    denominatorAtPixel.model = AlignmentModel::Homography;
    check(aligner.applyTransformRgb(
              rgbSource, width, height, denominatorAtPixel, rgbDestination),
          "RGB resampling should safely handle an inverse-map singular line");
    const size_t singularPixel = static_cast<size_t>(4) * 3;
    check(rgbDestination[singularPixel] == 0 &&
              rgbDestination[singularPixel + 1] == 0 &&
              rgbDestination[singularPixel + 2] == 0,
          "RGB resampling should zero pixels whose inverse map is undefined");

    AlignmentTransform translated;
    translated.c = 10.0;
    translated.f = 5.0;
    AlignmentBounds commonBounds;
    check(aligner.commonValidBounds(
              {translated}, 100, 80, commonBounds),
          "Alignment should find a common valid crop for translated frames");
    check(commonBounds.x >= 10 && commonBounds.x <= 12 &&
              commonBounds.y >= 5 && commonBounds.y <= 7 &&
              commonBounds.x + commonBounds.width <= 99 &&
              commonBounds.y + commonBounds.height <= 79,
          "Common crop should exclude inverse-mapping padding with safety margin");
    check(!aligner.commonValidBounds({}, 100, 80, commonBounds),
          "Common crop should reject an empty transform set");
}

void testAlignmentEstimation() {
    const std::vector<StarPoint> reference = {
        {80.0, 90.0}, {210.0, 130.0}, {355.0, 75.0}, {520.0, 160.0},
        {125.0, 310.0}, {290.0, 265.0}, {470.0, 350.0}, {650.0, 290.0},
        {190.0, 505.0}, {390.0, 470.0}, {575.0, 540.0}, {730.0, 430.0}
    };
    std::vector<StarPoint> source = reference;
    for (StarPoint& star : source) {
        star.x -= 12.5;
        star.y += 7.25;
    }

    ImageAligner aligner;
    AlignmentTransform transform;
    AlignmentQuality quality;
    check(aligner.align(reference, source, transform, &quality),
          "Triangle matching should recover a translated synthetic star field");
    check(std::abs(transform.a - 1.0) < 0.01 &&
              std::abs(transform.e - 1.0) < 0.01 &&
              std::abs(transform.b) < 0.01 && std::abs(transform.d) < 0.01 &&
              std::abs(transform.c - 12.5) < 0.2 &&
              std::abs(transform.f + 7.25) < 0.2,
          "Estimated alignment should preserve scale and recover translation");
    check(quality.matchedStars == static_cast<int>(reference.size()) &&
              quality.rmsError < 0.01,
          "Independent alignment verification should match every synthetic star");
    check(transform.model == AlignmentModel::Affine,
          "Auto model selection should keep affine for a true translation");

    std::vector<StarPoint> implausiblyScaled = reference;
    for (StarPoint& star : implausiblyScaled) {
        star.x *= 0.1;
        star.y *= 0.1;
    }
    check(!aligner.align(reference, implausiblyScaled, transform),
          "Alignment should reject a physically implausible scale change");

    std::vector<StarPoint> projectiveSource;
    std::vector<StarPoint> projectiveReference;
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 6; ++column) {
            StarPoint sourcePoint;
            sourcePoint.x = 70.0 + column * 130.0 + row * 3.0;
            sourcePoint.y = 60.0 + row * 95.0 + column * 2.0;
            const double denominator =
                1.0 + 0.000035 * sourcePoint.x - 0.000025 * sourcePoint.y;
            StarPoint referencePoint;
            referencePoint.x =
                (1.002 * sourcePoint.x - 0.012 * sourcePoint.y + 9.0) /
                denominator;
            referencePoint.y =
                (0.009 * sourcePoint.x + 0.998 * sourcePoint.y - 6.0) /
                denominator;
            projectiveSource.push_back(sourcePoint);
            projectiveReference.push_back(referencePoint);
        }
    }
    AlignmentOptions projectiveOptions;
    projectiveOptions.imageWidth = 800;
    projectiveOptions.imageHeight = 600;
    check(aligner.align(projectiveReference, projectiveSource, transform,
                        &quality, projectiveOptions),
          "Alignment should recover a mildly projective synthetic star field");
    check(transform.model == AlignmentModel::Homography,
          "Auto model selection should choose homography for spatially varying residuals");
    check(quality.matchedStars == static_cast<int>(projectiveSource.size()) &&
              quality.p95Error < 0.1 && quality.gridCoverage >= 0.75,
          "Homography diagnostics should cover the field with low tail error");

    std::vector<StarPoint> sparseEvaluationReference;
    std::vector<StarPoint> sparseEvaluationSource;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            for (int sample = 0; sample < 3; ++sample) {
                StarPoint point;
                point.x = column * (800.0 / 3.0) + 55.0 + sample * 32.0;
                point.y = row * 200.0 + 45.0 + sample * 28.0;
                sparseEvaluationReference.push_back(point);
                point.x -= 12.5;
                point.y += 7.25;
                sparseEvaluationSource.push_back(point);
            }
        }
    }
    // One false local match used to make a three-star cell's P95 equal its
    // maximum and veto an otherwise sub-pixel 27-star evaluation.
    sparseEvaluationSource[0].x += 5.0;
    AlignmentOptions sparseOptions;
    sparseOptions.imageWidth = 800;
    sparseOptions.imageHeight = 600;
    sparseOptions.allowHomography = false;
    sparseOptions.evaluationReferenceStars = &sparseEvaluationReference;
    sparseOptions.evaluationSourceStars = &sparseEvaluationSource;
    check(aligner.align(reference, source, transform, &quality, sparseOptions),
          "One sparse-cell false match should not reject a precise global transform");
    const bool retainedRawOutlier = std::any_of(
        quality.gridCells.begin(), quality.gridCells.end(),
        [](const AlignmentGridCell& cell) {
            return cell.p95Error > 4.5 && cell.trimmedP95Error < 0.1;
        });
    check(retainedRawOutlier,
          "Alignment diagnostics should retain raw and one-outlier-trimmed cell tails");

    std::vector<StarPoint> locallyBrokenSource = sparseEvaluationSource;
    locallyBrokenSource[1].x += 5.0;
    sparseOptions.evaluationSourceStars = &locallyBrokenSource;
    check(!aligner.align(reference, source, transform, &quality, sparseOptions),
          "Two bad matches in one sparse cell should still reject the transform");
    check(std::find(
              quality.affineCandidate.failureReasons.begin(),
              quality.affineCandidate.failureReasons.end(),
              "cell-rms-above-3px") !=
              quality.affineCandidate.failureReasons.end(),
          "Repeated local residuals should fail the existing cell RMS gate");

    std::vector<StarPoint> concentratedReference;
    std::vector<StarPoint> concentratedSource;
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 6; ++column) {
            StarPoint point;
            point.x = 300.0 + column * 28.0;
            point.y = 225.0 + row * 30.0;
            concentratedReference.push_back(point);
            point.x -= 12.5;
            point.y += 7.25;
            concentratedSource.push_back(point);
        }
    }
    AlignmentOptions concentratedOptions;
    concentratedOptions.imageWidth = 800;
    concentratedOptions.imageHeight = 600;
    concentratedOptions.evaluationReferenceStars = &concentratedReference;
    concentratedOptions.evaluationSourceStars = &concentratedSource;
    check(!aligner.align(reference, source, transform, &quality,
                         concentratedOptions),
          "Alignment quality should reject evaluation stars confined to one grid cell");
    check(!quality.selected,
          "Failed model selection should be explicit in alignment diagnostics");
}

void testStarDetectionAndReduction() {
    constexpr int width = 96;
    constexpr int height = 72;
    std::vector<uint16_t> luminance(width * height, 1000);
    addGaussianStar(luminance, width, height, 18.0, 16.0, 1.4, 42000.0);
    addGaussianStar(luminance, width, height, 70.0, 20.0, 1.7, 36000.0);
    addGaussianStar(luminance, width, height, 48.0, 54.0, 1.5, 40000.0);
    const std::vector<std::pair<int, int>> faintStarCenters = {
        {8, 8}, {32, 9}, {52, 10}, {86, 9},
        {9, 38}, {30, 35}, {60, 37}, {86, 39},
        {13, 63}, {35, 64}, {66, 63}, {86, 62}
    };
    for (const auto& [x, y] : faintStarCenters) {
        addGaussianStar(luminance, width, height, x, y, 0.85, 8000.0);
    }

    DetectionOptions options;
    options.maxCandidates = 2;
    options.maxStars = 2;
    options.spatiallyBalanced = true;
    options.gridCols = 3;
    options.gridRows = 2;
    std::vector<StarPoint> stars;
    StarDetector detector;
    check(detector.detect(luminance, width, height, stars, options),
          "Synthetic star detection should succeed");
    check(!stars.empty() && stars.size() <= 2,
          "DetectionOptions should cap the final star count");

    std::vector<uint16_t> rgb(luminance.size() * 3);
    for (size_t i = 0; i < luminance.size(); ++i) {
        rgb[i * 3] = static_cast<uint16_t>(
            std::max(0, static_cast<int>(luminance[i]) - 200));
        rgb[i * 3 + 1] = luminance[i];
        rgb[i * 3 + 2] = static_cast<uint16_t>(
            std::min(65535, static_cast<int>(luminance[i]) + 400));
    }
    const std::vector<uint16_t> rgbInput = rgb;
    const uint16_t peakBefore = rgb[(16 * width + 18) * 3];
    const size_t faintCenterIndex =
        static_cast<size_t>(62 * width + 86) * 3;
    const uint16_t faintPeakBefore = rgb[faintCenterIndex];
    const uint16_t backgroundBefore = rgb[(65 * width + 5) * 3];
    const std::array<uint16_t, 3> coloredBackground = {
        backgroundBefore,
        rgb[(65 * width + 5) * 3 + 1],
        rgb[(65 * width + 5) * 3 + 2]
    };
    const size_t brightPixelsBefore = static_cast<size_t>(std::count_if(
        luminance.begin(), luminance.end(),
        [](uint16_t value) { return value > 5000; }));
    StarReductionStats reductionStats;
    check(StarReducer::reduce(rgb, width, height, 90, &reductionStats),
          "Star reduction should accept a valid RGB buffer");
    check(rgb[(16 * width + 18) * 3] < peakBefore,
          "Star reduction should lower a detected star peak");
    check(rgb[(65 * width + 5) * 3] == backgroundBefore,
          "Star reduction should preserve pixels outside star masks");
    check(rgb[faintCenterIndex] <
              backgroundBefore +
                  (faintPeakBefore - backgroundBefore) / 4,
          "Strong reduction should fade faint compact stars into the background "
          "(before=" + std::to_string(faintPeakBefore) +
          ", after=" + std::to_string(rgb[faintCenterIndex]) +
          ", background=" + std::to_string(backgroundBefore) + ")");
    for (int channel = 0; channel < 3; ++channel) {
        check(std::abs(static_cast<int>(rgb[faintCenterIndex + channel]) -
                       coloredBackground[channel]) < 300,
              "Removed stars should inherit the local RGB background color "
              "(channel=" + std::to_string(channel) +
              ", output=" +
              std::to_string(rgb[faintCenterIndex + channel]) +
              ", background=" +
              std::to_string(coloredBackground[channel]) + ")");
    }
    check(rgb[(16 * width + 18) * 3] > backgroundBefore + 1000,
          "Strong reduction should retain a visible core for prominent stars");
    const auto redAt = [&](int x, int y) {
        return rgb[(static_cast<size_t>(y) * width + x) * 3];
    };
    check(redAt(18, 16) >= redAt(19, 16) &&
              redAt(19, 16) >= redAt(20, 16) &&
              redAt(20, 16) >= redAt(21, 16),
          "Reduced bright stars should keep a monotonic radial profile without rings");
    check(std::abs(static_cast<int>(redAt(20, 16)) -
                   static_cast<int>(redAt(18, 18))) < 500,
          "Round Minimum should keep axial and diagonal star radii comparable");
    const size_t brightCenter = static_cast<size_t>(16 * width + 18) * 3;
    const int redExcess =
        static_cast<int>(rgb[brightCenter]) - coloredBackground[0];
    const int greenExcess =
        static_cast<int>(rgb[brightCenter + 1]) - coloredBackground[1];
    const int blueExcess =
        static_cast<int>(rgb[brightCenter + 2]) - coloredBackground[2];
    check(std::max({redExcess, greenExcess, blueExcess}) -
              std::min({redExcess, greenExcess, blueExcess}) < 300,
          "Signed star-layer recomposition should not create colored star cores");
    const size_t brightPixelsAfter = static_cast<size_t>(std::count_if(
        rgb.begin(), rgb.end(),
        [](uint16_t value) { return value > 5000; })) / 3;
    check(brightPixelsAfter < brightPixelsBefore,
          "PSF contraction should reduce the bright star footprint");
    check(reductionStats.detectedStars >= 3 &&
              reductionStats.processedStars >= 3 &&
              reductionStats.stronglySuppressedStars > 0 &&
              reductionStats.affectedPixels > 0 &&
              reductionStats.radiusScale < 1.0,
          "Star reduction should report useful processing diagnostics "
          "(detected=" + std::to_string(reductionStats.detectedStars) +
          ", processed=" + std::to_string(reductionStats.processedStars) +
          ", suppressed=" +
          std::to_string(reductionStats.stronglySuppressedStars) +
          ", affected=" + std::to_string(reductionStats.affectedPixels) +
          ", scale=" + std::to_string(reductionStats.radiusScale) + ")");
    check(std::all_of(rgb.begin(), rgb.end(),
                      [backgroundBefore](uint16_t value) {
                          return value >= backgroundBefore;
                      }),
          "Local-background reduction should not create black holes");
    check(!StarReducer::reduce(rgb, width + 1, height, 50),
          "Star reduction should reject mismatched dimensions");

    std::vector<uint16_t> reduced40 = rgbInput;
    std::vector<uint16_t> reduced70 = rgbInput;
    std::vector<uint16_t> reduced1 = rgbInput;
    std::vector<uint16_t> zeroStrength = rgbInput;
    check(StarReducer::reduce(reduced1, width, height, 1) &&
              StarReducer::reduce(reduced40, width, height, 40) &&
              StarReducer::reduce(reduced70, width, height, 70),
          "Intermediate star-reduction strengths should process successfully");
    check(reduced40[faintCenterIndex] >= reduced70[faintCenterIndex] &&
              reduced70[faintCenterIndex] >= rgb[faintCenterIndex],
          "Increasing star-reduction strength should monotonically suppress faint stars");
    const auto totalDifference = [](const std::vector<uint16_t>& first,
                                    const std::vector<uint16_t>& second) {
        uint64_t difference = 0;
        for (size_t index = 0; index < first.size(); ++index) {
            difference += static_cast<uint64_t>(std::abs(
                static_cast<int>(first[index]) -
                static_cast<int>(second[index])));
        }
        return difference;
    };
    check(totalDifference(reduced1, rgbInput) <
              totalDifference(reduced40, rgbInput),
          "Strength 1 should remain a small continuous change rather than "
          "jumping to a fixed defringe amount");
    check(StarReducer::reduce(zeroStrength, width, height, 0) &&
              zeroStrength == rgbInput,
          "Zero star-reduction strength should be an exact no-op");

    // A neutral synthetic star with a saturated blue outer ring approximates
    // longitudinal chromatic aberration in a fast wide-angle lens. Reduction
    // should keep the neutral core while pulling only the fringe toward the
    // core chromaticity.
    constexpr int fringeX = 24;
    constexpr int fringeY = 48;
    const std::array<int, 3> fringeBackground = {1800, 2200, 2800};
    std::vector<uint16_t> fringedRgb(
        static_cast<size_t>(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double distance = std::hypot(x - fringeX, y - fringeY);
            const double neutralCore = 30000.0 *
                std::exp(-(distance * distance) / (2.0 * 1.25 * 1.25));
            const double blueRing = 9000.0 *
                std::exp(-((distance - 3.0) * (distance - 3.0)) /
                         (2.0 * 0.55 * 0.55));
            const size_t pixel = static_cast<size_t>(y) * width + x;
            const std::array<double, 3> additions = {
                neutralCore + blueRing * 0.05,
                neutralCore + blueRing * 0.15,
                neutralCore + blueRing
            };
            for (int channel = 0; channel < 3; ++channel) {
                fringedRgb[pixel * 3 + channel] = static_cast<uint16_t>(
                    std::clamp(std::lround(
                        fringeBackground[channel] + additions[channel]),
                        0L, 65535L));
            }
        }
    }
    const size_t fringeCore =
        static_cast<size_t>(fringeY * width + fringeX) * 3;
    const size_t fringeRing =
        static_cast<size_t>(fringeY * width + fringeX + 3) * 3;
    const auto fringeBlueExcess = [&](const std::vector<uint16_t>& values,
                                      size_t index) {
        const double red = values[index] - fringeBackground[0];
        const double green = values[index + 1] - fringeBackground[1];
        const double blue = values[index + 2] - fringeBackground[2];
        return blue - (red + green) * 0.5;
    };
    const double fringeBefore = fringeBlueExcess(fringedRgb, fringeRing);
    const int coreRedGreenBefore =
        (fringedRgb[fringeCore] - fringeBackground[0]) -
        (fringedRgb[fringeCore + 1] - fringeBackground[1]);
    StarReductionStats fringeStats;
    check(StarReducer::reduce(
              fringedRgb, width, height, 55, &fringeStats),
          "Star reduction should process a chromatic-fringe fixture");
    const double fringeAfter = fringeBlueExcess(fringedRgb, fringeRing);
    const int coreRedGreenAfter =
        (fringedRgb[fringeCore] - fringeBackground[0]) -
        (fringedRgb[fringeCore + 1] - fringeBackground[1]);
    check(fringeStats.defringedPixels > 0 &&
              fringeAfter < fringeBefore * 0.7,
          "Star-edge defringing should reduce blue halo chroma without relying "
          "on global desaturation (before=" + std::to_string(fringeBefore) +
          ", after=" + std::to_string(fringeAfter) +
          ", pixels=" + std::to_string(fringeStats.defringedPixels) + ")");
    check(std::abs(coreRedGreenAfter - coreRedGreenBefore) < 400,
          "Star-edge defringing should preserve the measured core color");

    std::vector<uint16_t> blueStarRgb(
        static_cast<size_t>(width) * height * 3, 2000);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x - 72.0;
            const double dy = y - 50.0;
            const double signal = 28000.0 *
                std::exp(-(dx * dx + dy * dy) / (2.0 * 1.5 * 1.5));
            const size_t pixel = static_cast<size_t>(y) * width + x;
            blueStarRgb[pixel * 3] = static_cast<uint16_t>(2000 + signal * 0.65);
            blueStarRgb[pixel * 3 + 1] =
                static_cast<uint16_t>(2000 + signal * 0.82);
            blueStarRgb[pixel * 3 + 2] =
                static_cast<uint16_t>(2000 + signal * 1.15);
        }
    }
    const size_t blueStarWing = static_cast<size_t>(50 * width + 74) * 3;
    const auto blueRedRatio = [&](const std::vector<uint16_t>& values) {
        const double red = std::max(1, static_cast<int>(
            values[blueStarWing]) - 2000);
        const double blue = std::max(1, static_cast<int>(
            values[blueStarWing + 2]) - 2000);
        return blue / red;
    };
    const double nativeBlueRatioBefore = blueRedRatio(blueStarRgb);
    StarReductionStats blueStarStats;
    check(StarReducer::reduce(
              blueStarRgb, width, height, 55, &blueStarStats),
          "Star reduction should process a native-blue star fixture");
    check(std::abs(blueRedRatio(blueStarRgb) - nativeBlueRatioBefore) < 0.08,
          "A consistently blue star should retain its chromaticity because "
          "its wing is not more saturated than its core");

    // A round subpixel structuring element must not favor horizontal/vertical
    // stars over a diagonal trail. This fixture represents the roughly
    // three-pixel motion seen in a 20 s ultra-wide exposure.
    std::vector<uint16_t> trailedRgb(
        static_cast<size_t>(width) * height * 3, 1600);
    const auto addTrailedStar = [&](double centerX, double centerY,
                                    double angle) {
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double dx = x - centerX;
                const double dy = y - centerY;
                const double major = dx * cosine + dy * sine;
                const double minor = -dx * sine + dy * cosine;
                const double signal = 26000.0 * std::exp(
                    -(major * major / (2.0 * 2.0 * 2.0) +
                      minor * minor / (2.0 * 1.15 * 1.15)));
                const size_t pixel = static_cast<size_t>(y) * width + x;
                for (int channel = 0; channel < 3; ++channel) {
                    trailedRgb[pixel * 3 + channel] =
                        static_cast<uint16_t>(std::clamp(
                            std::lround(trailedRgb[pixel * 3 + channel] +
                                        signal),
                            0L, 65535L));
                }
            }
        }
    };
    addTrailedStar(24.25, 24.25, 0.0);
    addTrailedStar(70.25, 48.25, 0.7853981633974483);
    const auto localStarFlux = [&](const std::vector<uint16_t>& values,
                                   double centerX, double centerY) {
        double flux = 0.0;
        for (int y = std::max(0, static_cast<int>(centerY) - 7);
             y <= std::min(height - 1, static_cast<int>(centerY) + 7); ++y) {
            for (int x = std::max(0, static_cast<int>(centerX) - 7);
                 x <= std::min(width - 1, static_cast<int>(centerX) + 7); ++x) {
                const size_t index =
                    (static_cast<size_t>(y) * width + x) * 3;
                flux += std::max(0, static_cast<int>(values[index]) - 1600);
            }
        }
        return flux;
    };
    const double horizontalFluxBefore =
        localStarFlux(trailedRgb, 24.25, 24.25);
    const double diagonalFluxBefore =
        localStarFlux(trailedRgb, 70.25, 48.25);
    StarReductionStats trailedStats;
    check(StarReducer::reduce(
              trailedRgb, width, height, 70, &trailedStats),
          "Star reduction should process mildly trailed stars");
    const double horizontalRetention =
        localStarFlux(trailedRgb, 24.25, 24.25) / horizontalFluxBefore;
    const double diagonalRetention =
        localStarFlux(trailedRgb, 70.25, 48.25) / diagonalFluxBefore;
    check(trailedStats.processedStars >= 2 &&
              std::abs(horizontalRetention - diagonalRetention) < 0.08,
          "Round subpixel erosion should have little orientation bias "
          "(horizontal=" + std::to_string(horizontalRetention) +
          ", diagonal=" + std::to_string(diagonalRetention) + ")");

    std::vector<uint16_t> saturatedLargeStar(
        static_cast<size_t>(width) * height * 3, 1500);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x - 48.0;
            const double dy = y - 36.0;
            const double signal = 90000.0 *
                std::exp(-(dx * dx + dy * dy) / (2.0 * 4.0 * 4.0));
            const uint16_t value = static_cast<uint16_t>(
                std::clamp(std::lround(1500.0 + signal), 0L, 65535L));
            const size_t pixel = static_cast<size_t>(y) * width + x;
            saturatedLargeStar[pixel * 3] = value;
            saturatedLargeStar[pixel * 3 + 1] = value;
            saturatedLargeStar[pixel * 3 + 2] = value;
        }
    }
    const std::vector<uint16_t> saturatedLargeStarInput = saturatedLargeStar;
    StarReductionStats saturatedStats;
    check(StarReducer::reduce(
              saturatedLargeStar, width, height, 70, &saturatedStats),
          "Star reduction should safely inspect a saturated large star");
    check(saturatedStats.defringedPixels == 0,
          "Automatic defringing should skip saturated large stars");
    check(saturatedLargeStar == saturatedLargeStarInput,
          "Automatic reduction should preserve saturated large stars instead "
          "of carving clipped halos into rings");

    std::vector<uint16_t> overlapLuminance(width * height, 1200);
    addGaussianStar(overlapLuminance, width, height,
                    46.0, 36.0, 1.8, 30000.0);
    addGaussianStar(overlapLuminance, width, height,
                    50.0, 36.0, 1.6, 24000.0);
    std::vector<uint16_t> overlapRgb(overlapLuminance.size() * 3);
    for (size_t pixel = 0; pixel < overlapLuminance.size(); ++pixel) {
        overlapRgb[pixel * 3] = overlapLuminance[pixel];
        overlapRgb[pixel * 3 + 1] = overlapLuminance[pixel];
        overlapRgb[pixel * 3 + 2] = overlapLuminance[pixel];
    }
    std::vector<uint16_t> repeated = overlapRgb;
    check(StarReducer::reduce(overlapRgb, width, height, 80) &&
              StarReducer::reduce(repeated, width, height, 80),
          "Overlapping-star reduction should complete");
    check(overlapRgb == repeated,
          "Overlapping-star reduction should be deterministic");
    check(std::all_of(overlapRgb.begin(), overlapRgb.end(),
                      [](uint16_t value) { return value >= 1200; }),
          "Overlapping star masks should not create dark holes");

    std::vector<uint16_t> maskedRgb = rgbInput;
    const std::vector<uint16_t> maskedInput = maskedRgb;
    std::vector<uint8_t> skyOnlyMask(width * height, 0);
    std::fill(skyOnlyMask.begin(),
              skyOnlyMask.begin() + static_cast<size_t>(width) * height / 2,
              255);
    check(StarReducer::reduce(maskedRgb, width, height, 90, nullptr,
                              &skyOnlyMask),
          "Mask-aware star reduction should process a matching sky mask");
    const size_t groundBegin =
        static_cast<size_t>(width) * (height / 2) * 3;
    check(std::equal(maskedRgb.begin() + groundBegin, maskedRgb.end(),
                     maskedInput.begin() + groundBegin),
          "Mask-aware star reduction should leave all ground pixels bit-exact");
    const std::vector<uint8_t> wrongSizeMask(3);
    check(!StarReducer::reduce(maskedRgb, width, height, 90, nullptr,
                               &wrongSizeMask),
          "Mask-aware star reduction should reject a mismatched mask");
}

void testNoiseReduction() {
    constexpr int width = 96;
    constexpr int height = 64;
    constexpr int splitX = width / 2;
    std::vector<uint16_t> noisy(static_cast<size_t>(width) * height * 3);
    std::mt19937 random(12345);
    std::normal_distribution<double> luminanceNoise(0.0, 650.0);
    std::normal_distribution<double> colorNoise(0.0, 900.0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int base = x < splitX ? 9000 : 29000;
            const double common = luminanceNoise(random);
            const size_t pixel = static_cast<size_t>(y) * width + x;
            noisy[pixel * 3] = static_cast<uint16_t>(std::clamp(
                base + common + colorNoise(random), 0.0, 65535.0));
            noisy[pixel * 3 + 1] = static_cast<uint16_t>(std::clamp(
                base + common, 0.0, 65535.0));
            noisy[pixel * 3 + 2] = static_cast<uint16_t>(std::clamp(
                base + common + colorNoise(random), 0.0, 65535.0));
        }
    }

    const auto variance = [&](const std::vector<uint16_t>& image,
                              bool chroma) {
        std::vector<double> samples;
        for (int y = 8; y < height - 8; ++y) {
            for (int x = 8; x < splitX - 8; ++x) {
                const size_t base =
                    (static_cast<size_t>(y) * width + x) * 3;
                samples.push_back(chroma
                    ? static_cast<double>(image[base]) - image[base + 1]
                    : (image[base] * 0.2126 +
                       image[base + 1] * 0.7152 +
                       image[base + 2] * 0.0722));
            }
        }
        double mean = 0.0;
        for (double sample : samples) mean += sample;
        mean /= samples.size();
        double squared = 0.0;
        for (double sample : samples) {
            const double delta = sample - mean;
            squared += delta * delta;
        }
        return squared / samples.size();
    };
    const auto regionLuminance = [&](const std::vector<uint16_t>& image,
                                     int x0, int x1) {
        double sum = 0.0;
        size_t count = 0;
        for (int y = 8; y < height - 8; ++y) {
            for (int x = x0; x < x1; ++x) {
                const size_t base =
                    (static_cast<size_t>(y) * width + x) * 3;
                sum += image[base] * 0.2126 +
                    image[base + 1] * 0.7152 +
                    image[base + 2] * 0.0722;
                ++count;
            }
        }
        return sum / count;
    };

    std::vector<uint16_t> denoised;
    check(NoiseReductionEngine::denoiseRgb(
              noisy, width, height, 55, denoised),
          "Multiscale RGB denoise should accept a valid image");
    check(variance(denoised, false) < variance(noisy, false) * 0.65,
          "Denoise should substantially reduce flat-field luminance variance");
    check(variance(denoised, true) < variance(noisy, true) * 0.45,
          "Denoise should suppress chroma speckle more strongly than luminance");

    const double inputContrast =
        regionLuminance(noisy, splitX + 8, width - 8) -
        regionLuminance(noisy, 8, splitX - 8);
    const double outputContrast =
        regionLuminance(denoised, splitX + 8, width - 8) -
        regionLuminance(denoised, 8, splitX - 8);
    check(outputContrast > inputContrast * 0.98,
          "Denoise should preserve broad edge contrast");
    check(std::abs(regionLuminance(denoised, 8, splitX - 8) -
                   regionLuminance(noisy, 8, splitX - 8)) < 20.0,
          "Denoise should preserve the mean level of a flat field");

    std::vector<uint16_t> hydrogenAlpha(
        static_cast<size_t>(width) * height * 3);
    std::mt19937 haRandom(67890);
    std::normal_distribution<double> haNoise(0.0, 300.0);
    const double centerX = width / 2.0;
    const double centerY = height / 2.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = x - centerX;
            const double dy = y - centerY;
            const double redNebula =
                3500.0 * std::exp(-(dx * dx + dy * dy) / (2.0 * 7.0 * 7.0));
            const size_t pixel = static_cast<size_t>(y) * width + x;
            hydrogenAlpha[pixel * 3] = static_cast<uint16_t>(std::clamp(
                7000.0 + redNebula + haNoise(haRandom), 0.0, 65535.0));
            hydrogenAlpha[pixel * 3 + 1] = static_cast<uint16_t>(std::clamp(
                6500.0 + haNoise(haRandom), 0.0, 65535.0));
            hydrogenAlpha[pixel * 3 + 2] = static_cast<uint16_t>(std::clamp(
                6200.0 + haNoise(haRandom), 0.0, 65535.0));
        }
    }
    const auto redNebulaContrast = [&](const std::vector<uint16_t>& image) {
        double core = 0.0;
        double background = 0.0;
        size_t coreCount = 0;
        size_t backgroundCount = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double distance =
                    std::hypot(x - centerX, y - centerY);
                const size_t base =
                    (static_cast<size_t>(y) * width + x) * 3;
                const double redExcess =
                    static_cast<double>(image[base]) - image[base + 1];
                if (distance <= 4.0) {
                    core += redExcess;
                    ++coreCount;
                } else if (distance >= 18.0 && distance <= 24.0) {
                    background += redExcess;
                    ++backgroundCount;
                }
            }
        }
        return core / coreCount - background / backgroundCount;
    };
    std::vector<uint16_t> denoisedHa;
    check(NoiseReductionEngine::denoiseRgb(
              hydrogenAlpha, width, height, 55, denoisedHa),
          "Denoise should process a red nebular structure");
    check(redNebulaContrast(denoisedHa) >=
              redNebulaContrast(hydrogenAlpha) * 0.85,
          "Denoise should preserve BCF-modified-camera H-alpha color contrast");

    std::vector<uint16_t> noOp;
    check(NoiseReductionEngine::denoiseRgb(
              noisy, width, height, 0, noOp) && noOp == noisy,
          "Zero denoise strength should be an exact no-op");
    std::vector<uint16_t> untouched = {1, 2, 3};
    check(!NoiseReductionEngine::denoiseRgb(
              noisy, width + 1, height, 50, untouched) &&
              untouched == std::vector<uint16_t>({1, 2, 3}),
          "Denoise should reject mismatched dimensions without changing output");
}

void testPhotometricNormalization() {
    constexpr int width = 160;
    constexpr int height = 96;
    constexpr double expectedGain = 1.08;
    constexpr std::array<double, 3> expectedOffsets = {350.0, -180.0, 90.0};
    const size_t pixelCount = static_cast<size_t>(width) * height;

    std::vector<uint16_t> reference(pixelCount * 3);
    std::vector<uint16_t> source(pixelCount * 3);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t base = (static_cast<size_t>(y) * width + x) * 3;
            const double texture = 8500.0 + x * 105.0 + y * 73.0 +
                1200.0 * std::sin(x * 0.17) * std::cos(y * 0.11);
            for (int channel = 0; channel < 3; ++channel) {
                const double channelValue = texture + channel * 1700.0 +
                    420.0 * std::sin((x + channel * 7) * 0.09);
                reference[base + channel] = static_cast<uint16_t>(
                    std::clamp(std::lround(channelValue), 0L, 65535L));
                source[base + channel] = static_cast<uint16_t>(std::clamp(
                    std::lround((channelValue - expectedOffsets[channel]) /
                                expectedGain),
                    0L, 65535L));
            }
        }
    }

    // Simulate a moving bright foreground/cloud that must not drive the fit.
    for (int y = 20; y < 58; ++y) {
        for (int x = 25; x < 105; ++x) {
            const size_t base = (static_cast<size_t>(y) * width + x) * 3;
            source[base] = 53000;
            source[base + 1] = 47000;
            source[base + 2] = 43000;
        }
    }
    source[0] = source[1] = source[2] = 0;

    PhotometricReferenceProfile profile;
    check(PhotometricNormalizer::buildReferenceProfile(
              reference, width, height, profile, 4096),
          "Photometric reference profile should build from valid RGB data");
    std::vector<uint8_t> skyMask(pixelCount, 0);
    std::fill(skyMask.begin(),
              skyMask.begin() + static_cast<size_t>(width) * (height / 2),
              255);
    PhotometricReferenceProfile maskedProfile;
    check(PhotometricNormalizer::buildReferenceProfile(
              reference, width, height, maskedProfile, 4096,
              &skyMask, 160) &&
              std::all_of(maskedProfile.samples.begin(),
                          maskedProfile.samples.end(),
                          [width](const auto& sample) {
                              return sample.pixelIndex <
                                  static_cast<size_t>(width) * (height / 2);
                          }),
          "Photometric profiles should honor a sky-only inclusion mask");
    PhotometricModel model;
    check(PhotometricNormalizer::estimate(profile, source, model),
          "Photometric model should tolerate a large local outlier region");
    check(std::abs(model.gain - expectedGain) < 0.02,
          "Photometric model should recover the exposure gain");
    for (int channel = 0; channel < 3; ++channel) {
        check(std::abs(model.offsets[channel] - expectedOffsets[channel]) < 90.0,
              "Photometric model should recover per-channel offsets");
    }

    const std::vector<uint16_t> before = source;
    check(PhotometricNormalizer::applyInPlace(source, width, height, model),
          "Photometric model should apply to valid RGB data");
    check(source[0] == 0 && source[1] == 0 && source[2] == 0,
          "Photometric normalization should preserve zero-valued warp borders");

    double beforeError = 0.0;
    double afterError = 0.0;
    size_t comparedValues = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x >= 25 && x < 105 && y >= 20 && y < 58) continue;
            const size_t base = (static_cast<size_t>(y) * width + x) * 3;
            if (base == 0) continue;
            for (int channel = 0; channel < 3; ++channel) {
                beforeError += std::abs(static_cast<double>(before[base + channel]) -
                                        reference[base + channel]);
                afterError += std::abs(static_cast<double>(source[base + channel]) -
                                       reference[base + channel]);
                ++comparedValues;
            }
        }
    }
    check(comparedValues > 0 && afterError < beforeError * 0.03,
          "Photometric normalization should substantially reduce frame mismatch");

    std::vector<uint16_t> invalid = {1, 2, 3};
    check(!PhotometricNormalizer::applyInPlace(
              invalid, width, height, model) &&
              invalid == std::vector<uint16_t>({1, 2, 3}),
          "Photometric normalization should reject mismatched dimensions safely");
}

std::vector<uint16_t> qualityTestFrame(int width, int height,
                                       double starSigma, int starStride,
                                       unsigned seed) {
    std::mt19937 generator(seed);
    std::normal_distribution<double> noise(0.0, 45.0);
    std::vector<uint16_t> image(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t index = static_cast<size_t>(y) * width + x;
            // A strong smooth gradient represents horizon glow. Quality
            // detection must remove it locally before estimating stars.
            image[index] = static_cast<uint16_t>(std::clamp(
                std::lround(2600.0 + x * 70.0 + y * 50.0 +
                            noise(generator)),
                0L, 65535L));
        }
    }
    for (int y = 18; y < height - 18; y += starStride) {
        for (int x = 18; x < width - 18; x += starStride) {
            addGaussianStar(image, width, height,
                            x + (y % 7) * 0.08,
                            y + (x % 5) * 0.07,
                            starSigma, 18000.0);
        }
    }
    return image;
}

void testFrameQualitySelection() {
    constexpr int width = 256;
    constexpr int height = 160;
    const std::vector<std::vector<uint16_t>> frames = {
        qualityTestFrame(width, height, 1.35, 25, 1),
        qualityTestFrame(width, height, 1.10, 25, 2),
        qualityTestFrame(width, height, 1.40, 25, 3),
        qualityTestFrame(width, height, 5.50, 60, 4),
        qualityTestFrame(width, height, 1.30, 25, 5)
    };
    std::vector<FrameQualityMetrics> metrics(frames.size());
    for (size_t index = 0; index < frames.size(); ++index) {
        check(FrameQualityEvaluator::evaluate(
                  frames[index], width, height, metrics[index]),
              "Frame quality evaluator should accept a synthetic star field");
    }
    check(metrics[1].medianFwhm < metrics[3].medianFwhm,
          "Frame quality metrics should distinguish sharp and defocused stars");
    check(metrics[1].usableStars > metrics[3].usableStars,
          "Frame quality metrics should detect star loss in a poor frame");
    check(FrameQualityEvaluator::medianValidEllipticity(metrics) > 0.0,
          "Frame quality summary should expose the valid sequence star shape");

    FrameQualitySelection selection;
    check(FrameQualityEvaluator::selectSequence(
              metrics, 2, true, selection),
          "Frame quality selector should rank a valid sequence");
    check(selection.referenceIndex == 1,
          "Frame quality selector should prefer the sharpest well-populated frame");
    check(selection.rejected.size() == frames.size() && selection.rejected[3],
          "Frame quality selector should reject a severe defocus outlier");
    check(std::count(selection.rejected.begin(), selection.rejected.end(), false) >= 2,
          "Frame quality rejection should always retain a stackable sequence");

    FrameQualityMetrics invalid;
    check(!FrameQualityEvaluator::evaluate(
              std::vector<uint16_t>({1, 2, 3}), width, height, invalid) &&
              !invalid.valid,
          "Frame quality evaluator should reject mismatched dimensions");
}

void testTiffIccProfile() {
    QTemporaryDir directory;
    check(directory.isValid(), "Temporary export directory should be available");
    if (!directory.isValid()) return;

    const QString path = directory.filePath("linear-srgb.tiff");
    const std::vector<uint16_t> rgb = {
        0, 1000, 2000, 10000, 20000, 30000,
        40000, 50000, 60000, 65535, 32768, 16384
    };
    check(ImageExporter::exportRgb16(rgb, 2, 2, path,
                                     ImageExporter::Tiff16),
          "RGB TIFF export should succeed");

    TIFF* tiff = TIFFOpen(path.toStdString().c_str(), "r");
    check(tiff != nullptr, "Exported TIFF should be readable");
    if (!tiff) return;

    uint32_t profileSize = 0;
    void* profileData = nullptr;
    const bool hasProfile = TIFFGetField(tiff, TIFFTAG_ICCPROFILE,
                                         &profileSize, &profileData) == 1;
    check(hasProfile && profileSize > 0 && profileData,
          "RGB TIFF should contain an ICC profile");
    if (hasProfile && profileSize > 0 && profileData) {
        const QByteArray bytes(static_cast<const char*>(profileData),
                               static_cast<int>(profileSize));
        check(QColorSpace::fromIccProfile(bytes).isValid(),
              "Embedded TIFF ICC profile should be parseable");
    }
    TIFFClose(tiff);

    const QString unicodePath = directory.filePath(
        QString::fromUtf8("中文输出-银河.tiff"));
    check(ImageExporter::exportRgb16(rgb, 2, 2, unicodePath,
                                     ImageExporter::Tiff16) &&
              QFileInfo::exists(unicodePath),
          "RGB TIFF export should support a Unicode file path");
}

void testPngTransferAndColorSpace() {
    QTemporaryDir directory;
    check(directory.isValid(), "Temporary PNG directory should be available");
    if (!directory.isValid()) return;

    const QString rgbPath = directory.filePath(
        QString::fromUtf8("中文输出-预览.png"));
    const std::vector<uint16_t> rgb = {
        0, 65535, 32768,
        203, 204, 205
    };
    check(ImageExporter::exportRgb16(rgb, 2, 1, rgbPath,
                                     ImageExporter::Png8),
          "RGB PNG export should succeed on a Unicode path");
    const QImage loaded(rgbPath);
    check(!loaded.isNull() && loaded.width() == 2 && loaded.height() == 1,
          "Exported RGB PNG should be readable at the expected size");
    if (!loaded.isNull()) {
        const QColor first = loaded.pixelColor(0, 0);
        check(first.red() == 0 && first.green() == 255 &&
                  std::abs(first.blue() - 188) <= 1,
              "PNG export should apply the sRGB transfer function");
        check(loaded.colorSpace().isValid() &&
                  loaded.colorSpace() == QColorSpace(QColorSpace::SRgb),
              "RGB PNG should be tagged as sRGB");
    }

    const QString grayPath = directory.filePath("gray.png");
    const std::vector<uint16_t> gray = {0, 65535, 32768};
    check(ImageExporter::export16Bit(gray, 3, 1, grayPath,
                                     ImageExporter::Png8),
          "Grayscale PNG export should succeed");
    const QImage grayLoaded(grayPath);
    check(!grayLoaded.isNull() && grayLoaded.pixelColor(0, 0).red() == 0 &&
              grayLoaded.pixelColor(1, 0).red() == 255 &&
              std::abs(grayLoaded.pixelColor(2, 0).red() - 188) <= 1,
          "Grayscale PNG should use the same sRGB transfer function");
}

void testCancellableExport() {
    QTemporaryDir directory;
    check(directory.isValid(),
          "Temporary cancellable-export directory should be available");
    if (!directory.isValid()) return;

    const std::vector<uint16_t> rgb(64 * 64 * 3, 1000);
    const QString tiffPath = directory.filePath("cancelled.tiff");
    check(!ImageExporter::exportRgb16(
              rgb, 64, 64, tiffPath, ImageExporter::Tiff16,
              []() { return true; }) && !QFileInfo::exists(tiffPath),
          "Cancelled TIFF export should remove its incomplete file");
    const QString pngPath = directory.filePath("cancelled.png");
    check(!ImageExporter::exportRgb16(
              rgb, 64, 64, pngPath, ImageExporter::Png8,
              []() { return true; }) && !QFileInfo::exists(pngPath),
          "Cancelled PNG conversion should not create an output file");
}

void testPresetDenoisePersistence() {
    const QList<Preset> builtins = PresetManager::builtinPresets();
    check(builtins.size() == 2 &&
              builtins[0].noiseReductionEnabled &&
              builtins[0].noiseReductionStrength == 30 &&
              builtins[1].noiseReductionEnabled &&
              builtins[1].noiseReductionStrength == 35,
          "Built-in presets should expose conservative denoise defaults");

    QTemporaryDir directory;
    check(directory.isValid(),
          "Temporary preset directory should be available");
    if (!directory.isValid()) return;
    Preset original;
    original.name = "Denoise round trip";
    original.autoRejectLowQualityFrames = false;
    original.photometricNormalizationEnabled = false;
    original.noiseReductionEnabled = true;
    original.noiseReductionStrength = 42;
    original.basicAdjustments.temperature = 18;
    original.basicAdjustments.exposureTenths = 7;
    original.basicAdjustments.highlights = -24;
    original.basicAdjustments.vibrance = 16;
    original.basicAdjustments.sharpening = 35;
    const QString path = directory.filePath("preset.json");
    PresetManager::savePreset(original, path);
    const Preset loaded = PresetManager::loadPreset(path);
    check(loaded.name == original.name &&
              !loaded.autoRejectLowQualityFrames &&
              !loaded.photometricNormalizationEnabled &&
              loaded.noiseReductionEnabled &&
              loaded.noiseReductionStrength == 42 &&
              loaded.basicAdjustments.temperature == 18 &&
              loaded.basicAdjustments.exposureTenths == 7 &&
              loaded.basicAdjustments.highlights == -24 &&
              loaded.basicAdjustments.vibrance == 16 &&
              loaded.basicAdjustments.sharpening == 35,
          "JSON presets should persist denoise and basic adjustment settings");

    const QString legacyPath = directory.filePath("legacy.json");
    QFile legacyFile(legacyPath);
    check(legacyFile.open(QIODevice::WriteOnly) &&
              legacyFile.write("{\"name\":\"Legacy\"}") > 0,
          "Legacy preset fixture should be writable");
    legacyFile.close();
    const Preset legacy = PresetManager::loadPreset(legacyPath);
    check(legacy.autoRejectLowQualityFrames &&
              legacy.photometricNormalizationEnabled &&
              !legacy.noiseReductionEnabled &&
              legacy.noiseReductionStrength == 30 &&
              legacy.basicAdjustments.isNeutral() &&
              legacy.kappaValue == 2.5,
          "Legacy presets without newer fields should use safe defaults");

    const Preset missing =
        PresetManager::loadPreset(directory.filePath("missing.json"));
    check(!missing.noiseReductionEnabled &&
              missing.noiseReductionStrength == 30 &&
              missing.basicAdjustments.isNeutral(),
          "Missing preset files should return initialized defaults");
}

void testRawApiValidation() {
    RawImageLoader loader;
    RawImageLoader::Metadata metadata;
    RawImageLoader::PreviewData preview;
    check(!loader.loadMetadata(QStringLiteral("/path/that/does/not/exist.raw"), metadata),
          "Metadata API should report missing files");
    check(!loader.loadPreview(QStringLiteral("/path/that/does/not/exist.raw"), 120, preview),
          "Preview API should report missing files");
    check(!loader.loadPreview(QStringLiteral("unused.raw"), 0, preview),
          "Preview API should reject a non-positive requested size before I/O");
}

void testSkyGroundHorizonDetection() {
    const auto runSyntheticScene = [](int width, int height, double baseRatio,
                                      const std::string& label) {
        std::vector<uint16_t> image(static_cast<size_t>(width) * height);
        std::vector<int> expected(width);
        for (int x = 0; x < width; ++x) {
            const double phase = x * 2.0 * 3.14159265358979323846 / width;
            const int ridge = static_cast<int>(std::round(
                height * baseRatio + height * 0.045 * std::sin(phase) +
                height * 0.025 * std::sin(phase * 3.0)));
            expected[x] = ridge;
            for (int y = 0; y < height; ++y) {
                const size_t index = static_cast<size_t>(y) * width + x;
                if (y < ridge) {
                    image[index] = static_cast<uint16_t>(
                        17000 + y * 7 + ((x * 13 + y * 3) % 31));
                } else {
                    image[index] = static_cast<uint16_t>(
                        5500 + ((x * 97 + y * 53) % 3500));
                }
            }
        }
        for (int x = 12; x < width; x += 37) {
            const int y = 10 + (x * 17) % std::max(12, height / 3);
            if (y < expected[x]) {
                image[static_cast<size_t>(y) * width + x] = 52000;
            }
        }

        std::vector<uint8_t> mask;
        check(SkyGroundMask::autoDetect(image, width, height, mask, 0),
              label + " horizon should be detected");
        if (mask.size() != image.size()) {
            check(false, label + " mask should preserve image dimensions");
            return;
        }

        double absoluteError = 0.0;
        for (int x = 0; x < width; ++x) {
            int boundary = 0;
            while (boundary < height &&
                   mask[static_cast<size_t>(boundary) * width + x] >= 128) {
                ++boundary;
            }
            absoluteError += std::abs(boundary - expected[x]);
        }
        const double meanAbsoluteError = absoluteError / width;
        check(meanAbsoluteError <= std::max(3.0, height * 0.025),
              label + " detected horizon should follow the synthetic ridge");
    };

    runSyntheticScene(320, 200, 0.64, "Landscape");
    runSyntheticScene(180, 320, 0.68, "Portrait");

    const int width = 160;
    const int height = 120;
    const std::vector<uint16_t> flat(static_cast<size_t>(width) * height, 12000);
    std::vector<uint8_t> mask;
    check(!SkyGroundMask::autoDetect(flat, width, height, mask, 0),
          "A flat frame without a credible horizon should be rejected");

    QImage preview(160, 100, QImage::Format_RGB888);
    for (int y = 0; y < preview.height(); ++y) {
        uchar* row = preview.scanLine(y);
        for (int x = 0; x < preview.width(); ++x) {
            const uchar value = y < 64 ? 125 :
                static_cast<uchar>(25 + (x * 7 + y * 3) % 25);
            row[x * 3] = value;
            row[x * 3 + 1] = value;
            row[x * 3 + 2] = value;
        }
    }
    check(SkyGroundMask::autoDetectPreview(preview, 320, 200, mask, 8) &&
              mask.size() == 320U * 200U,
          "Preview horizon detection should scale its mask to processing dimensions");
    check(!SkyGroundMask::autoDetectPreview(preview, 200, 200, mask, 0),
          "Preview detection should reject a mismatched target aspect ratio");

    QTemporaryDir directory;
    check(directory.isValid(), "Temporary mask directory should be available");
    if (directory.isValid()) {
        QImage alphaMask(2, 1, QImage::Format_RGBA8888);
        alphaMask.setPixelColor(0, 0, QColor(255, 255, 255, 0));
        alphaMask.setPixelColor(1, 0, QColor(255, 255, 255, 255));
        const QString path = directory.filePath("alpha-mask.png");
        check(alphaMask.save(path), "Alpha mask fixture should be writable");
        check(SkyGroundMask::loadUserMask(path, 2, 1, mask, 0) &&
                  mask == std::vector<uint8_t>({0, 255}),
              "Transparent user-mask pixels should mean ground, not sky");
    }
}

void testUpdateManifest() {
    const QByteArray manifestJson = R"JSON({
        "schemaVersion": 1,
        "version": "0.9.0",
        "publishedAt": "2026-08-11T10:30:00Z",
        "releaseNotes": "安全更新检查与安装包校验",
        "platforms": {
            "windows-x64": {
                "fileName": "StarProcessor-Windows-x64-v0.9.0.zip",
                "url": "https://di.nexusgen.net/starprocessor/downloads/StarProcessor-Windows-x64-v0.9.0.zip",
                "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
                "size": 12345678
            }
        }
    })JSON";

    UpdateManifest manifest;
    QString error;
    check(UpdateManifestParser::parse(manifestJson, "windows-x64",
                                      manifest, error),
          "A valid update manifest should parse");
    check(manifest.version == "0.9.0" &&
              manifest.package.size == 12345678 &&
              manifest.package.fileName ==
                  "StarProcessor-Windows-x64-v0.9.0.zip",
          "The update manifest should retain trusted package metadata");
    check(UpdateManifestParser::isNewerVersion("0.9.0", "0.8.1"),
          "A greater semantic version should be considered newer");
    check(!UpdateManifestParser::isNewerVersion("0.9.0-beta.1", "0.9.0"),
          "A prerelease should not replace its final release");
    check(UpdateManifestParser::compareVersions("1.0.0-beta.2",
                                                "1.0.0-beta.11") < 0,
          "Numeric prerelease identifiers should compare numerically");
    check(UpdateManifestParser::compareVersions("1.0.0-01", "1.0.0") == 0,
          "A numeric prerelease identifier with a leading zero should be invalid");

    QByteArray unsafeManifest = manifestJson;
    unsafeManifest.replace(
        "https://di.nexusgen.net/starprocessor/downloads/StarProcessor-Windows-x64-v0.9.0.zip",
        "http://di.nexusgen.net/starprocessor/downloads/StarProcessor-Windows-x64-v0.9.0.zip");
    check(!UpdateManifestParser::parse(unsafeManifest, "windows-x64",
                                       manifest, error),
          "The update manifest should reject a non-HTTPS package URL");
    QByteArray mismatchedVersion = manifestJson;
    mismatchedVersion.replace("v0.9.0.zip", "v0.8.1.zip");
    check(!UpdateManifestParser::parse(mismatchedVersion, "windows-x64",
                                       manifest, error),
          "A package filename should identify the advertised version");
    check(!UpdateManifestParser::parse(manifestJson, "linux-x64",
                                       manifest, error),
          "The update manifest should reject an unavailable platform");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testMinimumFilter();
    testStacking();
    testRawCalibration();
    testDeepSkyCalibrationPreflight();
    testTimelapseDenoise();
    testTemporalPhotometricSmoothing();
    testImageBufferUtils();
    testRgbAutoOptimize();
    testRgbTransform();
    testMemoryEstimator();
    testPreviewToneMapper();
    testTransformDirection();
    testAlignmentEstimation();
    testStarDetectionAndReduction();
    testNoiseReduction();
    testPhotometricNormalization();
    testFrameQualitySelection();
    testTiffIccProfile();
    testPngTransferAndColorSpace();
    testCancellableExport();
    testPresetDenoisePersistence();
    testRawApiValidation();
    testSkyGroundHorizonDetection();
    testUpdateManifest();

    if (failures == 0) {
        std::cout << "All core tests passed.\n";
        return 0;
    }
    std::cerr << failures << " core test(s) failed.\n";
    return 1;
}
