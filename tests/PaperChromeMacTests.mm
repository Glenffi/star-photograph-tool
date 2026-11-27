// Standalone Cocoa smoke (from the repo root; point qt at your Qt lib folder):
// qt=/opt/homebrew/opt/qt@6/lib
// xcrun clang++ -std=c++17 -mmacosx-version-min=14.0 -Isrc -F"$qt" \
//   -I"$qt/QtCore.framework/Headers" -I"$qt/QtGui.framework/Headers" \
//   -I"$qt/QtWidgets.framework/Headers" tests/PaperChromeMacTests.mm \
//   src/ui/PaperWindowChrome.cpp src/ui/PaperWindowChrome_mac.mm \
//   -framework QtWidgets -framework QtGui -framework QtCore \
//   -framework AppKit -framework ApplicationServices -o /tmp/paper-chrome-smoke
// Run in a logged-in GUI session; --input also needs the ability to post mouse events.
// --capture-dir uses window-only ScreenCaptureKit (macOS 14.4+).
// /tmp/paper-chrome-smoke --input [--capture-dir /tmp/paper-chrome-captures]

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "ui/PaperWindowChrome.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColorSpace>
#include <QDeadlineTimer>
#include <QDir>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QWindow>
#include <atomic>
#include <cmath>
#include <dlfcn.h>
#include <functional>
#include <memory>
#include <thread>

namespace {

const QColor kPaper(231, 222, 200);
constexpr unsigned short kAnsiJKeyCode = 38;
int failures = 0;

void check(bool ok, const char* message) {
    if (!ok) {
        qCritical("FAIL: %s", message);
        ++failures;
    }
}

bool waitFor(const std::function<bool()>& ready, int timeout = 4000) {
    QDeadlineTimer deadline(timeout);
    do {
        QCoreApplication::processEvents();
        if (ready()) return true;
        QThread::msleep(10);
    } while (!deadline.hasExpired());
    return false;
}

void settle(int millis = 250) {
    waitFor([] { return false; }, millis);
}

class Paper final : public QWidget {
public:
    Paper() { setAttribute(Qt::WA_OpaquePaintEvent); }

private:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QLinearGradient grain(0, 0, width(), 0);
        grain.setColorAt(0, QColor(224, 212, 183));
        grain.setColorAt(0.55, QColor(238, 225, 195));
        grain.setColorAt(1, QColor(228, 215, 188));
        painter.fillRect(rect(), grain);
    }
};

class TestWindow final : public QMainWindow {
public:
    int closeRequests = 0;
    bool canClose = false;

private:
    void closeEvent(QCloseEvent* event) override {
        ++closeRequests;
        event->setAccepted(canClose);
    }
};

NSView* platformView(const QMainWindow& window) {
    return (__bridge NSView*)reinterpret_cast<void*>(window.internalWinId());
}

NSButton* button(NSWindow* native, NSWindowButton kind) {
    return [native standardWindowButton:kind];
}

void verifyExtension(const QMainWindow& window, const QWidget* masthead, bool fullscreen = false) {
    NSView* view = platformView(window);
    NSWindow* native = view.window;
    const NSRect viewOnScreen = [native convertRectToScreen:[view convertRect:view.bounds toView:nil]];
    check(masthead->mapTo(&window, QPoint()) == QPoint() && masthead->width() == window.width(),
          "the Qt masthead reaches the top and both edges of the client");
    check(native && std::abs(NSMaxY(viewOnScreen) - NSMaxY(native.frame)) <= 1,
          "Qt content reaches the top edge of the real NSWindow");
    if (fullscreen) return;

    check(native.titlebarAppearsTransparent && native.titleVisibility == NSWindowTitleHidden &&
          native.titlebarSeparatorStyle == NSTitlebarSeparatorStyleNone &&
          (native.styleMask & NSWindowStyleMaskFullSizeContentView),
          "transparent full-size title bar with no native separator or duplicate title");
    for (auto kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton* control = button(native, kind);
        check(control && control.window == native && !control.hiddenOrHasHiddenAncestor &&
              control.target && control.action, "Cocoa owns a visible, wired standard window button");
        if (!control) continue;
        const NSRect bounds = [control convertRect:control.bounds toView:view];
        const qreal scale = native.backingScaleFactor / window.devicePixelRatioF();
        const QMargins margins = masthead->layout()->contentsMargins();
        check(NSMidX(bounds) < NSWidth(view.bounds) / 2 ? margins.left() >= NSMaxX(bounds) * scale
              : margins.right() >= (NSWidth(view.bounds) - NSMinX(bounds)) * scale,
              "the masthead layout leaves room for the real button bounds");
    }
}

