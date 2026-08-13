#pragma once

#include "RawImageLoader.h"

#include <QString>
#include <QStringList>

#include <vector>

/**
 * @brief Performs a cheap header-only check before Bayer calibration starts.
 *
 * Deep-sky calibration used to stop at the first incompatible frame. That made
 * a folder with several metadata problems painful to repair: users had to run
 * the full workflow repeatedly just to discover the next bad file. This class
 * reads only RAW headers, validates raw calibration groups or one imported
 * Master per role, and groups repeated findings into one actionable report.
 *
 * Pixel-dependent checks, such as Flat brightness and CFA buffer validity,
 * remain in the formal calibration path after LibRaw unpacks the sensor data.
 */
class DeepSkyCalibrationPreflight {
public:
    enum class Role {
        Light,
        Dark,
        Flat,
        Bias,
        DarkFlat,
        MasterDark,
        MasterFlat,
        MasterBias,
        MasterDarkFlat
    };

    enum class Severity {
        Warning,
        Error
    };

    struct FrameRecord {
        QString path;
        Role role = Role::Light;
        bool readable = false;
        // Set false only for an imported Master Dark known to have had Bias
        // removed. Preflight then requires a Bias source for Light correction.
        bool masterDarkIncludesBiasPedestal = true;
        RawImageLoader::Metadata metadata;
    };

    struct Finding {
        Severity severity = Severity::Error;
        Role role = Role::Light;
        QString groupKey;
        QString message;
        QString path;
    };

    struct Report {
        int lightCount = 0;
        int darkCount = 0;
        int flatCount = 0;
        int biasCount = 0;
        int darkFlatCount = 0;
        int masterDarkCount = 0;
        int masterFlatCount = 0;
        int masterBiasCount = 0;
        int masterDarkFlatCount = 0;
        QString referenceCamera;
        int referenceIso = 0;
        double referenceExposure = 0.0;
        int referenceWidth = 0;
        int referenceHeight = 0;
        std::vector<Finding> findings;

        bool hasErrors() const;
        QString userMessage(int maximumGroups = 10) const;
        QStringList warningMessages(int maximumGroups = 10) const;
    };

    struct Inputs {
        QStringList lightPaths;
        QStringList darkPaths;
        QStringList flatPaths;
        QStringList biasPaths;
        QStringList darkFlatPaths;
        QStringList masterDarkPaths;
        QStringList masterFlatPaths;
        QStringList masterBiasPaths;
        QStringList masterDarkFlatPaths;
    };

    static Report inspect(RawImageLoader& loader, const Inputs& inputs);

    // Compatibility overload for the original Bias/Dark/Flat workflow.
    static Report inspect(RawImageLoader& loader,
                          const QStringList& lightPaths,
                          const QStringList& darkPaths,
                          const QStringList& flatPaths,
                          const QStringList& biasPaths);

    // Exposed separately so synthetic metadata can cover every rule without
    // requiring camera-specific RAW fixtures in unit tests.
    static Report validate(const std::vector<FrameRecord>& frames);

    static bool exposureMatches(double first, double second);
    static QString roleName(Role role);
};
