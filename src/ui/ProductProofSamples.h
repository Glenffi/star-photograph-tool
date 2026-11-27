#pragma once

#include "ProcessingScene.h"

#include <QImage>
#include <QString>

namespace ProductProofSamples {

int count(ProcessingScene scene);
// Frames are zero-based and chronological. Invalid indices return a null image.
QImage frame(ProcessingScene scene, int index = 0);
QString credit(ProcessingScene scene);

}  // namespace ProductProofSamples
