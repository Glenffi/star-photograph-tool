#pragma once

#include <QMetaType>
#include <QWidget>

enum class ProcessingScene {
    SingleFrame,
    Nightscape,
    DeepSky,
    SkyGround,
    StarTrail,
    Timelapse
};

class SceneLauncher : public QWidget {
    Q_OBJECT
public:
    explicit SceneLauncher(QWidget* parent = nullptr);

signals:
    void sceneSelected(ProcessingScene scene);
};

Q_DECLARE_METATYPE(ProcessingScene)
