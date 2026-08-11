#pragma once

#include <QDateTime>
#include <QByteArray>
#include <QString>
#include <QUrl>

struct UpdatePackage {
    QString platform;
    QString fileName;
    QUrl url;
    QByteArray sha256Hex;
    qint64 size = 0;
};

struct UpdateManifest {
    int schemaVersion = 0;
    QString version;
    QDateTime publishedAt;
    QString releaseNotes;
    UpdatePackage package;
};

// Parses the public update manifest without performing any network access.
// Keeping this validation in the core library makes malformed or unsafe
// manifests testable before they reach the downloader.
class UpdateManifestParser {
public:
    static bool parse(const QByteArray& json, const QString& platform,
                      UpdateManifest& manifest, QString& error);

    // Returns a positive value when lhs is newer than rhs, zero when equal,
    // and a negative value when lhs is older. Invalid versions compare as 0.
    static int compareVersions(const QString& lhs, const QString& rhs);
    static bool isNewerVersion(const QString& candidate,
                               const QString& current);
};
