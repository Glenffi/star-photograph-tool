#include "UpdateManager.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTimer>

#include <algorithm>

namespace {

constexpr qint64 kMaximumManifestBytes = 1024 * 1024;
constexpr qint64 kDownloadSpaceReserve = 128LL * 1024 * 1024;
constexpr int kAutomaticCheckIntervalSeconds = 24 * 60 * 60;

QString humanReadableSize(qint64 bytes) {
    const double mebibytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QString::number(mebibytes, 'f', mebibytes < 10.0 ? 1 : 0) +
           QStringLiteral(" MiB");
}

QString downloadCandidatePath(const QDir& directory, const QString& fileName,
                              int number) {
    const QFileInfo info(fileName);
    const QString suffix = info.completeSuffix();
    const QString baseName = info.completeBaseName();
    if (number == 0) return directory.filePath(fileName);
    const QString candidateName = suffix.isEmpty()
        ? QStringLiteral("%1 (%2)").arg(baseName).arg(number)
        : QStringLiteral("%1 (%2).%3").arg(baseName).arg(number).arg(suffix);
    return directory.filePath(candidateName);
}

}  // namespace

UpdateManager::UpdateManager(QWidget* parentWindow)
    : QObject(parentWindow),
      m_parentWindow(parentWindow),
      m_network(new QNetworkAccessManager(this)),
      m_manifestUrl(QStringLiteral(STARPROCESSOR_UPDATE_MANIFEST_URL)) {}

UpdateManager::~UpdateManager() {
    m_shuttingDown = true;
    if (m_manifestReply) {
        disconnect(m_manifestReply, nullptr, this, nullptr);
        m_manifestReply->abort();
        m_manifestReply = nullptr;
    }
    if (m_downloadReply) {
        disconnect(m_downloadReply, nullptr, this, nullptr);
        m_downloadReply->abort();
        m_downloadReply = nullptr;
    }
    if (m_progressDialog) {
        disconnect(m_progressDialog, nullptr, this, nullptr);
        m_progressDialog->close();
    }
    if (m_downloadFile) m_downloadFile->close();
}

void UpdateManager::scheduleAutomaticCheck() {
    QSettings settings;
    const QDateTime lastCheck = settings.value(
        QStringLiteral("updates/lastCheckUtc")).toDateTime();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (lastCheck.isValid() && lastCheck.secsTo(now) >= 0 &&
        lastCheck.secsTo(now) < kAutomaticCheckIntervalSeconds) {
        return;
    }
    QTimer::singleShot(2500, this, [this]() { checkForUpdates(false); });
}

void UpdateManager::checkForUpdates(bool interactive) {
    if (m_manifestReply) {
        if (interactive) {
            emit statusMessage(QStringLiteral("正在检查更新..."), 2500);
        }
        return;
    }

    if (platformKey().isEmpty()) {
        if (interactive) {
            QMessageBox::information(m_parentWindow, QStringLiteral("检查更新"),
                                     QStringLiteral("当前平台暂不支持自动更新。"));
        }
        return;
    }

    m_manifestInteractive = interactive;
    m_manifestPayload.clear();
    m_manifestFailure.clear();
    QSettings().setValue(QStringLiteral("updates/lastCheckUtc"),
                         QDateTime::currentDateTimeUtc());
    emit statusMessage(QStringLiteral("正在检查更新..."), 0);

    QNetworkRequest request(m_manifestUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent",
                         QByteArray("StarProcessor/") +
                             QApplication::applicationVersion().toUtf8());
    m_manifestReply = m_network->get(request);
    m_manifestReply->setReadBufferSize(kMaximumManifestBytes + 1);
    connect(m_manifestReply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (!m_manifestReply || !m_manifestFailure.isEmpty()) return;
        const qint64 declaredSize = m_manifestReply->header(
            QNetworkRequest::ContentLengthHeader).toLongLong();
        if (declaredSize > kMaximumManifestBytes) {
            m_manifestFailure = QStringLiteral("更新清单超过允许大小");
            m_manifestReply->abort();
        }
    });
    connect(m_manifestReply, &QNetworkReply::readyRead,
            this, &UpdateManager::consumeManifestData);
    connect(m_manifestReply, &QNetworkReply::finished,
            this, &UpdateManager::handleManifestFinished);
}

