#pragma once

#include <QThread>
#include <QString>

#include <cstdint>
#include <vector>

class MaskPreviewWorker : public QThread {
    Q_OBJECT

public:
    MaskPreviewWorker(const QString& filePath, int featherRadius,
                      QObject* parent = nullptr);

    std::vector<uint8_t> takeMask();
    int width() const { return m_width; }
    int height() const { return m_height; }
    QString errorString() const { return m_error; }

protected:
    void run() override;

private:
    QString m_filePath;
    int m_featherRadius = 0;
    std::vector<uint8_t> m_mask;
    int m_width = 0;
    int m_height = 0;
    QString m_error;
};
