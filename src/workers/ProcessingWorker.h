#pragma once

#include "core/SkyGroundMask.h"

#include <QThread>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <vector>

class ProcessingWorker : public QThread {
    Q_OBJECT

public:
    struct Params {
        QString stackMethod = "average";
        double kappaValue = 2.5;
        bool dewarpEnabled = false;
        int dewarpStrength = 30;
        bool stretchEnabled = false;
        bool starReduceEnabled = false;
        int starReduceStrength = 50;
        QString outputFormat = "tiff16";
        QString outputPath;
        bool skyGroundSepEnabled = false;
        SkyGroundMask::Mode skyGroundMode = SkyGroundMask::AutoDetect;
        QString userMaskPath;
        int featherRadius = 20;
    };

    ProcessingWorker(const QStringList& files, const QString& referenceFrame,
                     const Params& params, QObject* parent = nullptr);

    // These accessors are called by the GUI only after QThread::finished.
    std::vector<uint16_t> takeStackedData();
    int stackedWidth() const { return m_width; }
    int stackedHeight() const { return m_height; }
    int stackedFrameCount() const { return m_frameCount; }
    QString errorString() const { return m_errorString; }
    bool wasCancelled() const { return m_wasCancelled; }

    void requestCancel();

signals:
    void progress(int value);
    void stageMessage(const QString& message);

protected:
    void run() override;

private:
    bool stopIfCancelled();

    QStringList m_files;
    QString m_referenceFrame;
    Params m_params;
    std::vector<uint16_t> m_stackedData;
    int m_width = 0;
    int m_height = 0;
    int m_frameCount = 0;
    QString m_errorString;
    std::atomic<bool> m_cancelRequested{false};
    bool m_wasCancelled = false;
};