QString UpdateManager::platformKey() const {
    const QString architecture = QSysInfo::buildCpuArchitecture().toLower();
#if defined(Q_OS_WIN)
    if (architecture == QStringLiteral("x86_64") ||
        architecture == QStringLiteral("amd64")) {
        return QStringLiteral("windows-x64");
    }
#elif defined(Q_OS_MACOS)
    if (architecture == QStringLiteral("arm64") ||
        architecture == QStringLiteral("aarch64")) {
        return QStringLiteral("macos-arm64");
    }
    if (architecture == QStringLiteral("x86_64")) {
        return QStringLiteral("macos-x64");
    }
#endif
    return {};
}

bool UpdateManager::trustedPackageUrl(const QUrl& url) const {
    const int manifestPort = m_manifestUrl.port(443);
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host().compare(m_manifestUrl.host(), Qt::CaseInsensitive) == 0 &&
           url.port(443) == manifestPort &&
           url.userInfo().isEmpty() && url.query().isEmpty() &&
           url.fragment().isEmpty() &&
           url.path().startsWith(QStringLiteral("/starprocessor/downloads/")) &&
           !url.path().contains(QStringLiteral("/../"));
}

void UpdateManager::consumeManifestData() {
    if (!m_manifestReply || !m_manifestFailure.isEmpty()) return;
    const QByteArray chunk = m_manifestReply->readAll();
    if (chunk.size() > kMaximumManifestBytes - m_manifestPayload.size()) {
        m_manifestFailure = QStringLiteral("更新清单超过允许大小");
        m_manifestReply->abort();
        return;
    }
    m_manifestPayload.append(chunk);
}

void UpdateManager::handleManifestFinished() {
    QNetworkReply* reply = m_manifestReply;
    m_manifestReply = nullptr;
    if (!reply || m_shuttingDown) return;

    // readyRead normally consumed every chunk; this final read also covers a
    // backend that delivers data immediately before finished().
    const QByteArray finalChunk = reply->readAll();
    if (finalChunk.size() > kMaximumManifestBytes - m_manifestPayload.size()) {
        m_manifestFailure = QStringLiteral("更新清单超过允许大小");
    } else {
        m_manifestPayload.append(finalChunk);
    }

    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray payload = std::move(m_manifestPayload);
    const QString manifestFailure = m_manifestFailure;
    m_manifestPayload.clear();
    m_manifestFailure.clear();
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (!manifestFailure.isEmpty()) {
        showCheckError(manifestFailure);
        return;
    }
    if (!networkOk || status < 200 || status >= 300) {
        showCheckError(QStringLiteral("服务器连接失败：%1").arg(networkError));
        return;
    }
    UpdateManifest manifest;
    QString error;
    if (!UpdateManifestParser::parse(payload, platformKey(), manifest, error)) {
        showCheckError(error);
        return;
    }
    if (!trustedPackageUrl(manifest.package.url)) {
        showCheckError(QStringLiteral("更新包地址不属于受信任下载目录"));
        return;
    }

    if (!UpdateManifestParser::isNewerVersion(
            manifest.version, QApplication::applicationVersion())) {
        emit statusMessage(QStringLiteral("当前已是最新版本"), 3500);
        if (m_manifestInteractive) {
            QMessageBox::information(
                m_parentWindow, QStringLiteral("检查更新"),
                QStringLiteral("当前版本 %1 已是最新版本。")
                    .arg(QApplication::applicationVersion()));
        }
        return;
    }

    emit statusMessage(
        QStringLiteral("发现新版本 %1").arg(manifest.version), 5000);
    offerUpdate(manifest);
}

void UpdateManager::offerUpdate(const UpdateManifest& manifest) {
    QMessageBox dialog(m_parentWindow);
    dialog.setWindowTitle(QStringLiteral("发现新版本"));
    dialog.setIcon(QMessageBox::Information);
    dialog.setTextFormat(Qt::PlainText);
    dialog.setText(QStringLiteral("StarProcessor %1 已发布")
                       .arg(manifest.version));
    dialog.setInformativeText(
        QStringLiteral("安装包大小：%1\n\n%2")
            .arg(humanReadableSize(manifest.package.size),
                 manifest.releaseNotes.isEmpty()
                     ? QStringLiteral("此版本包含功能改进和问题修复。")
                     : manifest.releaseNotes));
    auto* downloadButton = dialog.addButton(
        QStringLiteral("下载更新"), QMessageBox::AcceptRole);
    dialog.addButton(QStringLiteral("稍后"), QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() == downloadButton) {
        startDownload(manifest.package, manifest.version);
    }
}

