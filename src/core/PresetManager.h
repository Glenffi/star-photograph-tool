#pragma once

#include <QString>
#include <QList>

struct Preset {
    QString name;
    QString alignMethod = "star";
    QString stackMethod = "average";
    double kappaValue = 2.5;
    bool dewarpEnabled = false;
    int dewarpStrength = 30;
    bool noiseReductionEnabled = false;
    int noiseReductionStrength = 30;
    bool stretchEnabled = false;
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
