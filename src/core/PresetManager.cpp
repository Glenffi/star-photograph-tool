#include "PresetManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

QList<Preset> PresetManager::builtinPresets() {
    QList<Preset> presets;

    // 银河广角
    Preset milkyWay;
    milkyWay.name = "银河广角";
    milkyWay.alignMethod = "star";
    milkyWay.stackMethod = "median";
    milkyWay.kappaValue = 2.5;
    // Traditional DCP can suppress real dark nebula structure. Keep it an
    // explicit correction instead of applying it to every nightscape.
    milkyWay.dewarpEnabled = false;
    milkyWay.dewarpStrength = 25;
    milkyWay.noiseReductionEnabled = true;
    milkyWay.noiseReductionStrength = 30;
    milkyWay.stretchEnabled = true;
    milkyWay.starReduceEnabled = false;
    milkyWay.starReduceStrength = 70;
    milkyWay.outputFormat = "tiff16";
    presets.append(milkyWay);

    // 深空天体
    Preset deepSky;
    deepSky.name = "深空天体";
    deepSky.alignMethod = "star";
    deepSky.stackMethod = "winsorized";
    deepSky.kappaValue = 2.5;
    deepSky.dewarpEnabled = false;
    deepSky.dewarpStrength = 20;
    deepSky.noiseReductionEnabled = true;
    deepSky.noiseReductionStrength = 35;
    deepSky.stretchEnabled = true;
    deepSky.starReduceEnabled = false;
    deepSky.starReduceStrength = 70;
    deepSky.outputFormat = "tiff16";
    presets.append(deepSky);

    return presets;
}

void PresetManager::savePreset(const Preset& preset, const QString& path) {
    QJsonObject obj;
    obj["name"] = preset.name;
    obj["alignMethod"] = preset.alignMethod;
    obj["stackMethod"] = preset.stackMethod;
    obj["kappaValue"] = preset.kappaValue;
    obj["autoRejectLowQualityFrames"] = preset.autoRejectLowQualityFrames;
    obj["photometricNormalizationEnabled"] =
        preset.photometricNormalizationEnabled;
    obj["dewarpEnabled"] = preset.dewarpEnabled;
    obj["dewarpStrength"] = preset.dewarpStrength;
    obj["noiseReductionEnabled"] = preset.noiseReductionEnabled;
    obj["noiseReductionStrength"] = preset.noiseReductionStrength;
    obj["modifiedCameraColorEnabled"] = preset.modifiedCameraColorEnabled;
    obj["modifiedCameraColorStrength"] = preset.modifiedCameraColorStrength;
    obj["stretchEnabled"] = preset.stretchEnabled;
    obj["temperature"] = preset.basicAdjustments.temperature;
    obj["tint"] = preset.basicAdjustments.tint;
    obj["exposureTenths"] = preset.basicAdjustments.exposureTenths;
    obj["contrast"] = preset.basicAdjustments.contrast;
    obj["highlights"] = preset.basicAdjustments.highlights;
    obj["shadows"] = preset.basicAdjustments.shadows;
    obj["whites"] = preset.basicAdjustments.whites;
    obj["blacks"] = preset.basicAdjustments.blacks;
    obj["vibrance"] = preset.basicAdjustments.vibrance;
    obj["saturation"] = preset.basicAdjustments.saturation;
    obj["sharpening"] = preset.basicAdjustments.sharpening;
    obj["starReduceEnabled"] = preset.starReduceEnabled;
    obj["starReduceStrength"] = preset.starReduceStrength;
    obj["outputFormat"] = preset.outputFormat;

    QJsonDocument doc(obj);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Compact));
    }
}

Preset PresetManager::loadPreset(const QString& path) {
    Preset preset;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return preset;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();

    preset.name = obj["name"].toString();
    preset.alignMethod = obj["alignMethod"].toString("star");
    preset.stackMethod = obj["stackMethod"].toString("average");
    preset.kappaValue = obj["kappaValue"].toDouble(2.5);
    preset.autoRejectLowQualityFrames =
        obj["autoRejectLowQualityFrames"].toBool(true);
    preset.photometricNormalizationEnabled =
        obj["photometricNormalizationEnabled"].toBool(true);
    preset.dewarpEnabled = obj["dewarpEnabled"].toBool(false);
    preset.dewarpStrength = obj["dewarpStrength"].toInt(30);
    preset.noiseReductionEnabled = obj["noiseReductionEnabled"].toBool(false);
    preset.noiseReductionStrength = obj["noiseReductionStrength"].toInt(30);
    preset.modifiedCameraColorEnabled =
        obj["modifiedCameraColorEnabled"].toBool(false);
    preset.modifiedCameraColorStrength =
        obj["modifiedCameraColorStrength"].toInt(100);
    preset.stretchEnabled = obj["stretchEnabled"].toBool(false);
    preset.basicAdjustments.temperature = obj["temperature"].toInt(0);
    preset.basicAdjustments.tint = obj["tint"].toInt(0);
    preset.basicAdjustments.exposureTenths =
        obj["exposureTenths"].toInt(0);
    preset.basicAdjustments.contrast = obj["contrast"].toInt(0);
    preset.basicAdjustments.highlights = obj["highlights"].toInt(0);
    preset.basicAdjustments.shadows = obj["shadows"].toInt(0);
    preset.basicAdjustments.whites = obj["whites"].toInt(0);
    preset.basicAdjustments.blacks = obj["blacks"].toInt(0);
    preset.basicAdjustments.vibrance = obj["vibrance"].toInt(0);
    preset.basicAdjustments.saturation = obj["saturation"].toInt(0);
    preset.basicAdjustments.sharpening = obj["sharpening"].toInt(0);
    preset.starReduceEnabled = obj["starReduceEnabled"].toBool(false);
    preset.starReduceStrength = obj["starReduceStrength"].toInt(70);
    preset.outputFormat = obj["outputFormat"].toString("tiff16");

    return preset;
}
