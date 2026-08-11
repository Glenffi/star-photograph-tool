#include "UpdateManifest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace {

constexpr qint64 kMaximumPackageBytes = 8LL * 1024 * 1024 * 1024;

struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QStringList prerelease;
    bool valid = false;
};

SemanticVersion parseSemanticVersion(QString value) {
    static const QRegularExpression expression(
        QStringLiteral("^v?(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)"
                       "(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?"
                       "(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$"));
    const QRegularExpressionMatch match = expression.match(value.trimmed());
    if (!match.hasMatch()) return {};

    SemanticVersion version;
    bool majorOk = false;
    bool minorOk = false;
    bool patchOk = false;
    version.major = match.captured(1).toInt(&majorOk);
    version.minor = match.captured(2).toInt(&minorOk);
    version.patch = match.captured(3).toInt(&patchOk);
    if (!majorOk || !minorOk || !patchOk) return {};
    if (!match.captured(4).isEmpty()) {
        version.prerelease = match.captured(4).split('.');
        for (const QString& identifier : version.prerelease) {
            bool numeric = true;
            for (const QChar ch : identifier) {
                if (!ch.isDigit()) {
                    numeric = false;
                    break;
                }
            }
            if (numeric && identifier.size() > 1 && identifier.startsWith('0')) {
                return {};
            }
        }
    }
    version.valid = true;
    return version;
}

bool isNumericIdentifier(const QString& value) {
    if (value.isEmpty() || (value.size() > 1 && value.startsWith('0'))) {
        return false;
    }
    for (const QChar ch : value) {
        if (!ch.isDigit()) return false;
    }
    return true;
}

int comparePrerelease(const QStringList& lhs, const QStringList& rhs) {
    if (lhs.isEmpty() && rhs.isEmpty()) return 0;
    if (lhs.isEmpty()) return 1;
    if (rhs.isEmpty()) return -1;

    const int count = std::min(lhs.size(), rhs.size());
    for (int i = 0; i < count; ++i) {
        if (lhs[i] == rhs[i]) continue;
        const bool lhsNumeric = isNumericIdentifier(lhs[i]);
        const bool rhsNumeric = isNumericIdentifier(rhs[i]);
        if (lhsNumeric && rhsNumeric) {
            if (lhs[i].size() != rhs[i].size()) {
                return lhs[i].size() < rhs[i].size() ? -1 : 1;
            }
            return QString::compare(lhs[i], rhs[i], Qt::CaseSensitive) < 0
                ? -1 : 1;
        }
        if (lhsNumeric != rhsNumeric) return lhsNumeric ? -1 : 1;
        return QString::compare(lhs[i], rhs[i], Qt::CaseSensitive) < 0 ? -1 : 1;
    }
    if (lhs.size() == rhs.size()) return 0;
    return lhs.size() < rhs.size() ? -1 : 1;
}

bool validSha256(const QByteArray& value) {
    if (value.size() != 64) return false;
    for (const char ch : value) {
        const bool digit = ch >= '0' && ch <= '9';
        const bool lower = ch >= 'a' && ch <= 'f';
        const bool upper = ch >= 'A' && ch <= 'F';
        if (!digit && !lower && !upper) return false;
    }
    return true;
}

}  // namespace

bool UpdateManifestParser::parse(const QByteArray& json,
                                 const QString& platform,
                                 UpdateManifest& manifest,
                                 QString& error) {
    manifest = {};
    error.clear();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("更新清单不是有效的 JSON 对象");
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
        error = QStringLiteral("不支持的更新清单版本");
        return false;
    }

    const QString version = root.value(QStringLiteral("version")).toString();
    if (!parseSemanticVersion(version).valid) {
        error = QStringLiteral("更新版本号无效");
        return false;
    }

    const QDateTime publishedAt = QDateTime::fromString(
        root.value(QStringLiteral("publishedAt")).toString(), Qt::ISODate);
    if (!publishedAt.isValid()) {
        error = QStringLiteral("更新发布时间无效");
        return false;
    }

    const QJsonObject platforms =
        root.value(QStringLiteral("platforms")).toObject();
    const QJsonObject package = platforms.value(platform).toObject();
    if (package.isEmpty()) {
        error = QStringLiteral("当前平台没有可用安装包");
        return false;
    }

    const QString fileName = package.value(QStringLiteral("fileName")).toString();
    const QUrl url(package.value(QStringLiteral("url")).toString());
    const QByteArray sha256 = package.value(QStringLiteral("sha256"))
                                  .toString().trimmed().toLatin1().toLower();
    const double sizeValue = package.value(QStringLiteral("size")).toDouble(-1.0);
    static const QRegularExpression safeFileName(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    QString expectedFileName;
    if (platform == QStringLiteral("windows-x64")) {
        expectedFileName = QStringLiteral("StarProcessor-Windows-x64-v%1.zip")
                               .arg(version);
    } else if (platform == QStringLiteral("macos-arm64")) {
        expectedFileName = QStringLiteral("StarProcessor-v%1-macOS-arm64.dmg")
                               .arg(version);
    } else if (platform == QStringLiteral("macos-x64")) {
        expectedFileName = QStringLiteral("StarProcessor-v%1-macOS-x64.dmg")
                               .arg(version);
    }
    if (fileName.isEmpty() || !safeFileName.match(fileName).hasMatch() ||
        fileName != QFileInfo(fileName).fileName() ||
        fileName != expectedFileName) {
        error = QStringLiteral("安装包文件名无效");
        return false;
    }
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.query().isEmpty() || !url.fragment().isEmpty() ||
        url.fileName() != fileName) {
        error = QStringLiteral("安装包下载地址无效");
        return false;
    }
    if (!validSha256(sha256)) {
        error = QStringLiteral("安装包 SHA-256 无效");
        return false;
    }
    if (!std::isfinite(sizeValue) || sizeValue < 1.0 ||
        std::floor(sizeValue) != sizeValue ||
        sizeValue > static_cast<double>(kMaximumPackageBytes)) {
        error = QStringLiteral("安装包大小无效");
        return false;
    }

    manifest.schemaVersion = 1;
    manifest.version = version;
    manifest.publishedAt = publishedAt;
    manifest.releaseNotes =
        root.value(QStringLiteral("releaseNotes")).toString().left(10000);
    manifest.package.platform = platform;
    manifest.package.fileName = fileName;
    manifest.package.url = url;
    manifest.package.sha256Hex = sha256;
    manifest.package.size = static_cast<qint64>(sizeValue);
    return true;
}

int UpdateManifestParser::compareVersions(const QString& lhs,
                                          const QString& rhs) {
    const SemanticVersion left = parseSemanticVersion(lhs);
    const SemanticVersion right = parseSemanticVersion(rhs);
    if (!left.valid || !right.valid) return 0;

    if (left.major != right.major) return left.major < right.major ? -1 : 1;
    if (left.minor != right.minor) return left.minor < right.minor ? -1 : 1;
    if (left.patch != right.patch) return left.patch < right.patch ? -1 : 1;
    return comparePrerelease(left.prerelease, right.prerelease);
}

bool UpdateManifestParser::isNewerVersion(const QString& candidate,
                                          const QString& current) {
    return compareVersions(candidate, current) > 0;
}
