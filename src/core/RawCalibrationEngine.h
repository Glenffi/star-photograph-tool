#pragma once

#include "RawImageLoader.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Calibrates Bayer-domain deep-sky RAW data before demosaic.
 *
 * A matched master dark already includes the sensor bias pedestal, so light
 * frames subtract either Dark or Bias, never both. Flat frames are calibrated
 * with Master Bias and normalized independently for each Bayer phase before
 * integration; this removes vignetting and dust without using the flat light's
 * color as a white-balance reference.
 */
class RawCalibrationEngine {
public:
    struct MasterFrames {
        int width = 0;
        int height = 0;
        int rawWidth = 0;
        int rawHeight = 0;
        int topMargin = 0;
        int leftMargin = 0;
        int iso = 0;
        double lightExposureTime = 0.0;
        std::string cameraModel;
        std::array<uint8_t, 4> cfaPattern = {};
        uint16_t saturation = 0;
        std::vector<float> bias;
        std::vector<float> dark;
        std::vector<float> flat;

        bool complete() const noexcept;
    };

    struct CalibrationStats {
        size_t clippedLowPixels = 0;
        size_t clippedHighPixels = 0;
        size_t invalidFlatPixels = 0;
    };

    class MeanAccumulator {
    public:
        explicit MeanAccumulator(size_t valueCount);

        bool add(const std::vector<uint16_t>& frame);
        bool add(const std::vector<float>& frame);
        bool finish(std::vector<float>& mean) const;
        size_t frameCount() const noexcept { return m_frameCount; }

    private:
        std::vector<double> m_sum;
        std::vector<float> m_minimum;
        std::vector<float> m_maximum;
        size_t m_frameCount = 0;
    };

    static bool compatible(const RawImageLoader::CfaImageData& reference,
                           const RawImageLoader::CfaImageData& candidate,
                           std::string& reason);

    static bool normalizeFlat(
        const RawImageLoader::CfaImageData& flat,
        const std::vector<float>& masterBias,
        std::vector<float>& normalized,
        std::array<double, 4>* phaseMedians = nullptr);

    static bool finalizeMasterFlat(std::vector<float>& masterFlat,
                                   int width, int height);

    static bool calibrateLight(const RawImageLoader::CfaImageData& light,
                               const MasterFrames& masters,
                               RawImageLoader::CfaImageData& calibrated,
                               CalibrationStats* stats = nullptr);
};
