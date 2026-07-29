#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/AutoOptimizeEngine.h"
#include "core/NoiseReductionEngine.h"
#include "core/PresetManager.h"
#include "core/RawImageLoader.h"
#include "core/ProcessingMemoryEstimator.h"
#include "core/PreviewToneMapper.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarReducer.h"

#include <QByteArray>
#include <QColorSpace>
#include <QCoreApplication>
#include <QFile>
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
    check(!engine.stackWithMask(frames, {originals.front()}, 2, 2,
                                StackingEngine::Average, 2.5, mask, result),
          "Mask stacking should reject mismatched frame counts");

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
          "Linked RGB stretch should preserve strong star color ordering");

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
              6000, 4000, dehazeOptions) > baseEstimate,
          "Dehaze should increase the estimated peak for full-resolution float planes");
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
    std::vector<uint16_t> zeroStrength = rgbInput;
    check(StarReducer::reduce(reduced40, width, height, 40) &&
              StarReducer::reduce(reduced70, width, height, 70),
          "Intermediate star-reduction strengths should process successfully");
    check(reduced40[faintCenterIndex] >= reduced70[faintCenterIndex] &&
              reduced70[faintCenterIndex] >= rgb[faintCenterIndex],
          "Increasing star-reduction strength should monotonically suppress faint stars");
    check(StarReducer::reduce(zeroStrength, width, height, 0) &&
              zeroStrength == rgbInput,
          "Zero star-reduction strength should be an exact no-op");

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

void testTiffIccProfile() {
    QTemporaryDir directory;
    check(directory.isValid(), "Temporary export directory should be available");
    if (!directory.isValid()) return;

    const QString path = directory.filePath("linear-srgb.tiff");
    const std::vector<uint16_t> rgb = {
        0, 1000, 2000, 10000, 20000, 30000,
        40000, 50000, 60000, 65535, 32768, 16384
    };
    check(ImageExporter::exportRgb16(rgb, 2, 2, path.toStdString(),
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
    original.noiseReductionEnabled = true;
    original.noiseReductionStrength = 42;
    const QString path = directory.filePath("preset.json");
    PresetManager::savePreset(original, path);
    const Preset loaded = PresetManager::loadPreset(path);
    check(loaded.name == original.name &&
              loaded.noiseReductionEnabled &&
              loaded.noiseReductionStrength == 42,
          "JSON presets should persist denoise settings");

    const QString legacyPath = directory.filePath("legacy.json");
    QFile legacyFile(legacyPath);
    check(legacyFile.open(QIODevice::WriteOnly) &&
              legacyFile.write("{\"name\":\"Legacy\"}") > 0,
          "Legacy preset fixture should be writable");
    legacyFile.close();
    const Preset legacy = PresetManager::loadPreset(legacyPath);
    check(!legacy.noiseReductionEnabled &&
              legacy.noiseReductionStrength == 30 &&
              legacy.kappaValue == 2.5,
          "Legacy presets without denoise fields should use defaults");

    const Preset missing =
        PresetManager::loadPreset(directory.filePath("missing.json"));
    check(!missing.noiseReductionEnabled &&
              missing.noiseReductionStrength == 30,
          "Missing preset files should return initialized defaults");
}

void testRawApiValidation() {
    RawImageLoader loader;
    RawImageLoader::Metadata metadata;
    RawImageLoader::PreviewData preview;
    check(!loader.loadMetadata("/path/that/does/not/exist.raw", metadata),
          "Metadata API should report missing files");
    check(!loader.loadPreview("/path/that/does/not/exist.raw", 120, preview),
          "Preview API should report missing files");
    check(!loader.loadPreview("unused.raw", 0, preview),
          "Preview API should reject a non-positive requested size before I/O");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    testStacking();
    testImageBufferUtils();
    testRgbAutoOptimize();
    testRgbTransform();
    testMemoryEstimator();
    testPreviewToneMapper();
    testTransformDirection();
    testAlignmentEstimation();
    testStarDetectionAndReduction();
    testNoiseReduction();
    testTiffIccProfile();
    testPresetDenoisePersistence();
    testRawApiValidation();

    if (failures == 0) {
        std::cout << "All core tests passed.\n";
        return 0;
    }
    std::cerr << failures << " core test(s) failed.\n";
    return 1;
}
