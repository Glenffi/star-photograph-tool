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
 * reads only RAW headers, applies the same metadata rules as ProcessingWorker,
 * and groups repeated findings into one actionable report.
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
        Bias
    };

    enum class Severity {
        Warning,
        Error
    };

    struct FrameRecord {
        QString path;
        Role role = Role::Light;
        bool readable = false;
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
