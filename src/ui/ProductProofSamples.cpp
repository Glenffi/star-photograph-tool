#include "ProductProofSamples.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QLatin1String productKey(ProcessingScene scene) {
    switch (scene) {
    case ProcessingScene::SingleFrame:
    case ProcessingScene::Nightscape:
    case ProcessingScene::DeepSky:
    case ProcessingScene::SkyGround:
        return QLatin1String("still");
    case ProcessingScene::StarTrail:
        return QLatin1String("starTrail");
    case ProcessingScene::Timelapse:
        return QLatin1String("timelapse");
    }
    return {};
}

QJsonObject sample(ProcessingScene scene) {
    static const QJsonObject catalog = [] {
        QFile manifest(QStringLiteral(":/print-order/product-proof-samples.json"));
        if (!manifest.open(QIODevice::ReadOnly)) return QJsonObject{};
        return QJsonDocument::fromJson(manifest.readAll()).object();
    }();
    return catalog.value(productKey(scene)).toObject();
}

}  // namespace

namespace ProductProofSamples {

int count(ProcessingScene scene) {
    return static_cast<int>(sample(scene).value(QLatin1String("frames")).toArray().size());
}

QImage frame(ProcessingScene scene, int index) {
    const QJsonArray frames = sample(scene).value(QLatin1String("frames")).toArray();
    if (index < 0 || index >= frames.size()) return {};
    const QString file = frames.at(index).toObject().value(QLatin1String("file")).toString();
    if (file.isEmpty()) return {};
    return QImage(QStringLiteral(":/print-order/") + file);
}

QString credit(ProcessingScene scene) {
    return sample(scene).value(QLatin1String("credit")).toString();
}

}  // namespace ProductProofSamples
