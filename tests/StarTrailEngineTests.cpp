#include "core/StarTrailEngine.h"

#include <climits>
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

std::vector<uint16_t> solidFrame(int width, int height,
                                 uint16_t red, uint16_t green, uint16_t blue) {
    std::vector<uint16_t> frame(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
    for (size_t offset = 0; offset < frame.size(); offset += 3) {
        frame[offset] = red;
        frame[offset + 1] = green;
        frame[offset + 2] = blue;
    }
    return frame;
}

void setPixel(std::vector<uint16_t>& frame, int width, int x, int y,
              uint16_t value) {
    const size_t offset =
        (static_cast<size_t>(y) * static_cast<size_t>(width) +
         static_cast<size_t>(x)) * 3;
    frame[offset] = value;
    frame[offset + 1] = value;
    frame[offset + 2] = value;
}

uint16_t redAt(const std::vector<uint16_t>& frame, int width, int x, int y = 0) {
    return frame[(static_cast<size_t>(y) * static_cast<size_t>(width) +
                  static_cast<size_t>(x)) * 3];
}

void testLightenBuildsMovingTrail() {
    constexpr int width = 7;
    StarTrailEngine engine;
    expect(engine.initialize(width, 1), "Lighten initialization should succeed");

    for (int x : {1, 3, 5}) {
        auto frame = solidFrame(width, 1, 1000, 1000, 1000);
        setPixel(frame, width, x, 0, 12000);
        expect(engine.addFrame(frame), "Lighten should accept each valid frame");
    }

    std::vector<uint16_t> output;
    expect(engine.render(output), "Lighten should render after frames are added");
    expect(redAt(output, width, 1) == 12000 &&
               redAt(output, width, 3) == 12000 &&
               redAt(output, width, 5) == 12000,
           "moving bright points should form a complete Lighten trail");
    expect(redAt(output, width, 0) == 1000,
           "Lighten should preserve untouched background");
    expect(engine.statistics().processedFrames == 3,
           "statistics should report every accepted frame");
    expect(engine.statistics().referenceBackground ==
               std::array<uint16_t, 3>{{1000, 1000, 1000}},
           "a bright outlier must not bias robust background statistics");
}

void testCometNormalizesAndAnchorsBackground() {
    constexpr int width = 7;
    StarTrailEngine::Options options;
    options.mode = StarTrailEngine::Mode::Comet;
    options.cometStrength = 50.0;

    StarTrailEngine engine;
    expect(engine.initialize(width, 1, options),
           "Comet initialization should succeed");
    auto first = solidFrame(width, 1, 1000, 2000, 3000);
    auto second = solidFrame(width, 1, 5000, 6000, 7000);
    setPixel(second, width, 3, 0, 12000);
    expect(engine.addFrame(first) && engine.addFrame(second),
           "Comet should accept changing-background frames");

    std::vector<uint16_t> output;
    expect(engine.render(output), "Comet should render normalized frames");
    for (size_t offset = 0; offset < output.size(); offset += 3) {
        if (offset == static_cast<size_t>(3 * 3)) continue;
        expect(output[offset] == 1000 && output[offset + 1] == 2000 &&
                   output[offset + 2] == 3000,
               "background drift must be normalized to the first frame");
    }
    const size_t starOffset = static_cast<size_t>(3 * 3);
    expect(output[starOffset] == 8000 && output[starOffset + 1] == 8000 &&
               output[starOffset + 2] == 8000,
           "bright signal should be measured above each frame background");
    expect(engine.statistics().referenceBackground ==
               std::array<uint16_t, 3>{{1000, 2000, 3000}},
           "statistics should expose first-frame channel backgrounds");
}

void testForwardCometDecay() {
    constexpr int width = 7;
    StarTrailEngine::Options options;
    options.mode = StarTrailEngine::Mode::Comet;
    options.direction = StarTrailEngine::TailDirection::Forward;
    options.cometStrength = 100.0;

    StarTrailEngine engine;
    expect(engine.initialize(width, 1, options),
           "forward Comet initialization should succeed");
    for (int x : {1, 3, 5}) {
        auto frame = solidFrame(width, 1, 1000, 1000, 1000);
        setPixel(frame, width, x, 0, 11000);
        expect(engine.addFrame(frame), "forward Comet should accept a frame");
    }

    std::vector<uint16_t> output;
    expect(engine.render(output), "forward Comet should render");
    expect(redAt(output, width, 1) < redAt(output, width, 3) &&
               redAt(output, width, 3) < redAt(output, width, 5),
           "forward Comet should fade older trail positions");
    expect(redAt(output, width, 0) == 1000,
           "forward Comet must not darken the background");
}

void testReverseCometDecay() {
    constexpr int width = 7;
    StarTrailEngine::Options options;
    options.mode = StarTrailEngine::Mode::Comet;
    options.direction = StarTrailEngine::TailDirection::Reverse;
    options.cometStrength = 100.0;

    StarTrailEngine engine;
    expect(engine.initialize(width, 1, options),
           "reverse Comet initialization should succeed");
    for (int x : {1, 3, 5}) {
        auto frame = solidFrame(width, 1, 1000, 1000, 1000);
        setPixel(frame, width, x, 0, 11000);
        expect(engine.addFrame(frame), "reverse Comet should accept a frame");
    }

    std::vector<uint16_t> output;
    expect(engine.render(output), "reverse Comet should render");
    expect(redAt(output, width, 1) > redAt(output, width, 3) &&
               redAt(output, width, 3) > redAt(output, width, 5),
           "reverse Comet should fade later trail positions");
}

void testZeroStrengthIsLighten() {
    constexpr int width = 5;
    StarTrailEngine::Options cometOptions;
    cometOptions.mode = StarTrailEngine::Mode::Comet;
    cometOptions.direction = StarTrailEngine::TailDirection::Reverse;
    cometOptions.cometStrength = 0.0;

    StarTrailEngine lighten;
    StarTrailEngine comet;
    expect(lighten.initialize(width, 1) &&
               comet.initialize(width, 1, cometOptions),
           "zero-strength comparison engines should initialize");

    auto first = solidFrame(width, 1, 1000, 2000, 3000);
    auto second = solidFrame(width, 1, 4000, 5000, 6000);
    setPixel(first, width, 1, 0, 12000);
    setPixel(second, width, 3, 0, 13000);
    expect(lighten.addFrame(first) && lighten.addFrame(second) &&
               comet.addFrame(first) && comet.addFrame(second),
           "both engines should accept identical input");

    std::vector<uint16_t> lightenOutput;
    std::vector<uint16_t> cometOutput;
    expect(lighten.render(lightenOutput) && comet.render(cometOutput),
           "both engines should render");
    expect(cometOutput == lightenOutput,
           "Comet strength zero must be bit-exact Lighten");
}

void testValidationErrors() {
    StarTrailEngine engine;
    expect(!engine.initialize(0, 1) &&
               engine.lastError() == StarTrailEngine::Error::InvalidDimensions,
           "zero dimensions must be rejected");
    expect(!engine.initialize(INT_MAX, INT_MAX) &&
               engine.lastError() == StarTrailEngine::Error::SizeOverflow,
           "unaddressable RGB dimensions must be rejected before allocation");

    StarTrailEngine::Options invalidOptions;
    invalidOptions.cometStrength = 101.0;
    expect(!engine.initialize(2, 2, invalidOptions) &&
               engine.lastError() == StarTrailEngine::Error::InvalidOptions,
           "out-of-range strength must be rejected");

    expect(engine.initialize(2, 2), "validation engine should initialize");
    std::vector<uint16_t> output = {1, 2, 3};
    expect(!engine.render(output) && output.empty() &&
               engine.lastError() == StarTrailEngine::Error::NoFrames,
           "rendering without frames must fail and clear output");
    expect(!engine.addFrame(std::vector<uint16_t>(11)) &&
               engine.lastError() == StarTrailEngine::Error::InvalidFrameSize,
           "a frame with the wrong RGB size must be rejected");
    expect(engine.statistics().processedFrames == 0,
           "a rejected frame must not change statistics");
}

} // namespace

int main() {
    testLightenBuildsMovingTrail();
    testCometNormalizesAndAnchorsBackground();
    testForwardCometDecay();
    testReverseCometDecay();
    testZeroStrengthIsLighten();
    testValidationErrors();

    if (failures != 0) {
        std::cerr << failures << " star-trail test(s) failed\n";
        return 1;
    }
    std::cout << "All star-trail tests passed\n";
    return 0;
}
