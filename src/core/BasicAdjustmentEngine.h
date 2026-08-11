#pragma once

#include <cstdint>
#include <vector>

/**
 * Camera Raw style global adjustments for an RGB16 working image.
 *
 * Values intentionally use integer UI units so presets and QSettings remain
 * stable across platforms. Exposure is stored in tenths of an EV; the other
 * signed controls use the familiar -100..100 range.
 */
struct BasicAdjustmentOptions {
    int temperature = 0;
    int tint = 0;
    int exposureTenths = 0;
    int contrast = 0;
    int highlights = 0;
    int shadows = 0;
    int whites = 0;
    int blacks = 0;
    int vibrance = 0;
    int saturation = 0;
    int sharpening = 0;

    bool hasToneOrColorAdjustments() const noexcept;
    bool hasSharpening() const noexcept { return sharpening > 0; }
    bool isNeutral() const noexcept {
        return !hasToneOrColorAdjustments() && !hasSharpening();
    }
    bool isValid() const noexcept;
};

class BasicAdjustmentEngine {
public:
    // Applies white balance, linked luminance tone controls and chroma controls.
    // RGB hue is retained by fitting chroma around the target luminance instead
    // of clipping the three channels independently.
    static bool adjustRgb(const std::vector<uint16_t>& src, int width, int height,
                          const BasicAdjustmentOptions& options,
                          std::vector<uint16_t>& dst);

    // Luminance-only thresholded unsharp mask. Dark noise and bright star cores
    // are protected, and RGB is rebuilt around the sharpened luminance.
    static bool sharpenRgb(const std::vector<uint16_t>& src, int width, int height,
                           int strength, std::vector<uint16_t>& dst);
};
