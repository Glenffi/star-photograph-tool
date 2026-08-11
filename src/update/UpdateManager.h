#pragma once

#include "core/UpdateManifest.h"

#include <QObject>
#include <QPointer>
#include <QUrl>

#include <memory>

class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressDialog;
class QTemporaryFile;
class QWidget;

// Owns the complete update-check workflow. Packages are downloaded to the
// user's Downloads folder, verified, and only then offered to the operating
// system. The application never replaces or launches unverified bytes.
class UpdateManager final : public QObject {
    Q_OBJECT

public:
    explicit UpdateManager(QWidget* parentWindow);
    ~UpdateManager() override;

    void scheduleAutomaticCheck();
    void checkForUpdates(bool interactive = true);

signals:
    void statusMessage(const QString& message, int timeoutMs);

private:
    QString platformKey() const;
    bool trustedPackageUrl(const QUrl& url) const;
    void handleManifestFinished();
    void consumeManifestData();
    void offerUpdate(const UpdateManifest& manifest);
    void startDownload(const UpdatePackage& package,
                       const QString& version);
    void consumeDownloadData();
    void handleDownloadFinished();
    void showCheckError(const QString& detail);
    void resetDownload();

    QWidget* m_parentWindow = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QNetworkReply* m_manifestReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
    QPointer<QProgressDialog> m_progressDialog;
    std::unique_ptr<QTemporaryFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;
    QUrl m_manifestUrl;
    UpdatePackage m_downloadPackage;
    QString m_downloadPath;
    QString m_downloadDirectory;
    QString m_downloadVersion;
    QString m_downloadFailure;
    QByteArray m_manifestPayload;
    QString m_manifestFailure;
    qint64 m_downloadedBytes = 0;
    bool m_manifestInteractive = false;
    bool m_downloadCancelled = false;
    bool m_shuttingDown = false;
};
