#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace UiAssets {

// A deliberately small operational icon vocabulary. Workflow, scenes and
// parameter sections use typography instead of decorative pictograms.
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
    Brush,
    Undo,
    Reset,
    Done,
    ChevronLeft,
    ArrowUp,
    NewFolder,
    ListView,
    DetailView,
    ChevronRight,
    More,
    Stop
};

QIcon icon(Glyph glyph, const QColor& color, int logicalSize = 20);
QIcon controlIcon(Glyph glyph, const QColor& ink, int logicalSize = 20);
QPixmap logoMark(int logicalSize = 36);
QIcon appIcon();

} // namespace UiAssets
