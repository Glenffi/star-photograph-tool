#include "FrameReceipt.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

using Outcome = FrameReceipt::Outcome;

QLatin1String outcomeCode(Outcome outcome) {
    switch (outcome) {
    case Outcome::Used: return QLatin1String("used");
    case Outcome::ManualExcluded: return QLatin1String("manual-excluded");
    case Outcome::NotSubmitted: return QLatin1String("not-submitted");
    case Outcome::QualityRejected: return QLatin1String("quality-rejected");
    case Outcome::RawLoadFailed: return QLatin1String("raw-load-failed");
    case Outcome::NoStars: return QLatin1String("no-stars");
    case Outcome::AlignmentFailed: return QLatin1String("alignment-failed");
    case Outcome::ResamplingFailed: return QLatin1String("resampling-failed");
    case Outcome::OtherSkipped: return QLatin1String("other-skipped");
    case Outcome::Unconfirmed: return QLatin1String("unconfirmed");
    }
    return QLatin1String("unconfirmed");
}

bool isProcessingSkip(Outcome outcome) {
    switch (outcome) {
    case Outcome::QualityRejected:
    case Outcome::RawLoadFailed:
    case Outcome::NoStars:
    case Outcome::AlignmentFailed:
    case Outcome::ResamplingFailed:
    case Outcome::OtherSkipped:
        return true;
    default:
        return false;
    }
}

QJsonArray anonymousReasons(const QStringList& reasons) {
    static const QStringList allowed{
        QStringLiteral("quality-outlier"),
        QStringLiteral("raw-decode-failed"),
        QStringLiteral("no-alignment-stars"),
        QStringLiteral("no-fit-model"),
        QStringLiteral("alignment-quality-gate"),
        QStringLiteral("resampling-failed"),
        QStringLiteral("insufficient-matches"),
        QStringLiteral("low-match-coverage"),
        QStringLiteral("rms-above-2px"),
        QStringLiteral("p95-above-3.5px"),
        QStringLiteral("insufficient-field-coverage"),
        QStringLiteral("low-grid-coverage"),
        QStringLiteral("insufficient-outer-grid-coverage"),
        QStringLiteral("outer-p95-above-4px"),
        QStringLiteral("cell-rms-above-3px"),
        QStringLiteral("cell-trimmed-p95-above-4.5px")
    };
    QJsonArray result;
    QSet<QString> emitted;
    for (const QString& reason : reasons) {
        const auto code = std::find(allowed.cbegin(), allowed.cend(), reason);
        if (code == allowed.cend() || emitted.contains(*code)) continue;
        emitted.insert(*code);
        result.append(*code);
    }
    return result;
}

bool hasJsonSuffix(const QFileInfo& file) {
    return file.suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) == 0;
}

bool isLocalJsonPath(const QString& path, const QFileInfo& file) {
    if (path.contains(QChar::Null) || !file.isAbsolute() || !hasJsonSuffix(file)) {
        return false;
    }
#ifdef Q_OS_WIN
    const QString nativePath = QDir::fromNativeSeparators(path);
    // Reject UNC/device paths and alternate data streams.
    if (nativePath.size() < 3 || nativePath.at(1) != u':' ||
        nativePath.at(2) != u'/' || nativePath.indexOf(u':', 2) >= 0) {
        return false;
    }
#endif
    return file.isNativePath() && !file.isSymLink();
}

} // namespace

int FrameReceipt::submittedCount() const {
    return static_cast<int>(std::count_if(rows.cbegin(), rows.cend(), [](const Row& row) {
        return row.outcome != Outcome::ManualExcluded && row.outcome != Outcome::NotSubmitted;
    }));
}

int FrameReceipt::usedCount() const {
    return static_cast<int>(std::count_if(rows.cbegin(), rows.cend(), [](const Row& row) {
        return row.outcome == Outcome::Used;
    }));
}

int FrameReceipt::skippedCount() const {
    return static_cast<int>(std::count_if(rows.cbegin(), rows.cend(), [](const Row& row) {
        return isProcessingSkip(row.outcome);
    }));
}

int FrameReceipt::manualCount() const {
    return static_cast<int>(std::count_if(rows.cbegin(), rows.cend(), [](const Row& row) {
        return row.outcome == Outcome::ManualExcluded;
    }));
}

QJsonObject FrameReceipt::anonymousJson() const {
    QJsonArray anonymousRows;
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const Row& row = rows.at(i);
        QJsonObject entry{
            {QStringLiteral("frameNumber"), static_cast<qint64>(i + 1)},
            {QStringLiteral("outcome"), QString(outcomeCode(row.outcome))},
            {QStringLiteral("reference"), row.reference}
        };
        if (sequence && row.outcome == Outcome::Used && row.outputNumber > 0) {
            entry.insert(QStringLiteral("outputNumber"), row.outputNumber);
        }
        if (row.detectedStars >= 0) {
            entry.insert(QStringLiteral("detectedStars"), row.detectedStars);
        }
        if (std::isfinite(row.qualityScore) && row.qualityScore != -1.0) {
            entry.insert(QStringLiteral("qualityScore"), row.qualityScore);
        }
        const QJsonArray reasons = anonymousReasons(row.reasonCodes);
        if (!reasons.isEmpty()) {
            entry.insert(QStringLiteral("reasonCodes"), reasons);
        }
        anonymousRows.append(entry);
    }

    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("sequence"), sequence},
        {QStringLiteral("deliverables"), deliverables},
        {QStringLiteral("submittedCount"), submittedCount()},
        {QStringLiteral("usedCount"), usedCount()},
        {QStringLiteral("skippedCount"), skippedCount()},
        {QStringLiteral("manualCount"), manualCount()},
        {QStringLiteral("rows"), anonymousRows}
    };
}

bool FrameReceipt::saveAnonymous(const QString& path, QString& error) const {
    error.clear();
    if (deliverables <= 0) {
        error = QStringLiteral("receipt-unavailable");
        return false;
    }
    const QFileInfo destination(path);
    if (!isLocalJsonPath(path, destination)) {
        error = QStringLiteral("invalid-receipt-path");
        return false;
    }
    // A RAW can have an unconventional suffix. Protect sources even then.
    const QString absolute = destination.absoluteFilePath();
    const QString canonical = destination.canonicalFilePath();
    for (const Row& row : rows) {
        const QFileInfo source(row.sourcePath);
        if (!hasJsonSuffix(source)) continue;
        if (source.absoluteFilePath() == absolute ||
            (!canonical.isEmpty() && source.canonicalFilePath() == canonical)) {
            error = QStringLiteral("invalid-receipt-path");
            return false;
        }
    }

    const QByteArray json = QJsonDocument(anonymousJson()).toJson(QJsonDocument::Indented);
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) {
        error = QStringLiteral("receipt-write-failed");
        return false;
    }
    return true;
}
