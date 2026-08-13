#pragma once

#include "RawCalibrationEngine.h"

#include <QString>

/**
 * @brief Reads and writes StarProcessor's sensor-space master-frame format.
 *
 * A `.spmaster` file stores floating-point CFA samples together with all
 * geometry and calibration-state metadata needed to reject an incompatible
 * camera, crop, Bayer pattern or exposure. One SHA-256 protects the metadata
 * used by cheap preflight; a second covers that metadata plus every float
 * sample, so neither calibration state nor pixels can change silently.
 */
class MasterFrameIO {
public:
    static bool save(const QString& path,
                     const RawCalibrationEngine::MasterFrame& master,
                     QString& error);

    static bool load(const QString& path,
                     RawCalibrationEngine::MasterFrame& master,
                     QString& error);

    /** Reads and validates metadata and file length without allocating pixels. */
    static bool loadHeader(const QString& path,
                           RawCalibrationEngine::MasterFrame& master,
                           QString& error);
};
