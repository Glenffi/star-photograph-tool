#pragma once

#include <QImage>
#include <QString>
#include <QThread>

class HistoryPreviewWorker final : public QThread {
    Q_OBJECT

public:
    explicit HistoryPreviewWorker(const QString& path,
                                  QObject* parent = nullptr);

signals:
    void loaded(const QString& path, const QImage& preview);
    void failed(const QString& path, const QString& reason);

protected:
    void run() override;

private:
    QString m_path;
};
