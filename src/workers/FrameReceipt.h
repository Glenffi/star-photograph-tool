#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct FrameReceipt {
    enum class Outcome {
        Used,
        ManualExcluded,
        NotSubmitted,
        QualityRejected,
        RawLoadFailed,
        NoStars,
        AlignmentFailed,
        ResamplingFailed,
        OtherSkipped,
        Unconfirmed
    };

    struct Row {
        QString sourcePath; // In-memory UI data only.
        Outcome outcome = Outcome::Unconfirmed;
        bool reference = false;
        int outputNumber = 0; // 1-based only for a written sequence frame.
        int detectedStars = -1; // Formal detection only; compare within a scene mode.
        double qualityScore = -1.0;
        QStringList reasonCodes;
    };

    QVector<Row> rows;
    bool sequence = false;
    int deliverables = 0;

    int submittedCount() const;
    int usedCount() const;
    int skippedCount() const; // Confirmed processing skips, not unknown outcomes.
    int manualCount() const;
    QJsonObject anonymousJson() const;
    bool saveAnonymous(const QString& path, QString& error) const;
};
