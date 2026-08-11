#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace UiAssets {

// A small, coherent icon vocabulary drawn by StarProcessor itself. Keeping the
// geometry in code avoids platform-specific system glyphs and external SVGs.
enum class Glyph {
    AddPhotos,
    Add,
    Scenes,
    Folder,
    Trash,
    Export,
    Play,
    Sliders,
    Info,
    Fit,
    ZoomIn,
    ZoomOut,
    Result,
    SingleFrame,
    Nightscape,
    DeepSky,
    SkyGround,
    StarTrail,
    Timelapse,
    Eyedropper,
    ChevronRight
};

QIcon icon(Glyph glyph, const QColor& color, int logicalSize = 18);
QPixmap logoMark(int logicalSize = 36);
QIcon appIcon();

} // namespace UiAssets
