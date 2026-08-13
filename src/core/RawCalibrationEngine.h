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
 * A matched master dark already includes the sensor bias pedestal, so Light
 * frames subtract Dark once. Flat frames are calibrated with either Master
 * Bias or an exposure-matched Master Dark Flat, then normalized independently
 * for each Bayer phase before integration. A previously calibrated Master Flat
 * can also be installed directly. This removes vignetting and dust without
 * using the Flat light's color as a white-balance reference.
 */
class RawCalibrationEngine {
public:
    enum class MasterRole {
        Bias,
        Dark,
        Flat,
        DarkFlat
    };

    /**
     * @brief One decoded master together with the CFA metadata it belongs to.
     *
     * Importers may decode a project-owned master format, FITS or another
     * container into this neutral structure. Keeping geometry and Bayer
     * metadata beside the pixels prevents a master from being applied merely
     * because its vector happens to have the right length.
     *
     * Flat masters must set normalizedFlat after bias/dark-flat subtraction
     * and per-CFA-phase normalization. Other roles keep it false.
     */
    struct MasterFrame {
        MasterRole role = MasterRole::Bias;
        int width = 0;
        int height = 0;
        int rawWidth = 0;
        int rawHeight = 0;
        int topMargin = 0;
        int leftMargin = 0;
        int iso = 0;
        double exposureTime = 0.0;
        std::string cameraModel;
        std::array<uint8_t, 4> cfaPattern = {};
        uint16_t saturation = 0;
        bool normalizedFlat = false;
        // Generated camera Dark/Dark Flat masters normally retain Bias. Set
        // false only when an external Master Dark was explicitly bias-corrected.
        bool darkIncludesBiasPedestal = true;
        std::vector<float> data;

        bool complete() const noexcept;
    };

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
        bool darkIncludesBiasPedestal = true;
        std::vector<float> bias;
        std::vector<float> dark;
        std::vector<float> flat;

        // Bias is required while building a traditional Master Flat, but a
        // fully calibrated imported Master Flat no longer needs it at Light
        // calibration time. Dark and normalized Flat remain mandatory.
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

    static bool createMasterFrame(
        MasterRole role,
        const RawImageLoader::CfaImageData& source,
        const std::vector<float>& data,
        bool normalizedFlat,
        MasterFrame& master);

    static bool validateMasterFrame(
        const RawImageLoader::CfaImageData& reference,
        const MasterFrame& master,
        std::string& reason);

    /**
     * @brief Transactionally installs an imported Bias, Dark or Flat master.
     *
     * Dark Flat is an intermediate used to calibrate individual Flat frames,
     * so it is deliberately rejected here and accepted by the normalizeFlat
     * overload below. On failure, @p masters is left untouched.
     */
    static bool installMasterFrame(
        const RawImageLoader::CfaImageData& lightReference,
        const MasterFrame& master,
        MasterFrames& masters,
        std::string& reason);

    static bool normalizeFlat(
        const RawImageLoader::CfaImageData& flat,
        const std::vector<float>& masterBias,
        std::vector<float>& normalized,
        std::array<double, 4>* phaseMedians = nullptr);

    /**
     * @brief Calibrates a Flat with either Master Bias or Master Dark Flat.
     *
     * A Dark Flat must match the Flat exposure. Both alternatives must match
     * the full sensor geometry, camera, ISO and CFA pattern. Output is only
     * committed after all validation and normalization succeeds.
     */
    static bool normalizeFlat(
        const RawImageLoader::CfaImageData& flat,
        const MasterFrame& offsetMaster,
        std::vector<float>& normalized,
        std::array<double, 4>* phaseMedians = nullptr,
        std::string* reason = nullptr);

    static bool finalizeMasterFlat(std::vector<float>& masterFlat,
                                   int width, int height);

    static bool calibrateLight(const RawImageLoader::CfaImageData& light,
                               const MasterFrames& masters,
                               RawImageLoader::CfaImageData& calibrated,
                               CalibrationStats* stats = nullptr);
};
