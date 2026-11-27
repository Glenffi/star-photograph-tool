#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QStringList>

struct PrintOrderDocument {
    QString scene;
    bool automaticStill = true;
    QStringList lights, excluded, dark, flat, bias, darkFlat, editedMaskSources;
    QString reference, selected, masterDark, masterFlat, masterBias, masterDarkFlat;
    QString userMask, outputDirectory, editedMaskReference, comparisonManifest;
    QJsonObject params;
    // A gray8 mask binds to the ordered non-excluded lights and to an active
    // frame (the explicit reference, if set). A null mask has no bindings.
    QImage editedMask;

    // Stored relative paths are based at the order file. Load returns absolute
    // paths and replaces result only on success.
    static bool load(const QString& path, PrintOrderDocument& result, QString& error);
    // Relative values supplied to save are based at the destination order file.
    bool save(const QString& path, QString& error) const;

    // Report missing sources (including excluded lights), not output or comparison paths.
    QStringList missingInputPaths() const;

    // Change only input paths below oldDir, preserving their relative suffixes.
    // Returns false without changes on a collision, invalid path, or no match.
    bool relocateDirectory(const QString& oldDir, const QString& newDir);
};