QImage captureOnlyWindow(NSWindow* native) {
    if (@available(macOS 14.4, *)) {
        // Keep ScreenCaptureKit optional for builds that only exercise input.
        static void* const captureKit = dlopen(
            "/System/Library/Frameworks/ScreenCaptureKit.framework/ScreenCaptureKit", RTLD_LAZY | RTLD_LOCAL);
        if (!captureKit) return {};
        Class shareable = NSClassFromString(@"SCShareableContent");
        Class contentFilter = NSClassFromString(@"SCContentFilter");
        Class configuration = NSClassFromString(@"SCStreamConfiguration");
        Class screenshotManager = NSClassFromString(@"SCScreenshotManager");
        // Capture only our WindowServer window, even when it is inactive or
        // covered. Never sample the desktop matched to the widget's bounds.
        struct Capture {
            std::atomic<bool> ready{false};
            QImage image;
        };
        const auto capture = std::make_shared<Capture>();
        const auto id = static_cast<CGWindowID>(native.windowNumber);
        const auto pid = NSProcessInfo.processInfo.processIdentifier;
        const NSRect frame = native.frame;
        const qreal dpr = native.backingScaleFactor;
        [shareable getCurrentProcessShareableContentWithCompletionHandler:
            ^(SCShareableContent* content, NSError* error) {
                for (SCWindow* candidate in content.windows) {
                    if (candidate.windowID != id || candidate.owningApplication.processID != pid) continue;
                    SCContentFilter* filter = [[contentFilter alloc] initWithDesktopIndependentWindow:candidate];
                    SCStreamConfiguration* config = [[configuration alloc] init];
                    config.width = qRound(NSWidth(frame) * dpr);
                    config.height = qRound(NSHeight(frame) * dpr);
                    config.ignoreShadowsSingleWindow = YES;
                    config.showsCursor = NO;
                    [screenshotManager captureImageWithFilter:filter configuration:config
                        completionHandler:^(CGImageRef image, NSError* captureError) {
                            if (image) {
                                auto* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
                                NSData* data = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                                capture->image = QImage::fromData(QByteArrayView(
                                    static_cast<const char*>(data.bytes), qsizetype(data.length)), "PNG");
#if !__has_feature(objc_arc)
                                [bitmap release];
#endif
                            } else {
                                qWarning("Window capture failed: %s", captureError.localizedDescription.UTF8String);
                            }
                            capture->ready = true;
                        }];
#if !__has_feature(objc_arc)
                    [config release];
                    [filter release];
#endif
                    return;
                }
                qWarning("Smoke window not available for window-only capture: %s",
                         error ? error.localizedDescription.UTF8String : "window not found");
                capture->ready = true;
            }];
        if (waitFor([&] { return capture->ready.load(); })) return capture->image;
    } else {
        qWarning("The optional window-only capture requires macOS 14.4 or later");
    }
    return {};
}

void checkPixels(const QMainWindow& window, const QString& path, int pageY = 79) {
    QImage shot = captureOnlyWindow(platformView(window).window);
    check(!shot.isNull(), "capture real window pixels (not QWidget::grab)");
    if (shot.isNull()) return;
    if (!shot.colorSpace().isValid()) shot.setColorSpace(QColorSpace::SRgb);
    shot.convertToColorSpace(QColorSpace::SRgb);
    check(shot.save(path), "write window-only capture");
    const qreal scale = qreal(shot.height()) / window.height();
    const QColor nearControls = shot.pixelColor(shot.width() * 2 / 5, qRound(12 * scale));
    const QColor page = shot.pixelColor(shot.width() * 2 / 5, qRound(pageY * scale));
    check(nearControls.red() > 205 && nearControls.green() > 193 &&
          std::abs(nearControls.red() - page.red()) <= 5 &&
          std::abs(nearControls.green() - page.green()) <= 5 &&
          std::abs(nearControls.blue() - page.blue()) <= 5,
          "pixels in the native title area and page are the same paper");
}

CGPoint screenPoint(NSWindow* native, NSPoint at) {
    const NSPoint screen = [native convertPointToScreen:at];
    return CGPointMake(screen.x, NSMaxY(NSScreen.screens.firstObject.frame) - screen.y);
}

CGPoint screenPoint(const QMainWindow& window, QPoint point) {
    NSView* view = platformView(window);
    NSWindow* native = view.window;
    const qreal scale = window.devicePixelRatioF() / native.backingScaleFactor;
    const NSPoint at = [view convertPoint:NSMakePoint(point.x() * scale, point.y() * scale) toView:nil];
    return screenPoint(native, at);
}

