#include "core/BasicAdjustmentEngine.h"

#include <algorithm>
#include <cmath>
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

std::vector<uint16_t> repeatedRgb(int width, int height,
                                  uint16_t r, uint16_t g, uint16_t b) {
    std::vector<uint16_t> image(static_cast<size_t>(width) * height * 3);
    for (size_t pixel = 0; pixel < image.size() / 3; ++pixel) {
        image[pixel * 3] = r;
        image[pixel * 3 + 1] = g;
        image[pixel * 3 + 2] = b;
    }
    return image;
}

void testNeutralOptionsAreBitExact() {
    const std::vector<uint16_t> source = {
        123, 456, 789, 5000, 7000, 9000,
        65535, 32000, 1000, 0, 0, 0
    };
    std::vector<uint16_t> adjusted;
    BasicAdjustmentOptions options;
    expect(BasicAdjustmentEngine::adjustRgb(source, 2, 2, options, adjusted),
           "neutral basic adjustments should accept valid RGB");
    expect(adjusted == source,
           "neutral basic adjustments must be bit-exact");

    std::vector<uint16_t> sharpened;
    expect(BasicAdjustmentEngine::sharpenRgb(
               source, 2, 2, 0, sharpened),
           "zero sharpening should accept valid RGB");
    expect(sharpened == source, "zero sharpening must be bit-exact");
}

void testExposureContrastAndRegionalTone() {
    std::vector<uint16_t> source = {
        5000, 5000, 5000,
        16000, 16000, 16000,
        48000, 48000, 48000
    };
    BasicAdjustmentOptions options;
    options.exposureTenths = 10;
    std::vector<uint16_t> exposed;
    expect(BasicAdjustmentEngine::adjustRgb(
               source, 3, 1, options, exposed),
           "one-stop exposure should process");
    expect(std::abs(static_cast<int>(exposed[0]) - 10000) <= 2,
           "+1 EV should double an unclipped neutral value");

    options = {};
    options.contrast = 60;
    std::vector<uint16_t> contrasted;
    expect(BasicAdjustmentEngine::adjustRgb(
               source, 3, 1, options, contrasted),
           "contrast should process");
    expect(contrasted[0] < source[0] && contrasted[6] > source[6],
           "positive contrast should separate shadows and highlights");

    options = {};
    options.shadows = 80;
    std::vector<uint16_t> shadows;
    expect(BasicAdjustmentEngine::adjustRgb(
               source, 3, 1, options, shadows),
           "shadow recovery should process");
    const int darkLift = static_cast<int>(shadows[0]) - source[0];
    const int brightLift = static_cast<int>(shadows[6]) - source[6];
    expect(darkLift > brightLift,
           "shadow recovery should favor dark values");

    options = {};
    options.highlights = -80;
    std::vector<uint16_t> highlights;
    expect(BasicAdjustmentEngine::adjustRgb(
               source, 3, 1, options, highlights),
           "highlight recovery should process");
    const int darkReduction = source[0] - highlights[0];
    const int brightReduction = source[6] - highlights[6];
    expect(brightReduction > darkReduction * 3,
           "negative highlights should primarily compress bright values");
}

void testToneCurvesRemainMonotonic() {
    constexpr int levels = 4096;
    std::vector<uint16_t> ramp(static_cast<size_t>(levels) * 3);
    for (int level = 0; level < levels; ++level) {
        const uint16_t value = static_cast<uint16_t>(
            static_cast<uint32_t>(level) * 65535U / (levels - 1));
        ramp[static_cast<size_t>(level) * 3] = value;
        ramp[static_cast<size_t>(level) * 3 + 1] = value;
        ramp[static_cast<size_t>(level) * 3 + 2] = value;
    }

    std::vector<BasicAdjustmentOptions> cases;
    for (int sign : {-100, 100}) {
        BasicAdjustmentOptions contrast;
        contrast.contrast = sign;
        cases.push_back(contrast);
        BasicAdjustmentOptions highlights;
        highlights.highlights = sign;
        cases.push_back(highlights);
        BasicAdjustmentOptions shadows;
        shadows.shadows = sign;
        cases.push_back(shadows);
        BasicAdjustmentOptions whites;
        whites.whites = sign;
        cases.push_back(whites);
        BasicAdjustmentOptions blacks;
        blacks.blacks = sign;
        cases.push_back(blacks);
    }
    BasicAdjustmentOptions combined;
    combined.contrast = 80;
    combined.highlights = -75;
    combined.shadows = 70;
    combined.whites = 45;
    combined.blacks = -50;
    cases.push_back(combined);

    for (const BasicAdjustmentOptions& options : cases) {
        std::vector<uint16_t> result;
        expect(BasicAdjustmentEngine::adjustRgb(
                   ramp, levels, 1, options, result),
               "tone-ramp adjustment should process");
        bool monotonic = true;
        for (int level = 1; level < levels; ++level) {
            if (result[static_cast<size_t>(level) * 3] <
                result[static_cast<size_t>(level - 1) * 3]) {
                monotonic = false;
                break;
            }
        }
        expect(monotonic,
               "every tone control must preserve gray-ramp ordering");
    }
}