void UpdateManager::startDownload(const UpdatePackage& package,
                                  const QString& version) {
    if (m_downloadReply) {
        emit statusMessage(QStringLiteral("已有更新正在下载"), 3000);
        return;
    }

    QString downloadDirectory = QStandardPaths::writableLocation(
        QStandardPaths::DownloadLocation);
    if (downloadDirectory.isEmpty()) downloadDirectory = QDir::homePath();
    QDir directory(downloadDirectory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        QMessageBox::warning(m_parentWindow, QStringLiteral("下载更新"),
                             QStringLiteral("无法创建下载目录。"));
        return;
    }

    const QStorageInfo storage(directory.absolutePath());
    if (storage.isValid() && storage.isReady() &&
        storage.bytesAvailable() < package.size + kDownloadSpaceReserve) {
        QMessageBox::warning(
            m_parentWindow, QStringLiteral("下载更新"),
            QStringLiteral("下载目录空间不足。至少需要 %1 可用空间。")
                .arg(humanReadableSize(package.size + kDownloadSpaceReserve)));
        return;
    }

    m_downloadPackage = package;
    m_downloadVersion = version;
    m_downloadDirectory = directory.absolutePath();
    m_downloadPath.clear();
    m_downloadFailure.clear();
    m_downloadedBytes = 0;
    m_downloadCancelled = false;
    m_downloadFile = std::make_unique<QTemporaryFile>(
        directory.filePath(QStringLiteral(".StarProcessor-update-XXXXXX.part")));
    m_downloadFile->setAutoRemove(true);
    if (!m_downloadFile->open()) {
        QMessageBox::warning(
            m_parentWindow, QStringLiteral("下载更新"),
            QStringLiteral("无法写入下载目录：%1")
                .arg(m_downloadFile->errorString()));
        resetDownload();
        return;
    }
    m_downloadHash = std::make_unique<QCryptographicHash>(
        QCryptographicHash::Sha256);

    m_progressDialog = new QProgressDialog(
        QStringLiteral("正在下载 StarProcessor %1...").arg(version),
        QStringLiteral("取消"), 0, 1000, m_parentWindow);
    m_progressDialog->setWindowTitle(QStringLiteral("下载更新"));
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setAutoReset(false);
    m_progressDialog->show();

    QNetworkRequest request(package.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(60000);
    request.setRawHeader("User-Agent",
                         QByteArray("StarProcessor/") +
                             QApplication::applicationVersion().toUtf8());
    m_downloadReply = m_network->get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead,
            this, &UpdateManager::consumeDownloadData);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (!m_progressDialog) return;
                const qint64 denominator = total > 0
                    ? total : m_downloadPackage.size;
                const int progress = denominator > 0
                    ? static_cast<int>(std::clamp(
                          received * 1000 / denominator,
                          qint64{0}, qint64{1000}))
                    : 0;
                m_progressDialog->setValue(progress);
                m_progressDialog->setLabelText(
                    QStringLiteral("正在下载 StarProcessor %1... %2 / %3")
                        .arg(m_downloadVersion,
                             humanReadableSize(received),
                             humanReadableSize(denominator)));
            });
    connect(m_progressDialog, &QProgressDialog::canceled, this, [this]() {
        m_downloadCancelled = true;
        if (m_downloadReply) m_downloadReply->abort();
    });
    connect(m_downloadReply, &QNetworkReply::finished,
            this, &UpdateManager::handleDownloadFinished);
}

void UpdateManager::consumeDownloadData() {
    if (!m_downloadReply || !m_downloadFile || !m_downloadHash ||
        !m_downloadFailure.isEmpty()) {
        return;
    }
    const QByteArray data = m_downloadReply->readAll();
    if (data.isEmpty()) return;
    if (m_downloadedBytes > m_downloadPackage.size - data.size()) {
        m_downloadFailure = QStringLiteral("服务器返回的数据超过清单声明大小");
        m_downloadReply->abort();
        return;
    }
    if (m_downloadFile->write(data) != data.size()) {
        m_downloadFailure = QStringLiteral("写入安装包失败：%1")
                                .arg(m_downloadFile->errorString());
        m_downloadReply->abort();
        return;
    }
    m_downloadHash->addData(data);
    m_downloadedBytes += data.size();
}