void postMouse(CGEventType type, CGPoint point, int clickCount, CGEventFlags flags) {
    CGEventRef event = CGEventCreateMouseEvent(nullptr, type, point, kCGMouseButtonLeft);
    CGEventSetIntegerValueField(event, kCGMouseEventClickState, clickCount);
    CGEventSetFlags(event, flags);
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}

void mouseSequence(NSWindow* target, CGPoint at, QPoint drag = {}, int clicks = 1,
                   CGEventFlags flags = 0) {
    const auto cocoa = NSMakePoint(at.x, NSMaxY(NSScreen.screens.firstObject.frame) - at.y);
    const bool hit = [NSWindow windowNumberAtPoint:cocoa belowWindowWithWindowNumber:0] == target.windowNumber;
    check(hit, "WindowServer input targets the smoke window");
    if (!hit) return;
    CGWarpMouseCursorPosition(at);
    std::thread sender([=] {
        for (int i = 1; i <= clicks; ++i) {
            postMouse(kCGEventLeftMouseDown, at, i, flags);
            QThread::msleep(45);
            const CGPoint end = CGPointMake(at.x + drag.x(), at.y + drag.y());
            if (!drag.isNull()) {
                postMouse(kCGEventLeftMouseDragged, end, i, flags);
                QThread::msleep(45);
            }
            postMouse(kCGEventLeftMouseUp, end, i, flags);
            QThread::msleep(90);
        }
    });
    settle(310);
    sender.join();
}

void clickWindowButton(NSWindow* native, NSWindowButton kind, bool realInput, CGEventFlags flags = 0) {
    NSButton* control = button(native, kind);
    if (!realInput) {
        [control performClick:nil];
        return;
    }
    const NSRect bounds = control.bounds;
    const NSPoint at = [control convertPoint:NSMakePoint(NSMidX(bounds), NSMidY(bounds)) toView:nil];
    mouseSequence(native, screenPoint(native, at), {}, 1, flags);
}

void postMenuShortcut() {
    for (bool pressed : {true, false}) {
        CGEventRef event = CGEventCreateKeyboardEvent(nullptr, kAnsiJKeyCode, pressed);
        CGEventSetFlags(event, kCGEventFlagMaskCommand | kCGEventFlagMaskShift);
        CGEventPostToPid(NSProcessInfo.processInfo.processIdentifier, event);
        CFRelease(event);
    }
}

enum class DoubleClick { Nothing, Minimize, Resize };

DoubleClick windowAction(NSWindow* window, NSRect before) {
    if (window.miniaturized) return DoubleClick::Minimize;
    return NSEqualRects(before, window.frame) ? DoubleClick::Nothing : DoubleClick::Resize;
}

DoubleClick stockDoubleClick() {
    QMainWindow reference;
    reference.setWindowTitle(QStringLiteral("Native title bar reference"));
    reference.resize(640, 300);
    reference.show();
    NSWindow* native = platformView(reference).window;
    [native makeKeyAndOrderFront:nil];
    settle();
    const NSRect before = native.frame;
    const auto at = CGPointMake(NSMidX(before), NSMaxY(NSScreen.screens.firstObject.frame) - NSMaxY(before) + 14);
    mouseSequence(native, at, {}, 2);
    settle();
    const auto action = windowAction(native, before);
    reference.hide();
    return action;
}

} // namespace

