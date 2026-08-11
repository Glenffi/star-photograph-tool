#include "BasicAdjustmentEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

constexpr float kRedLuma = 0.2126f;
constexpr float kGreenLuma = 0.7152f;
constexpr float kBlueLuma = 0.0722f;
constexpr float kInvU16 = 1.0f / 65535.0f;

bool checkedPixelCount(int width, int height, size_t& pixelCount) {
    if (width <= 0 || height <= 0) return false;
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > std::numeric_limits<size_t>::max() / h) return false;
    pixelCount = w * h;
    return pixelCount <= std::numeric_limits<size_t>::max() / 3;
}

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float smoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) return value >= edge1 ? 1.0f : 0.0f;
    const float t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float luminance(const std::array<float, 3>& rgb) {
    return kRedLuma * rgb[0] + kGreenLuma * rgb[1] + kBlueLuma * rgb[2];
}

std::array<float, 3> fitChromaToGamut(
    const std::array<float, 3>& rgb, float targetLuminance) {
    const float target = clamp01(targetLuminance);
    float chromaScale = 1.0f;
    for (float channel : rgb) {
        const float chroma = channel - target;
        if (chroma > 0.0f) {
            chromaScale = std::min(chromaScale, (1.0f - target) / chroma);
        } else if (chroma < 0.0f) {
            chromaScale = std::min(chromaScale, target / -chroma);
        }
    }
    chromaScale = clamp01(chromaScale);
    return {
        clamp01(target + (rgb[0] - target) * chromaScale),
        clamp01(target + (rgb[1] - target) * chromaScale),
        clamp01(target + (rgb[2] - target) * chromaScale)
    };
}

float blendPowerCurve(float value, float amount,
                      float positiveExponent, float negativeExponent,
                      float maximumMix) {
    if (std::abs(amount) < 1e-6f) return value;
    const float exponent = amount > 0.0f
        ? positiveExponent : negativeExponent;
    const float target = std::pow(clamp01(value), exponent);
    const float mix = std::abs(amount) * maximumMix;
    return value + (target - value) * mix;
}

float blendWhitePowerCurve(float value, float amount,
                           float positiveExponent, float negativeExponent,
                           float maximumMix) {
    if (std::abs(amount) < 1e-6f) return value;
    const float exponent = amount > 0.0f
        ? positiveExponent : negativeExponent;
    const float target = 1.0f - std::pow(1.0f - clamp01(value), exponent);
    const float mix = std::abs(amount) * maximumMix;
    return value + (target - value) * mix;
}

float applyToneCurve(float value, const BasicAdjustmentOptions& options) {
    const float exposure = std::exp2(
        static_cast<float>(options.exposureTenths) / 10.0f);
    float x = clamp01(value * exposure);

    const float blacks = static_cast<float>(options.blacks) / 100.0f;
    x = blendPowerCurve(x, blacks, 0.28f, 3.2f, 0.32f);

    const float shadows = static_cast<float>(options.shadows) / 100.0f;
    x = blendPowerCurve(x, shadows, 0.45f, 2.0f, 0.68f);

    const float highlights = static_cast<float>(options.highlights) / 100.0f;
    x = blendWhitePowerCurve(x, highlights, 2.0f, 0.45f, 0.62f);

    const float whites = static_cast<float>(options.whites) / 100.0f;
    x = blendWhitePowerCurve(x, whites, 3.2f, 0.28f, 0.30f);

    x = clamp01(x);
    const float contrast = static_cast<float>(options.contrast) / 100.0f;
    // The symmetric power curve is monotonic for the complete control range,
    // unlike an unconstrained cubic S curve which can reverse near endpoints.
    const float exponent = std::exp2(contrast);
    return x < 0.5f
        ? 0.5f * std::pow(2.0f * x, exponent)
        : 1.0f - 0.5f * std::pow(2.0f * (1.0f - x), exponent);
}

uint16_t toU16(float value) {
    return static_cast<uint16_t>(
        std::lround(clamp01(value) * 65535.0f));
}

} // namespace

bool BasicAdjustmentOptions::hasToneOrColorAdjustments() const noexcept {
    return temperature != 0 || tint != 0 || exposureTenths != 0 ||
        contrast != 0 || highlights != 0 || shadows != 0 || whites != 0 ||
        blacks != 0 || vibrance != 0 || saturation != 0;
}

bool BasicAdjustmentOptions::isValid() const noexcept {
    const auto signedControl = [](int value) {
        return value >= -100 && value <= 100;
    };
    return signedControl(temperature) && signedControl(tint) &&
        exposureTenths >= -50 && exposureTenths <= 50 &&
        signedControl(contrast) && signedControl(highlights) &&
        signedControl(shadows) && signedControl(whites) &&
        signedControl(blacks) && signedControl(vibrance) &&
        signedControl(saturation) && sharpening >= 0 && sharpening <= 100;
}