void UpdateManager::handleDownloadFinished() {
    QNetworkReply* reply = m_downloadReply;
    if (!reply || m_shuttingDown) return;
    consumeDownloadData();
    m_downloadReply = nullptr;

    const QUrl finalUrl = reply->url();
    const bool networkOk = reply->error() == QNetworkReply::NoError;
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if (m_progressDialog) {
        m_progressDialog->hide();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    QString error = m_downloadFailure;
    if (!m_downloadCancelled && error.isEmpty() && !networkOk) {
        error = QStringLiteral("下载失败：%1").arg(networkError);
    }
    if (!m_downloadCancelled && error.isEmpty() &&
        !trustedPackageUrl(finalUrl)) {
        error = QStringLiteral("下载被重定向到不受信任的地址");
    }
    if (!m_downloadCancelled && error.isEmpty() &&
        m_downloadedBytes != m_downloadPackage.size) {
        error = QStringLiteral("安装包大小不匹配（收到 %1，预期 %2）")
                    .arg(m_downloadedBytes)
                    .arg(m_downloadPackage.size);
    }
    if (!m_downloadCancelled && error.isEmpty() &&
        m_downloadHash->result().toHex().toLower() !=
            m_downloadPackage.sha256Hex) {
        error = QStringLiteral("安装包 SHA-256 校验失败，文件已丢弃");
    }
    if (!m_downloadCancelled && error.isEmpty() &&
        !m_downloadFile->flush()) {
        error = QStringLiteral("写入安装包失败：%1")
                    .arg(m_downloadFile->errorString());
    }
    if (!m_downloadCancelled && error.isEmpty()) {
        const QString temporaryPath = m_downloadFile->fileName();
        m_downloadFile->close();
        const QDir directory(m_downloadDirectory);
        for (int number = 0; number <= 999; ++number) {
            const QString candidate = downloadCandidatePath(
                directory, m_downloadPackage.fileName, number);
            if (QFile::rename(temporaryPath, candidate)) {
                m_downloadPath = candidate;
                m_downloadFile->setAutoRemove(false);
                break;
            }
            // QFile::rename never overwrites. An existing destination is an
            // expected race; any other error should be surfaced immediately.
            if (!QFileInfo::exists(candidate)) {
                error = QStringLiteral("无法保存安装包到下载目录");
                break;
            }
        }
        if (error.isEmpty() && m_downloadPath.isEmpty()) {
            error = QStringLiteral("下载目录中同名安装包过多");
        }
    }

    if (m_downloadCancelled || !error.isEmpty()) {
        if (m_downloadFile) m_downloadFile->close();
        if (m_downloadCancelled) {
            emit statusMessage(QStringLiteral("已取消更新下载"), 3000);
        } else {
            QMessageBox::warning(m_parentWindow, QStringLiteral("下载更新"),
                                 error);
            emit statusMessage(QStringLiteral("更新下载失败"), 4000);
        }
        resetDownload();
        return;
    }

    const QString completedPath = m_downloadPath;
    const QString completedVersion = m_downloadVersion;
    resetDownload();
    emit statusMessage(QStringLiteral("更新包已下载并通过校验"), 5000);
    const auto choice = QMessageBox::question(
        m_parentWindow, QStringLiteral("更新已就绪"),
        QStringLiteral("StarProcessor %1 已下载并通过 SHA-256 校验。\n\n"
                       "是否现在打开安装包？\n%2")
            .arg(completedVersion, completedPath),
        QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Open);
    if (choice == QMessageBox::Open) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(completedPath));
    }
}

void UpdateManager::showCheckError(const QString& detail) {
    emit statusMessage(QStringLiteral("检查更新失败"), 3500);
    if (m_manifestInteractive) {
        QMessageBox::warning(m_parentWindow, QStringLiteral("检查更新"), detail);
    }
}

void UpdateManager::resetDownload() {
    m_downloadFile.reset();
    m_downloadHash.reset();
    m_downloadPackage = {};
    m_downloadPath.clear();
    m_downloadDirectory.clear();
    m_downloadVersion.clear();
    m_downloadFailure.clear();
    m_downloadedBytes = 0;
    m_downloadCancelled = false;
}
