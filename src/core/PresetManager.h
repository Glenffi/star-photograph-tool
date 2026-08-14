#pragma once

#include <QString>
#include <QList>

#include "BasicAdjustmentEngine.h"

struct Preset {
    QString name;
    QString alignMethod = "star";
    QString stackMethod = "average";
    double kappaValue = 2.5;
    bool autoRejectLowQualityFrames = true;
    bool photometricNormalizationEnabled = true;
    bool dewarpEnabled = false;
    int dewarpStrength = 30;
    bool noiseReductionEnabled = false;
    int noiseReductionStrength = 30;
    bool modifiedCameraColorEnabled = false;
    int modifiedCameraColorStrength = 100;
    bool stretchEnabled = false;
    BasicAdjustmentOptions basicAdjustments;
    bool starDefringeEnabled = false;
    int starDefringeStrength = 55;
    bool starReduceEnabled = false;
    int starReduceStrength = 70;
    QString outputFormat = "tiff16";
};

class PresetManager {
public:
    static QList<Preset> builtinPresets();
    static void savePreset(const Preset& preset, const QString& path);
    static Preset loadPreset(const QString& path);
};
