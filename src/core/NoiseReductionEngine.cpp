#include "NoiseReductionEngine.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <limits>

namespace {

bool validRgb(const std::vector<uint16_t>& rgb, int width, int height,
              size_t& pixelCount) {
    if (width <= 0 || height <= 0 || width > INT_MAX / height) return false;
    pixelCount = static_cast<size_t>(width) * height;
    return pixelCount <= std::numeric_limits<size_t>::max() / 3 &&
        rgb.size() == pixelCount * 3;
}

float clampSample(const std::vector<float>& plane, int width, int height,
                  int x, int y) {
    const int clampedX = std::clamp(x, 0, width - 1);
    const int clampedY = std::clamp(y, 0, height - 1);
    return plane[static_cast<size_t>(clampedY) * width + clampedX];
}

void atrousBlur(const std::vector<float>& source, int width, int height,
                int step, std::vector<float>& temporary,
                std::vector<float>& blurred) {
    static constexpr std::array<float, 5> kernel = {
        1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f,
        4.0f / 16.0f, 1.0f / 16.0f
    };
    const size_t pixelCount = static_cast<size_t>(width) * height;
    temporary.resize(pixelCount);
    blurred.resize(pixelCount);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -2; tap <= 2; ++tap) {
                sum += kernel[static_cast<size_t>(tap + 2)] *
                    clampSample(source, width, height,
                                x + tap * step, y);
            }
            temporary[static_cast<size_t>(y) * width + x] = sum;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -2; tap <= 2; ++tap) {
                sum += kernel[static_cast<size_t>(tap + 2)] *
                    clampSample(temporary, width, height,
                                x, y + tap * step);
            }
            blurred[static_cast<size_t>(y) * width + x] = sum;
        }
    }
}

float detailSigma(const std::vector<float>& source,
                  const std::vector<float>& blurred) {
    constexpr size_t maxSamples = 262144;
    const size_t step =
        std::max<size_t>(1, (source.size() - 1) / maxSamples + 1);
    std::vector<float> deviations;
    deviations.reserve(std::min(source.size(), maxSamples + 1));
    for (size_t index = 0; index < source.size(); index += step) {
        deviations.push_back(std::abs(source[index] - blurred[index]));
    }
    if (deviations.empty()) return 0.0f;
    const size_t middle = deviations.size() / 2;
    std::nth_element(deviations.begin(), deviations.begin() + middle,
                     deviations.end());
    const float median = deviations[middle];
    return median * 1.4826f;
}

void shrinkPlane(std::vector<float>& plane, int width, int height,
                 float normalizedStrength, bool chroma) {
    std::vector<float> temporary;
    std::vector<float> blurred;
    std::vector<float> retainedDetails(plane.size(), 0.0f);
    for (int level = 0; level < 2; ++level) {
        atrousBlur(plane, width, height, 1 << level,
                   temporary, blurred);
        const float sigma = detailSigma(plane, blurred);
        if (sigma <= 1.0e-6f) continue;

        const float multiplier = chroma
            ? (level == 0 ? 4.2f : 2.1f)
            : (level == 0 ? 2.8f : 1.4f);
        const float threshold = sigma * multiplier * normalizedStrength;
        const float protectThreshold = threshold * 3.0f;
        for (size_t index = 0; index < plane.size(); ++index) {
            const float detail = plane[index] - blurred[index];
            const float magnitude = std::abs(detail);
            float retained = 0.0f;
            if (magnitude >= protectThreshold) {
                // Strong coefficients are likely stars, dust lanes or nebular
                // structure. Preserve them instead of subtracting a fixed bias.
                retained = detail;
            } else if (magnitude > threshold) {
                const float t =
                    (magnitude - threshold) /
                    std::max(1.0e-6f, protectThreshold - threshold);
                const float weight = t * t * (3.0f - 2.0f * t);
                retained = detail * weight;
            }
            retainedDetails[index] += retained;
            plane[index] = blurred[index];
        }
    }
    for (size_t index = 0; index < plane.size(); ++index) {
        plane[index] += retainedDetails[index];
    }
}

uint16_t clampToUint16(float value) {
    return static_cast<uint16_t>(
        std::lround(std::clamp(value, 0.0f, 65535.0f)));
}

} // namespace

bool NoiseReductionEngine::denoiseRgb(
    const std::vector<uint16_t>& src, int width, int height, int strength,
    std::vector<uint16_t>& dst) {
    size_t pixelCount = 0;
    if (!validRgb(src, width, height, pixelCount) || strength < 0) {
        return false;
    }
    if (strength == 0) {
        dst = src;
        return true;
    }
    strength = std::min(strength, 100);

    std::vector<float> luminance(pixelCount);
    std::vector<float> redMinusGreen(pixelCount);
    std::vector<float> blueMinusGreen(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        const float red = src[base];
        const float green = src[base + 1];
        const float blue = src[base + 2];
        luminance[pixel] =
            (13933.0f * red + 46871.0f * green + 4732.0f * blue) /
            65536.0f;
        redMinusGreen[pixel] = red - green;
        blueMinusGreen[pixel] = blue - green;
    }

    const float normalizedStrength = strength / 100.0f;
    shrinkPlane(luminance, width, height, normalizedStrength, false);
    shrinkPlane(redMinusGreen, width, height, normalizedStrength, true);
    shrinkPlane(blueMinusGreen, width, height, normalizedStrength, true);

    dst.resize(src.size());
    constexpr float redWeight = 13933.0f / 65536.0f;
    constexpr float blueWeight = 4732.0f / 65536.0f;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const float green = luminance[pixel] -
            redWeight * redMinusGreen[pixel] -
            blueWeight * blueMinusGreen[pixel];
        const size_t base = pixel * 3;
        dst[base] = clampToUint16(green + redMinusGreen[pixel]);
        dst[base + 1] = clampToUint16(green);
        dst[base + 2] = clampToUint16(green + blueMinusGreen[pixel]);
    }
    return true;
}
