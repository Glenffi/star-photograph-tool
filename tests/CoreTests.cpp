#include "core/ImageAligner.h"
#include "core/ImageBufferUtils.h"
#include "core/ImageExporter.h"
#include "core/RawImageLoader.h"
#include "core/StackingEngine.h"
#include "core/StarDetector.h"
#include "core/StarReducer.h"

#include <QByteArray>
#include <QColorSpace>
#include <QCoreApplication>
#include <QTemporaryDir>

#include <tiffio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
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
}

void testStarDetectionAndReduction() {
    constexpr int width = 96;
    constexpr int height = 72;
    std::vector<uint16_t> luminance(width * height, 1000);
    addGaussianStar(luminance, width, height, 18.0, 16.0, 1.4, 42000.0);
    addGaussianStar(luminance, width, height, 70.0, 20.0, 1.7, 36000.0);
    addGaussianStar(luminance, width, height, 48.0, 54.0, 1.5, 40000.0);

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
        rgb[i * 3] = luminance[i];
        rgb[i * 3 + 1] = luminance[i];
        rgb[i * 3 + 2] = luminance[i];
    }
    const uint16_t peakBefore = rgb[(16 * width + 18) * 3];
    const uint16_t backgroundBefore = rgb[(65 * width + 5) * 3];
    check(StarReducer::reduce(rgb, width, height, 70),
          "Star reduction should accept a valid RGB buffer");
    check(rgb[(16 * width + 18) * 3] < peakBefore,
          "Star reduction should lower a detected star peak");
    check(rgb[(65 * width + 5) * 3] == backgroundBefore,
          "Star reduction should preserve pixels outside star masks");
    check(!StarReducer::reduce(rgb, width + 1, height, 50),
          "Star reduction should reject mismatched dimensions");
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
    testTransformDirection();
    testStarDetectionAndReduction();
    testTiffIccProfile();
    testRawApiValidation();

    if (failures == 0) {
        std::cout << "All core tests passed.\n";
        return 0;
    }
    std::cerr << failures << " core test(s) failed.\n";
    return 1;
}
