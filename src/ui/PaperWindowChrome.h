#pragma once

class QMainWindow;
class QWidget;

namespace PaperWindowChrome {

// Install once on the GUI thread, after laying out the shared paper masthead.
// The masthead must span the top of the client area and paint (or inherit) the
// same paper as the page. Use its layout for content (e.g. 24, 0, 24, 0 margins
// and >= 56 logical px high); the chrome reserves room inside that layout.
// Keep in-window menus within or below the masthead. Leave layout stretch or
// another unoccupied area for dragging. Native/global menus are unchanged.
void install(QMainWindow* window, QWidget* masthead);

} // namespace PaperWindowChrome
