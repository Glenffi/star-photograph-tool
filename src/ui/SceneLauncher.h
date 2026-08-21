#pragma once

#include <QWidget>
#include "ProcessingScene.h"

class SceneLauncher : public QWidget {
    Q_OBJECT
public:
    explicit SceneLauncher(QWidget* parent = nullptr);

signals:
    void sceneSelected(ProcessingScene scene);
};
