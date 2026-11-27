#include "workers/FrameReceipt.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>

#include <iostream>
#include <limits>

namespace {

using Outcome = FrameReceipt::Outcome;

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::cerr << "[FAIL] " << message << '\n';
}

FrameReceipt::Row row(const QString& sourcePath, Outcome outcome) {
    FrameReceipt::Row result;
    result.sourcePath = sourcePath;
    result.outcome = outcome;
    return result;
}

QSet<QString> keys(const QJsonObject& json) {
    const auto names = json.keys();
    return QSet<QString>(names.cbegin(), names.cend());
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
}

void testAnonymousJson() {
    const QStringList codes{
        "used", "manual-excluded", "not-submitted", "quality-rejected", "raw-load-failed",
        "no-stars", "alignment-failed", "resampling-failed", "other-skipped", "unconfirmed"
    };
    const QVector<Outcome> outcomes{
        Outcome::Used, Outcome::ManualExcluded, Outcome::NotSubmitted,
        Outcome::QualityRejected, Outcome::RawLoadFailed, Outcome::NoStars,
        Outcome::AlignmentFailed, Outcome::ResamplingFailed, Outcome::OtherSkipped,
        Outcome::Unconfirmed
    };
    FrameReceipt receipt;
    receipt.sequence = true;
    receipt.deliverables = 1;
    for (qsizetype i = 0; i < outcomes.size(); ++i) {
        receipt.rows.append(row(QString("/Volumes/PrivateShare/Users/HomeSecret/%1/IMG_SECRET.dng").arg(i),
                                outcomes.at(i)));
    }
    receipt.rows[0].reference = true;
    receipt.rows[0].outputNumber = 1;
    receipt.rows[0].detectedStars = 42;
    receipt.rows[0].qualityScore = -2.25; // A measured score can be negative.
    receipt.rows[1].outputNumber = 9;
    receipt.rows[4].qualityScore = std::numeric_limits<double>::infinity();
    receipt.rows[5].detectedStars = 0;
    receipt.rows[6].reasonCodes = {
        "alignment-quality-gate",
        "low-grid-coverage",
        "decode: /Users/HomeSecret/unescaped-raw.dng",
        "low-grid-coverage",
        "quality-outlier:/Volumes/PrivateShare/IMG_SECRET.dng",
        "stage-only-but-not-allowlisted",
        "GPS=31.12345; EXIF=private; 2026:11:27 21:00:01",
        QString::fromLatin1(QByteArray(64, 'a'))
    };
    check(receipt.submittedCount() == 8 && receipt.usedCount() == 1 &&
              receipt.skippedCount() == 6 && receipt.manualCount() == 1,
          "Rejected and skipped rows should count once; unconfirmed is submitted but not a confirmed skip");

    const auto json = receipt.anonymousJson();
    check(keys(json) == QSet<QString>{"schemaVersion", "sequence", "deliverables", "rows",
                                          "submittedCount", "usedCount", "skippedCount", "manualCount"} &&
              json.value("schemaVersion").toInt() == 1 && json.value("sequence").toBool() &&
              json.value("deliverables").toInt() == 1 && json.value("submittedCount").toInt() == 8 &&
              json.value("usedCount").toInt() == 1 && json.value("skippedCount").toInt() == 6 &&
              json.value("manualCount").toInt() == 1,
          "Anonymous receipt should expose only the versioned schema and consistent counts");

    const auto rows = json.value("rows").toArray();
    check(rows.size() == outcomes.size(), "Each receipt row should appear anonymously");
    for (qsizetype i = 0; i < rows.size() && i < outcomes.size(); ++i) {
        const auto entry = rows.at(i).toObject();
        QSet<QString> expectedKeys{"frameNumber", "outcome", "reference"};
        if (i == 0) expectedKeys.unite({"outputNumber", "detectedStars", "qualityScore"});
        if (i == 5) expectedKeys.insert("detectedStars");
        if (i == 6) expectedKeys.insert("reasonCodes");
        check(keys(entry) == expectedKeys && entry.value("frameNumber").toInt() == i + 1 &&
                  entry.value("outcome").toString() == codes.at(i) &&
                  entry.value("reference").toBool() == (i == 0),
              "Row should use stable ordinal identity, controlled state and only present metrics");
    }
    const auto used = rows.at(0).toObject();
    check(used.value("outputNumber").toInt() == 1 && used.value("detectedStars").toInt() == 42 &&
              used.value("qualityScore").toDouble() == -2.25 &&
              rows.at(5).toObject().value("detectedStars").toInt(-1) == 0,
          "Known values, including zero detected stars and a negative quality score, should round trip");
    check(rows.at(6).toObject().value("reasonCodes").toArray() ==
              QJsonArray{"alignment-quality-gate", "low-grid-coverage"},
          "Only exact allowlisted reason codes should survive; discard duplicate and unknown text");

    for (qsizetype i = 0; i < receipt.rows.size(); ++i) {
        receipt.rows[i].sourcePath = QString("C:/DifferentPrivateDir/%1/new.dng").arg(i);
    }
    check(receipt.anonymousJson() == json,
          "Paths, including their names and any deterministic hash, must have no influence on anonymous JSON");

    receipt.sequence = false;
    check(!receipt.anonymousJson().value("rows").toArray().at(0).toObject().contains("outputNumber"),
          "A shared non-sequence deliverable should not acquire a per-source output number");
}

