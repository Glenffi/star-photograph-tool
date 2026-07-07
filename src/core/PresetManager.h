#pragma once

#include <QString>
#include <QList>

struct Preset {
    QString name;
    QString alignMethod;
    QString stackMethod;
    double kappaValue;
    bool dewarpEnabled;
    int dewarpStrength;
    bool stretchEnabled;
    bool starReduceEnabled;
    int starReduceStrength;
    QString outputFormat;
};

class PresetManager {
public:
    static QList<Preset> builtinPresets();
    static void savePreset(const Preset& preset, const QString& path);
    static Preset loadPreset(const QString& path);
};
