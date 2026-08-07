#include "core/FinishingPipeline.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
}

std::vector<uint16_t> sampleRgb() {
    return {
        100, 200, 300, 400, 500, 600,
        700, 800, 900, 1000, 1100, 1200
    };
}

void testNoOptionsAreBitExact() {
    std::vector<uint16_t> rgb = sampleRgb();
    const std::vector<uint16_t> original = rgb;
    FinishingOptions options;
    FinishingResult result;
    result.cancelled = true;
    result.error = "stale";
    int reportedStages = 0;

    const bool ok = FinishingPipeline::process(
        rgb, 2, 2, nullptr, options, result,
        [&](FinishingStage) { ++reportedStages; });

    expect(ok, "a valid no-op pipeline should succeed");
    expect(rgb == original, "a no-op pipeline must be bit-exact");
    expect(!result.cancelled, "result state must be reset between calls");
    expect(result.error.empty(), "a successful no-op must not report an error");
    expect(reportedStages == 0, "disabled stages must not be reported");
}

void testInvalidRgbIsRejected() {
    std::vector<uint16_t> rgb = sampleRgb();
    rgb.pop_back();
    const std::vector<uint16_t> original = rgb;
    FinishingResult result;

    const bool ok = FinishingPipeline::process(
        rgb, 2, 2, nullptr, FinishingOptions{}, result);

    expect(!ok, "an RGB buffer with the wrong length must fail");
    expect(!result.cancelled, "validation failure is not cancellation");
    expect(!result.error.empty(), "invalid RGB must provide an error");
    expect(rgb == original, "validation failure must not mutate RGB");
}

void testSkyMaskIsRequiredAndSizedExactly() {
    FinishingOptions options;
    options.skyGroundSeparation = true;

    std::vector<uint16_t> missingMaskRgb = sampleRgb();
    FinishingResult missingMaskResult;
    const bool missingMaskOk = FinishingPipeline::process(
        missingMaskRgb, 2, 2, nullptr, options, missingMaskResult);
    expect(!missingMaskOk, "sky-ground mode without a mask must fail");
    expect(!missingMaskResult.error.empty(),
           "a missing sky mask must provide an error");

    std::vector<uint16_t> wrongMaskRgb = sampleRgb();
    const std::vector<uint8_t> wrongMask(3, 255);
    FinishingResult wrongMaskResult;
    const bool wrongMaskOk = FinishingPipeline::process(
        wrongMaskRgb, 2, 2, &wrongMask, options, wrongMaskResult);
    expect(!wrongMaskOk, "a wrongly sized sky mask must fail");
    expect(!wrongMaskResult.error.empty(),
           "a wrongly sized sky mask must provide an error");
}

void testPreCancellationStopsBeforeWork() {
    std::vector<uint16_t> rgb = sampleRgb();
    const std::vector<uint16_t> original = rgb;
    FinishingResult result;
    int reportedStages = 0;

    const bool ok = FinishingPipeline::process(
        rgb, 2, 2, nullptr, FinishingOptions{}, result,
        [&](FinishingStage) { ++reportedStages; },
        [] { return true; });

    expect(!ok, "a pre-cancelled pipeline must return false");
    expect(result.cancelled, "pre-cancellation must be distinguished from error");
    expect(result.error.empty(), "cancellation must not be reported as an error");
    expect(rgb == original, "pre-cancellation must not mutate RGB");
    expect(reportedStages == 0, "pre-cancellation must not report a stage");
}

void testEnabledStagesRunInWorkerOrder() {
    constexpr int width = 32;
    constexpr int height = 32;
    std::vector<uint16_t> rgb(static_cast<size_t>(width) * height * 3);
    for (size_t pixel = 0; pixel < rgb.size() / 3; ++pixel) {
        // A low-amplitude gradient gives every engine valid, non-clipped input.
        const uint16_t base = static_cast<uint16_t>(1200 + pixel % width * 8);
        rgb[pixel * 3] = base;
        rgb[pixel * 3 + 1] = static_cast<uint16_t>(base + 10);
        rgb[pixel * 3 + 2] = static_cast<uint16_t>(base + 20);
    }
    const std::vector<uint8_t> skyMask(
        static_cast<size_t>(width) * height, 255);

    FinishingOptions options;
    options.modifiedCameraColorEnabled = true;
    options.noiseReductionEnabled = true;
    options.noiseReductionStrength = 10;
    options.dehazeEnabled = true;
    options.dehazeStrength = 10;
    options.stretchEnabled = true;
    options.skyGroundSeparation = true;
    options.groundDetailStrength = 10;
    options.starReductionEnabled = true;
    options.starReductionStrength = 10;

    std::vector<FinishingStage> stages;
    FinishingResult result;
    const bool ok = FinishingPipeline::process(
        rgb, width, height, &skyMask, options, result,
        [&](FinishingStage stage) { stages.push_back(stage); });
    const std::vector<FinishingStage> expected = {
        FinishingStage::ModifiedCameraColor,
        FinishingStage::NoiseReduction,
        FinishingStage::Dehaze,
        FinishingStage::Stretch,
        FinishingStage::GroundDetail,
        FinishingStage::StarReduction
    };

    expect(ok, "all enabled finishing stages should succeed on valid input");
    expect(result.error.empty(), "successful finishing must not report an error");
    expect(stages == expected,
           "enabled stages must run in the same order as ProcessingWorker");
}

} // namespace

int main() {
    testNoOptionsAreBitExact();
    testInvalidRgbIsRejected();
    testSkyMaskIsRequiredAndSizedExactly();
    testPreCancellationStopsBeforeWork();
    testEnabledStagesRunInWorkerOrder();

    if (failures != 0) {
        std::cerr << failures << " finishing pipeline test(s) failed\n";
        return 1;
    }
    std::cout << "All finishing pipeline tests passed\n";
    return 0;
}