void testSaveAnonymous() {
    QTemporaryDir folder;
    check(folder.isValid(), "Export tests need a local temporary directory");
    if (!folder.isValid()) return;

    const QByteArray previous("untouched original\n");
    const QString project = folder.filePath("project.nebprint");
    const QString raw = folder.filePath("original.dng");
    const QString unusualRaw = folder.filePath("original-with-unusual-suffix.json");
    const QString output = folder.filePath("receipt.JSON");
    check(writeFile(project, previous) && writeFile(raw, previous) &&
              writeFile(unusualRaw, previous) && writeFile(output, previous),
          "Existing targets should be created for overwrite and refusal checks");

    FrameReceipt receipt;
    receipt.deliverables = 1;
    receipt.rows = {row(raw, Outcome::Used), row(unusualRaw, Outcome::ManualExcluded)};
    QString error = "stale-error";
    check(receipt.saveAnonymous(output, error) && error.isEmpty() &&
              QJsonDocument::fromJson(readFile(output)).object() == receipt.anonymousJson(),
          "A valid absolute local .json destination should be atomically replaced");

    const QStringList invalidPaths{project, raw, unusualRaw, "relative-receipt.json",
        "https://example.invalid/receipt.json", QUrl::fromLocalFile(output).toString(),
        ":/receipt.json", output + QChar::Null + "ignored.json"};
    for (const auto& invalid : invalidPaths) {
        check(!receipt.saveAnonymous(invalid, error) && error == "invalid-receipt-path",
              "Refuse project/RAW paths, source aliases, URLs, resources, and non-absolute paths");
    }
    check(readFile(project) == previous && readFile(raw) == previous && readFile(unusualRaw) == previous,
          "Path validation must not overwrite a RAW or a project file");

    const QByteArray saved = readFile(output);
    check(!FrameReceipt{}.saveAnonymous(output, error) && error == "receipt-unavailable" &&
              readFile(output) == saved,
          "An empty receipt must not overwrite a valid receipt");

    const QString missingParent = folder.filePath("parent-missing/receipt.json");
    check(!receipt.saveAnonymous(missingParent, error) && error == "receipt-write-failed" &&
              !QFileInfo::exists(missingParent) && readFile(output) == saved,
          "A filesystem write error must not create the target or expose its name in the error code");

#ifdef Q_OS_UNIX
    const QString link = folder.filePath("project-alias.json");
    check(QFile::link(project, link) && !receipt.saveAnonymous(link, error) &&
              error == "invalid-receipt-path" && readFile(project) == previous &&
              QFileInfo(link).isSymLink(),
          "A .json symlink must not overwrite its project-file target");

    const bool permissionSet = QFile::setPermissions(output, QFileDevice::ReadOwner);
    check(permissionSet, "Read-only destination fixture should be created");
    if (permissionSet && !QFileInfo(output).isWritable()) {
        check(!receipt.saveAnonymous(output, error) && error == "receipt-write-failed" &&
                  readFile(output) == saved,
              "A failed save to an existing read-only file must preserve its old bytes");
    }
#endif
}

} // namespace

int main() {
    testAnonymousJson();
    testSaveAnonymous();
    if (failures != 0) return 1;
    std::cout << "All frame receipt tests passed.\n";
    return 0;
}
