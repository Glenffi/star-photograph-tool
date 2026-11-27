#include "ui/ProductProofSamples.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRect>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

using ProductProofSamples::count;
using ProductProofSamples::credit;
using ProductProofSamples::frame;

struct Proof {
    ProcessingScene scene;
    const char* key;
    const char* source;
    const char* download;
    const char* credit;
    const char* license;
    const char* licenseUrl;
    QSize size;
    int frames;
};

void check(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toUtf8().constData());
}

QList<QImage> verify(const QJsonObject& manifest, const Proof& proof) {
    const auto entry = manifest.value(QLatin1String(proof.key)).toObject();
    const auto files = entry.value("frames").toArray();
    const QString key = QString::fromLatin1(proof.key);
    check(entry.value("sourceUrl") == proof.source && entry.value("sourceFileUrl") == proof.download &&
              entry.value("credit") == proof.credit && credit(proof.scene) == proof.credit &&
              entry.value("license") == proof.license && entry.value("licenseUrl") == proof.licenseUrl &&
              !entry.value("author").toString().isEmpty() && !entry.value("title").toString().isEmpty() &&
              !entry.value("processing").toString().isEmpty(),
          "Missing or incorrect attribution for " + key);
    check(files.size() == proof.frames && count(proof.scene) == proof.frames,
          "Wrong frame count for " + key);
    check(frame(proof.scene, -1).isNull() && frame(proof.scene, proof.frames).isNull(),
          "Invalid indices must return a null image for " + key);

    QList<QImage> previews;
    for (int i = 0; i < proof.frames; ++i) {
        const auto item = files.at(i).toObject();
        const QString path = QStringLiteral(":/print-order/") + item.value("file").toString();
        QFile file(path);
        check(file.open(QIODevice::ReadOnly), "Missing packaged proof " + path);
        const QByteArray hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
        check(hash == item.value("sha256").toString().toLatin1(), "Proof differs from manifest: " + path);

        QImageReader reader(path);
        const QImage image = frame(proof.scene, i);
        check(reader.format() == "jpeg" && image.size() == proof.size && !image.hasAlphaChannel() &&
                  image == reader.read(),
              "Proof must decode the packaged JPEG without changes: " + path);
        if (proof.scene == ProcessingScene::Timelapse) {
            check(entry.value("sourceFramesPerSecond").toInt() == 25 &&
                      item.value("sourceTimeMs").toInt() == 8000 + 5000 * i &&
                      item.value("sourceFrame").toInt() == 200 + 125 * i,
                  "Wrong source time or playback order for " + path);
        } else {
            check(hash == entry.value("sourceFileSha256").toString().toLatin1(),
                  "Unmodified sample differs from its source checksum: " + path);
        }
        previews.append(image.scaled(144, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGB32));
    }
    return previews;
}

double colorDifference(const QImage& first, const QImage& second, const QRect& region) {
    double difference = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
        for (int x = region.left(); x <= region.right(); ++x) {
            const auto a = first.pixel(x, y);
            const auto b = second.pixel(x, y);
            difference += std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
                          std::abs(qBlue(a) - qBlue(b));
        }
    }
    return difference / (3 * region.width() * region.height());
}

void testProofs() {
    QFile file(QStringLiteral(":/print-order/product-proof-samples.json"));
    check(file.open(QIODevice::ReadOnly), "Missing bundled proof manifest");
    const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
    const auto still = verify(manifest, {ProcessingScene::Nightscape, "still",
        "https://npgallery.nps.gov/AssetDetail/2ef91b9f-ad6d-4c60-9a57-d2386724aeb7",
        "https://npgallery.nps.gov/GetAsset/2ef91b9f-ad6d-4c60-9a57-d2386724aeb7/proxymdres.jpg",
        "Neal Herbert / NPS", "Public domain (U.S. Government work)",
        "https://www.nps.gov/aboutus/disclaimer.htm", QSize(1440, 960), 1});
    const auto trail = verify(manifest, {ProcessingScene::StarTrail, "starTrail",
        "https://www.eso.org/public/images/santerne-northern-sky/",
        "https://cdn.eso.org/images/screen/santerne-northern-sky.jpg",
        "ESO/A. Santerne", "CC BY 4.0", "https://creativecommons.org/licenses/by/4.0/", QSize(1280, 853), 1});
    const auto sequence = verify(manifest, {ProcessingScene::Timelapse, "timelapse",
        "https://www.eso.org/public/videos/p4paranal/",
        "https://cdn.eso.org/videos/hd_and_apple/p4paranal.m4v",
        "ESO/S. Guisard (www.eso.org/~sguisard)", "CC BY 4.0",
        "https://creativecommons.org/licenses/by/4.0/", QSize(1080, 720), 5});

    const QImage stillImage = frame(ProcessingScene::Nightscape);
    for (const auto scene : {ProcessingScene::SingleFrame, ProcessingScene::DeepSky, ProcessingScene::SkyGround}) {
        check(count(scene) == 1 && frame(scene) == stillImage && credit(scene) == credit(ProcessingScene::Nightscape),
              "All still subjects must share the still product proof and its attribution");
    }
    const auto unknown = static_cast<ProcessingScene>(-1);
    check(count(unknown) == 0 && frame(unknown).isNull() && credit(unknown).isEmpty(),
          "An unknown scene must not select a proof");

    const QRect preview = still.front().rect();
    check(colorDifference(still.front(), trail.front(), preview) > 10.0 &&
              colorDifference(still.front(), sequence.front(), preview) > 10.0 &&
              colorDifference(trail.front(), sequence.front(), preview) > 10.0,
          "The three products must remain visibly distinct at the same preview size");
    const QRect sky(12, 0, 120, 52);
    // Sample the right hillside, away from the moving telescope enclosures.
    const QRect ridge(112, 69, 30, 22);
    for (qsizetype i = 1; i < sequence.size(); ++i) {
        const double skyChange = colorDifference(sequence.at(i - 1), sequence.at(i), sky);
        const double ridgeChange = colorDifference(sequence.at(i - 1), sequence.at(i), ridge);
        check(skyChange > 3.0 && ridgeChange < skyChange * 0.5,
              QStringLiteral("Timelapse frame %1 must move the sky over a stable foreground").arg(i + 1));
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    try {
        testProofs();
        std::cout << "[PASS] Product proof selection, pixel differences, chronological frames, and provenance\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
