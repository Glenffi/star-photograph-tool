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
    milkyWay.dewarpEnabled = true;
    milkyWay.dewarpStrength = 50;
    milkyWay.stretchEnabled = true;
    milkyWay.starReduceEnabled = false;
    milkyWay.starReduceStrength = 50;
    milkyWay.outputFormat = "tiff16";
    presets.append(milkyWay);

    // 深空天体
    Preset deepSky;
    deepSky.name = "深空天体";
    deepSky.alignMethod = "star";
    deepSky.stackMethod = "winsorized";
    deepSky.kappaValue = 2.5;
    deepSky.dewarpEnabled = true;
    deepSky.dewarpStrength = 70;
    deepSky.stretchEnabled = true;
    deepSky.starReduceEnabled = false;
    deepSky.starReduceStrength = 50;
    deepSky.outputFormat = "tiff16";
    presets.append(deepSky);

    // 单帧降噪
    Preset singleFrame;
    singleFrame.name = "单帧降噪";
    singleFrame.alignMethod = "star";
    singleFrame.stackMethod = "median";
    singleFrame.kappaValue = 2.0;
    singleFrame.dewarpEnabled = false;
    singleFrame.dewarpStrength = 30;
    singleFrame.stretchEnabled = true;
    singleFrame.starReduceEnabled = false;
    singleFrame.starReduceStrength = 50;
    singleFrame.outputFormat = "tiff16";
    presets.append(singleFrame);

    // 延时序列
    Preset timelapse;
    timelapse.name = "延时序列";
    timelapse.alignMethod = "star";
    timelapse.stackMethod = "kappa-sigma";
    timelapse.kappaValue = 3.0;
    timelapse.dewarpEnabled = true;
    timelapse.dewarpStrength = 30;
    timelapse.stretchEnabled = false;
    timelapse.starReduceEnabled = false;
    timelapse.starReduceStrength = 50;
    timelapse.outputFormat = "tiff16";
    presets.append(timelapse);

    return presets;
}

void PresetManager::savePreset(const Preset& preset, const QString& path) {
    QJsonObject obj;
    obj["name"] = preset.name;
    obj["alignMethod"] = preset.alignMethod;
    obj["stackMethod"] = preset.stackMethod;
    obj["kappaValue"] = preset.kappaValue;
    obj["dewarpEnabled"] = preset.dewarpEnabled;
    obj["dewarpStrength"] = preset.dewarpStrength;
    obj["stretchEnabled"] = preset.stretchEnabled;
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
    preset.dewarpEnabled = obj["dewarpEnabled"].toBool(false);
    preset.dewarpStrength = obj["dewarpStrength"].toInt(30);
    preset.stretchEnabled = obj["stretchEnabled"].toBool(false);
    preset.starReduceEnabled = obj["starReduceEnabled"].toBool(false);
    preset.starReduceStrength = obj["starReduceStrength"].toInt(50);
    preset.outputFormat = obj["outputFormat"].toString("tiff16");

    return preset;
}
