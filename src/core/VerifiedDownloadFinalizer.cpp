#include "VerifiedDownloadFinalizer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

namespace {

constexpr int kMaximumCandidateNumber = 999;
constexpr int kRenameAttempts = 4;

bool validSha256(const QByteArray& value) {
    if (value.size() != 64) return false;
    for (const char ch : value) {
        const bool digit = ch >= '0' && ch <= '9';
        const bool lowerHex = ch >= 'a' && ch <= 'f';
        const bool upperHex = ch >= 'A' && ch <= 'F';
        if (!digit && !lowerHex && !upperHex) return false;
    }
    return true;
}

bool verifyFile(const QString& path, qint64 expectedSize,
                const QByteArray& expectedSha256Hex, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("无法读取已下载的安装包：%1")
                    .arg(file.errorString());
        return false;
    }
    if (file.size() != expectedSize) {
        error = QStringLiteral("安装包大小不匹配（实际 %1，预期 %2）")
                    .arg(file.size())
                    .arg(expectedSize);
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) {
            error = QStringLiteral("读取安装包失败：%1")
                        .arg(file.errorString());
            return false;
        }
        hash.addData(block);
    }
    if (hash.result().toHex().toLower() != expectedSha256Hex.toLower()) {
        error = QStringLiteral("安装包 SHA-256 校验失败");
        return false;
    }
    return true;
}

}  // namespace

QString VerifiedDownloadFinalizer::candidatePath(
    const QString& directoryPath, const QString& fileName, int number) {
    const QDir directory(directoryPath);
    if (number <= 0) return directory.filePath(fileName);

    const QFileInfo info(fileName);
    const QString suffix = info.suffix();
    const QString baseName = info.completeBaseName();
    const QString candidateName = suffix.isEmpty()
        ? QStringLiteral("%1 (%2)").arg(baseName).arg(number)
        : QStringLiteral("%1 (%2).%3").arg(baseName).arg(number).arg(suffix);
    return directory.filePath(candidateName);
}

bool VerifiedDownloadFinalizer::commit(
    const QString& temporaryPath, const QString& directoryPath,
    const QString& fileName, qint64 expectedSize,
    const QByteArray& expectedSha256Hex, QString& savedPath,
    QString& error, const RenameAttempt& renameAttempt) {
    savedPath.clear();
    error.clear();

    if (temporaryPath.isEmpty() || directoryPath.isEmpty() ||
        fileName.isEmpty() || QFileInfo(fileName).fileName() != fileName ||
        fileName == QStringLiteral(".") || fileName == QStringLiteral("..") ||
        fileName.contains('/') || fileName.contains('\\') ||
        expectedSize <= 0 || !validSha256(expectedSha256Hex)) {
        error = QStringLiteral("安装包落盘参数无效");
        return false;
    }
    if (!verifyFile(temporaryPath, expectedSize, expectedSha256Hex, error)) {
        return false;
    }

    QString lastRenameError;
    QString lastCopyError;
    const RenameAttempt moveFile = renameAttempt
        ? renameAttempt
        : [](const QString& sourcePath, const QString& targetPath,
             QString& moveError) {
              QFile source(sourcePath);
              if (source.rename(targetPath)) return true;
              moveError = source.errorString();
              return false;
          };
    for (int number = 0; number <= kMaximumCandidateNumber; ++number) {
        const QString candidate = candidatePath(
            directoryPath, fileName, number);
        if (QFileInfo::exists(candidate)) continue;

        for (int attempt = 0; attempt < kRenameAttempts; ++attempt) {
            QString moveError;
            if (moveFile(temporaryPath, candidate, moveError)) {
                savedPath = candidate;
                return true;
            }
            lastRenameError = moveError;
            if (QFileInfo::exists(candidate)) break;
            if (attempt + 1 < kRenameAttempts) {
                QThread::msleep(static_cast<unsigned long>(75 * (attempt + 1)));
            }
        }
        if (QFileInfo::exists(candidate)) continue;

        // A verified copy is slower than rename but avoids losing a complete
        // download when Windows Defender or an indexer retains a short-lived
        // handle to the temporary file.
        QFile source(temporaryPath);
        if (source.copy(candidate)) {
            QString copiedFileError;
            if (verifyFile(candidate, expectedSize, expectedSha256Hex,
                           copiedFileError)) {
                QFile::remove(temporaryPath);
                savedPath = candidate;
                return true;
            }
            QFile::remove(candidate);
            error = QStringLiteral("复制后的安装包校验失败：%1")
                        .arg(copiedFileError);
            return false;
        }
        lastCopyError = source.errorString();
        if (QFileInfo::exists(candidate)) continue;

        error = QStringLiteral(
                    "无法保存安装包到下载目录。\n"
                    "目标：%1\n移动失败：%2\n复制失败：%3")
                    .arg(candidate, lastRenameError, lastCopyError);
        return false;
    }

    error = QStringLiteral("下载目录中同名安装包过多");
    return false;
}