void testWhiteBalanceAndColorControls() {
    const std::vector<uint16_t> neutral = {18000, 18000, 18000};
    BasicAdjustmentOptions options;
    options.temperature = 70;
    std::vector<uint16_t> warm;
    expect(BasicAdjustmentEngine::adjustRgb(
               neutral, 1, 1, options, warm),
           "temperature should process");
    expect(warm[0] > warm[1] && warm[1] > warm[2],
           "positive temperature should warm a neutral pixel");

    options = {};
    options.tint = 70;
    std::vector<uint16_t> magenta;
    expect(BasicAdjustmentEngine::adjustRgb(
               neutral, 1, 1, options, magenta),
           "tint should process");
    expect(magenta[1] < magenta[0] && magenta[1] < magenta[2],
           "positive tint should reduce green relative to red and blue");

    const std::vector<uint16_t> colored = {24000, 14000, 8000};
    options = {};
    options.saturation = -100;
    std::vector<uint16_t> monochrome;
    expect(BasicAdjustmentEngine::adjustRgb(
               colored, 1, 1, options, monochrome),
           "full desaturation should process");
    expect(monochrome[0] == monochrome[1] &&
               monochrome[1] == monochrome[2],
           "saturation -100 should produce neutral RGB");

    const std::vector<uint16_t> twoColors = {
        22000, 20000, 19000,
        30000, 9000, 4000
    };
    options = {};
    options.vibrance = 80;
    std::vector<uint16_t> vibrant;
    expect(BasicAdjustmentEngine::adjustRgb(
               twoColors, 2, 1, options, vibrant),
           "vibrance should process");
    const double mutedBefore = 22000.0 - 19000.0;
    const double mutedAfter = vibrant[0] - vibrant[2];
    const double strongBefore = 30000.0 - 4000.0;
    const double strongAfter = vibrant[3] - vibrant[5];
    expect(mutedAfter / mutedBefore > strongAfter / strongBefore,
           "vibrance should favor muted colors over saturated colors");
}

void testSharpeningIsLuminanceOnlyAndThresholded() {
    constexpr int width = 9;
    constexpr int height = 7;
    std::vector<uint16_t> edge = repeatedRgb(width, height, 10000, 10000, 10000);
    for (int y = 0; y < height; ++y) {
        for (int x = width / 2; x < width; ++x) {
            const size_t base = (static_cast<size_t>(y) * width + x) * 3;
            edge[base] = edge[base + 1] = edge[base + 2] = 30000;
        }
    }
    std::vector<uint16_t> sharpened;
    expect(BasicAdjustmentEngine::sharpenRgb(
               edge, width, height, 70, sharpened),
           "sharpening should process an edge image");
    const size_t darkEdge =
        (static_cast<size_t>(height / 2) * width + width / 2 - 1) * 3;
    const size_t brightEdge = darkEdge + 3;
    expect(sharpened[darkEdge] < edge[darkEdge] &&
               sharpened[brightEdge] > edge[brightEdge],
           "unsharp masking should increase edge acutance");
    for (size_t i = 0; i < sharpened.size(); i += 3) {
        expect(sharpened[i] == sharpened[i + 1] &&
                   sharpened[i + 1] == sharpened[i + 2],
               "luminance sharpening should keep neutral pixels neutral");
    }

    const std::vector<uint16_t> flat =
        repeatedRgb(width, height, 12000, 18000, 24000);
    expect(BasicAdjustmentEngine::sharpenRgb(
               flat, width, height, 100, sharpened),
           "sharpening should accept a flat color field");
    expect(sharpened == flat,
           "thresholded sharpening must not alter a flat field");
}

void testInvalidOptionsDoNotOverwriteOutput() {
    const std::vector<uint16_t> source = repeatedRgb(2, 2, 100, 200, 300);
    std::vector<uint16_t> output = {9, 8, 7};
    BasicAdjustmentOptions options;
    options.temperature = 101;
    expect(!BasicAdjustmentEngine::adjustRgb(
               source, 2, 2, options, output),
           "out-of-range controls must be rejected");
    expect(output == std::vector<uint16_t>({9, 8, 7}),
           "validation failure must not overwrite the caller output");
    expect(!BasicAdjustmentEngine::sharpenRgb(
               source, 2, 2, -1, output),
           "negative sharpening must be rejected");
}

} // namespace

int main() {
    testNeutralOptionsAreBitExact();
    testExposureContrastAndRegionalTone();
    testWhiteBalanceAndColorControls();
    testToneCurvesRemainMonotonic();
    testSharpeningIsLuminanceOnlyAndThresholded();
    testInvalidOptionsDoNotOverwriteOutput();

    if (failures != 0) {
        std::cerr << failures << " basic-adjustment test(s) failed\n";
        return 1;
    }
    std::cout << "All basic-adjustment tests passed\n";
    return 0;
}
