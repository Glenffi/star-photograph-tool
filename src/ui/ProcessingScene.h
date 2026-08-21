#pragma once

#include <QMetaType>

enum class ProcessingScene {
    SingleFrame,
    Nightscape,
    DeepSky,
    SkyGround,
    StarTrail,
    Timelapse
};

Q_DECLARE_METATYPE(ProcessingScene)