static int runSmoke(QApplication& app) {
    const auto args = app.arguments();
    const bool offscreen = app.platformName() == QStringLiteral("offscreen");
    if (!offscreen && app.platformName() != QStringLiteral("cocoa")) qFatal("Expected Cocoa or offscreen");
    if (!offscreen) [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    TestWindow window;
    window.setWindowTitle(QStringLiteral("Paper Chrome Smoke"));
    window.resize(720, 425);
    window.setMinimumSize(460, 250);
    auto* paper = new Paper;
    auto* sheet = new QVBoxLayout(paper);
    sheet->setContentsMargins(0, 0, 0, 0);
    sheet->setSpacing(0);
    auto* masthead = new QWidget(paper);
    masthead->setObjectName(QStringLiteral("paperMasthead"));
    masthead->setMinimumHeight(56);
    auto* row = new QHBoxLayout(masthead);
    row->setContentsMargins(24, 0, 24, 0);
    auto* brand = new QLabel(QStringLiteral("Paper / proof"), masthead);
    brand->setTextFormat(Qt::PlainText);
    row->addWidget(brand);
    row->addStretch();
    auto* command = new QPushButton(QStringLiteral("Paper action"), masthead);
    command->setFixedHeight(30);
    auto* tools = new QToolButton(masthead);
    tools->setFixedHeight(30);
    tools->setText(QStringLiteral("Tools"));
    auto* popup = new QMenu(tools);
    popup->addAction(QStringLiteral("Delivery"));
    tools->setMenu(popup);
    tools->setPopupMode(QToolButton::InstantPopup);
    row->addWidget(command);
    row->addWidget(tools);
    sheet->addWidget(masthead);
    sheet->addStretch();
    window.setCentralWidget(paper);
    int clicks = 0;
    int menuOpens = 0;
    int shortcuts = 0;
    QObject::connect(command, &QPushButton::clicked, [&] { ++clicks; });
    QObject::connect(popup, &QMenu::aboutToShow, [&] { ++menuOpens; });
    QMenuBar* menu = window.menuBar();
    if (!offscreen) {
        auto* action = menu->addMenu(QStringLiteral("&Proof"))->addAction(QStringLiteral("Proof shortcut"));
        action->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+J")));
        QObject::connect(action, &QAction::triggered, [&] { ++shortcuts; });
    }

    QCoreApplication::processEvents();
    const auto originalId = window.internalWinId();
    const auto originalFlags = window.windowFlags();
    PaperWindowChrome::install(&window, masthead);
    PaperWindowChrome::install(&window, masthead);
    QCoreApplication::processEvents();
    check(window.internalWinId() == originalId, "install does not eagerly create or replace a native window");
    window.show();
    settle();
    check(window.windowFlags() == originalFlags, "Qt window flags stay native and intact");

    if (offscreen) {
        check(row->contentsMargins() == QMargins(24, 0, 24, 0) && !masthead->autoFillBackground(),
              "offscreen preserves baseline margins and the existing paper painter");
        QMainWindow fallback;
        auto* unpainted = new QWidget;
        unpainted->setLayout(new QHBoxLayout);
        fallback.setCentralWidget(unpainted);
        PaperWindowChrome::install(&fallback, unpainted);
        QCoreApplication::processEvents();
        check(!fallback.internalWinId() && unpainted->autoFillBackground() &&
              unpainted->palette().color(QPalette::Window) == kPaper,
              "fallback supplies paper color without creating a fake native surface");
        if (!failures) qInfo("Paper chrome offscreen smoke passed");
        return failures ? 1 : 0;
    }

    NSWindow* native = platformView(window).window;
    check(native && native.visible && window.windowHandle()->isExposed(), "real Cocoa window is visible");
    if (!native) return 1;
    [NSApp activateIgnoringOtherApps:YES];
    [native makeKeyAndOrderFront:nil];
    settle();
    check(waitFor([&] { return native.keyWindow; }), "window can become key");
    check(menu->isNativeMenuBar(), "global menu is still native");
    const qreal backingScale = native.backingScaleFactor;
    verifyExtension(window, masthead);
    const QMargins aligned = row->contentsMargins();
    check(aligned.top() > 0 && aligned.bottom() > aligned.top() &&
          command->height() == 30 && tools->height() == 30,
          "masthead lifts toward the native caption without shrinking hit targets");
    const QSizePolicy commandPolicy = command->sizePolicy();
    command->setMaximumHeight(QWIDGETSIZE_MAX);
    command->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    check(waitFor([&] { return row->contentsMargins().top() == 0 && row->contentsMargins().bottom() == 0; }),
          "a growing interactive widget vetoes the lift");
    command->setFixedHeight(30);
    command->setSizePolicy(commandPolicy);
    check(waitFor([&] { return row->contentsMargins() == aligned; }), "the lift returns when the row has room");
    settle();
    const int captureArg = args.indexOf(QStringLiteral("--capture-dir"));
    const QString capture = captureArg >= 0 ? args.value(captureArg + 1) : QString();
    if (!capture.isEmpty()) {
        check(QDir().mkpath(capture), "create capture directory");
        checkPixels(window, QDir(capture).filePath(QStringLiteral("key.png")));
    }

    QMainWindow other;
    other.resize(220, 100);
    other.show();
    [platformView(other).window makeKeyAndOrderFront:nil];
    check(waitFor([&] { return !native.keyWindow; }), "an inactive real Cocoa window");
    verifyExtension(window, masthead);
    if (!capture.isEmpty()) checkPixels(window, QDir(capture).filePath(QStringLiteral("inactive.png")));
    other.hide();
    [native makeKeyAndOrderFront:nil];
    settle();

    const bool wantsInput = args.contains(QStringLiteral("--input"));
    const bool realInput = wantsInput && CGPreflightPostEventAccess();
    if (wantsInput) check(realInput, "permission to exercise real WindowServer mouse input");
    if (realInput) {
        const NSRect before = native.frame;
        mouseSequence(native, screenPoint(window, command->mapTo(&window, command->rect().center())));
        check(clicks == 1 && NSEqualRects(before, native.frame), "input clicks the Qt control without dragging");
        QTimer::singleShot(700, popup, &QWidget::hide);
        mouseSequence(native, screenPoint(window, tools->mapTo(&window, QPoint(tools->width() / 2, 1))));
        check(waitFor([&] { return menuOpens == 1 && !popup->isVisible(); }) && NSEqualRects(before, native.frame),
              "an in-window menu opens without dragging");
        postMenuShortcut();
        check(waitFor([&] { return shortcuts == 1; }), "native global menu shortcut works");
        mouseSequence(native, screenPoint(window, QPoint(window.width() / 2, masthead->height() + 32)), {63, 22});
        check(NSEqualRects(before, native.frame), "bare paper below the masthead does not drag");
        mouseSequence(native, screenPoint(window, QPoint(window.width() / 2, 21)), {63, 22});
        check(!NSEqualPoints(before.origin, native.frame.origin), "an empty masthead region moves the native window");
        const auto expected = stockDoubleClick();
        [native makeKeyAndOrderFront:nil];
        settle();
        const NSRect preDoubleClick = native.frame;
        const auto doubleClickMasthead = [&] {
            mouseSequence(native, screenPoint(window, QPoint(window.width() / 2, 21)), {}, 2);
            settle(450);
        };
        doubleClickMasthead();
        check(windowAction(native, preDoubleClick) == expected,
              "masthead double-click follows the same setting as a stock native title bar");
        if (expected == DoubleClick::Resize) {
            doubleClickMasthead();
            check(NSEqualRects(native.frame, preDoubleClick), "second double-click restores the window frame");
        }
        qInfo("Double-click reference: %s", expected == DoubleClick::Minimize ? "minimize" :
              expected == DoubleClick::Resize ? "resize" : "none");
        if (native.miniaturized) [native deminiaturize:nil];
        if (!NSEqualRects(native.frame, preDoubleClick)) [native setFrame:preDoubleClick display:YES];
        settle();
    }

    clickWindowButton(native, NSWindowMiniaturizeButton, realInput);
    check(waitFor([&] { return native.miniaturized && window.isMinimized(); }), "native yellow button minimizes");
    [native deminiaturize:nil];
    check(waitFor([&] { return !native.miniaturized && !window.isMinimized(); }), "restore from Dock");
    [native makeKeyAndOrderFront:nil];
    settle();

    const NSRect beforeZoom = native.frame;
    if (realInput) clickWindowButton(native, NSWindowZoomButton, true, kCGEventFlagMaskAlternate);
    else [native performZoom:nil];
    check(waitFor([&] { return !window.isFullScreen() && !NSEqualSizes(beforeZoom.size, native.frame.size); }),
          "Option-click on the native green button zooms without entering fullscreen");
    settle();
    verifyExtension(window, masthead);
    [native setFrame:beforeZoom display:YES];
    settle();

    clickWindowButton(native, NSWindowZoomButton, realInput);
    check(waitFor([&] { return (native.styleMask & NSWindowStyleMaskFullScreen) && window.isFullScreen(); }),
          "native green button enters native fullscreen");
    settle(700);
    if (window.isFullScreen()) {
        verifyExtension(window, masthead, true);
        if (!capture.isEmpty()) checkPixels(window, QDir(capture).filePath(QStringLiteral("fullscreen.png")), 115);
        [native toggleFullScreen:nil];
        check(waitFor([&] { return !(native.styleMask & NSWindowStyleMaskFullScreen) && !window.isFullScreen(); }),
              "exit native fullscreen");
        settle(700);
        verifyExtension(window, masthead);
    }

    clickWindowButton(native, NSWindowCloseButton, realInput);
    check(waitFor([&] { return window.closeRequests == 1; }) && window.isVisible(),
          "native red button respects a Qt close veto");
    window.canClose = true;
    clickWindowButton(native, NSWindowCloseButton, realInput);
    check(waitFor([&] { return window.closeRequests == 2 && !window.isVisible(); }), "native red button closes");

    if (!failures) qInfo("Paper chrome Cocoa smoke passed (Qt %s, Retina %.2f, Qt DPR %.2f)",
                         qVersion(), backingScale, window.devicePixelRatioF());
    return failures ? 1 : 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QTimer::singleShot(0, &app, [&] { app.exit(runSmoke(app)); });
    return app.exec();
}
