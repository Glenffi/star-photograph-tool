#pragma once

#include <QByteArray>
#include <QString>
#include <QtTypes>

#include <functional>

// Commits a fully downloaded and verified package into the user's download
// directory without overwriting an existing file. The copy fallback handles
// Windows scanners that can briefly prevent an atomic rename after close().
class VerifiedDownloadFinalizer {
public:
    using RenameAttempt = std::function<bool(
        const QString& sourcePath, const QString& targetPath,
        QString& error)>;

    static QString candidatePath(const QString& directoryPath,
                                 const QString& fileName, int number);

    static bool commit(const QString& temporaryPath,
                       const QString& directoryPath,
                       const QString& fileName,
                       qint64 expectedSize,
                       const QByteArray& expectedSha256Hex,
                       QString& savedPath,
                       QString& error,
                       const RenameAttempt& renameAttempt = {});
};