bool BasicAdjustmentEngine::adjustRgb(
    const std::vector<uint16_t>& src, int width, int height,
    const BasicAdjustmentOptions& options, std::vector<uint16_t>& dst) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        src.size() != pixelCount * 3 || !options.isValid()) {
        return false;
    }
    if (!options.hasToneOrColorAdjustments()) {
        dst = src;
        return true;
    }

    std::vector<uint16_t> adjusted(src.size());
    const float temperature = static_cast<float>(options.temperature) / 100.0f;
    const float tint = static_cast<float>(options.tint) / 100.0f;
    std::array<float, 3> gains = {
        std::exp(0.34f * temperature + 0.10f * tint),
        std::exp(-0.22f * tint),
        std::exp(-0.34f * temperature + 0.10f * tint)
    };
    const float neutralLuminance = luminance(gains);
    if (neutralLuminance > 1e-6f) {
        for (float& gain : gains) gain /= neutralLuminance;
    }

    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        std::array<float, 3> rgb = {
            static_cast<float>(src[base]) * kInvU16 * gains[0],
            static_cast<float>(src[base + 1]) * kInvU16 * gains[1],
            static_cast<float>(src[base + 2]) * kInvU16 * gains[2]
        };

        const float sourceLuminance = std::max(luminance(rgb), 0.0f);
        const float targetLuminance = applyToneCurve(sourceLuminance, options);
        if (sourceLuminance > 1e-7f) {
            const float ratio = targetLuminance / sourceLuminance;
            for (float& channel : rgb) channel *= ratio;
        } else {
            rgb = {targetLuminance, targetLuminance, targetLuminance};
        }
        rgb = fitChromaToGamut(rgb, targetLuminance);

        const float maximum = std::max({rgb[0], rgb[1], rgb[2]});
        const float minimum = std::min({rgb[0], rgb[1], rgb[2]});
        const float colorfulness = maximum > 1e-6f
            ? clamp01((maximum - minimum) / maximum) : 0.0f;
        const float saturationFactor =
            1.0f + static_cast<float>(options.saturation) / 100.0f;
        const float vibranceAmount =
            static_cast<float>(options.vibrance) / 100.0f;
        const float vibranceFactor = vibranceAmount >= 0.0f
            ? 1.0f + vibranceAmount * (1.0f - colorfulness) *
                (1.0f - colorfulness)
            : 1.0f + vibranceAmount;
        const float chromaFactor =
            std::max(0.0f, saturationFactor * vibranceFactor);
        for (float& channel : rgb) {
            channel = targetLuminance +
                (channel - targetLuminance) * chromaFactor;
        }
        rgb = fitChromaToGamut(rgb, targetLuminance);

        adjusted[base] = toU16(rgb[0]);
        adjusted[base + 1] = toU16(rgb[1]);
        adjusted[base + 2] = toU16(rgb[2]);
    }

    dst = std::move(adjusted);
    return true;
}

bool BasicAdjustmentEngine::sharpenRgb(
    const std::vector<uint16_t>& src, int width, int height,
    int strength, std::vector<uint16_t>& dst) {
    size_t pixelCount = 0;
    if (!checkedPixelCount(width, height, pixelCount) ||
        src.size() != pixelCount * 3 || strength < 0 || strength > 100) {
        return false;
    }
    if (strength == 0) {
        dst = src;
        return true;
    }

    std::vector<float> luma(pixelCount);
    std::vector<float> horizontal(pixelCount);
    std::vector<float> blurred(pixelCount);
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const size_t base = pixel * 3;
        luma[pixel] = (kRedLuma * src[base] +
                       kGreenLuma * src[base + 1] +
                       kBlueLuma * src[base + 2]) * kInvU16;
    }

    constexpr std::array<float, 5> kernel = {
        1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f,
        4.0f / 16.0f, 1.0f / 16.0f
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -2; tap <= 2; ++tap) {
                const int sampleX = std::clamp(x + tap, 0, width - 1);
                sum += luma[static_cast<size_t>(y) * width + sampleX] *
                    kernel[static_cast<size_t>(tap + 2)];
            }
            horizontal[static_cast<size_t>(y) * width + x] = sum;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -2; tap <= 2; ++tap) {
                const int sampleY = std::clamp(y + tap, 0, height - 1);
                sum += horizontal[static_cast<size_t>(sampleY) * width + x] *
                    kernel[static_cast<size_t>(tap + 2)];
            }
            blurred[static_cast<size_t>(y) * width + x] = sum;
        }
    }

    std::vector<uint16_t> sharpened(src.size());
    const float normalizedStrength = static_cast<float>(strength) / 100.0f;
    const float amount = 1.4f * normalizedStrength;
    const float threshold = 0.0008f + 0.0012f * (1.0f - normalizedStrength);
    const float detailLimit = 0.015f + 0.025f * normalizedStrength;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const float originalLuminance = luma[pixel];
        float detail = originalLuminance - blurred[pixel];
        if (std::abs(detail) <= threshold) {
            detail = 0.0f;
        } else {
            detail = std::copysign(std::abs(detail) - threshold, detail);
        }
        detail = std::clamp(detail, -detailLimit, detailLimit);
        const float protection =
            smoothStep(0.02f, 0.12f, originalLuminance) *
            (1.0f - smoothStep(0.72f, 0.98f, originalLuminance));
        const float targetLuminance = clamp01(
            originalLuminance + amount * protection * detail);

        const size_t base = pixel * 3;
        std::array<float, 3> rgb = {
            static_cast<float>(src[base]) * kInvU16,
            static_cast<float>(src[base + 1]) * kInvU16,
            static_cast<float>(src[base + 2]) * kInvU16
        };
        if (originalLuminance > 1e-7f) {
            const float ratio = targetLuminance / originalLuminance;
            for (float& channel : rgb) channel *= ratio;
        } else {
            rgb = {targetLuminance, targetLuminance, targetLuminance};
        }
        rgb = fitChromaToGamut(rgb, targetLuminance);
        sharpened[base] = toU16(rgb[0]);
        sharpened[base + 1] = toU16(rgb[1]);
        sharpened[base + 2] = toU16(rgb[2]);
    }

    dst = std::move(sharpened);
    return true;
}
