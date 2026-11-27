#include "PaperWindowChrome_p.h"

#import <AppKit/AppKit.h>

#include <QTimer>
#include <QWindow>
#include <QtMath>

namespace PaperWindowChrome::detail {
namespace {

constexpr CGFloat kResizeGuardPoints = 6;

qreal logicalPerPoint(NSView* view, const QWindow* window) {
    // AppKit uses points, not backing pixels. Account for explicit Qt scale
    // factors as well as Retina when translating to QWidget coordinates.
    return window ? view.window.backingScaleFactor / window->devicePixelRatio() : 1.0;
}

QRectF toClient(NSView* view, NSRect rect, qreal scale) {
    const NSRect bounds = view.bounds;
    const CGFloat top = view.isFlipped ? NSMinY(rect) - NSMinY(bounds)
                                      : NSMaxY(bounds) - NSMaxY(rect);
    return {(NSMinX(rect) - NSMinX(bounds)) * scale, top * scale,
            NSWidth(rect) * scale, NSHeight(rect) * scale};
}

class CocoaChrome final : public Chrome {
public:
    using Chrome::Chrome;
    ~CocoaChrome() override { stopObserving(); }

private:
    void stopObserving() {
        if (m_mouseMonitor) [NSEvent removeMonitor:m_mouseMonitor];
        if (m_observer) [NSNotificationCenter.defaultCenter removeObserver:m_observer];
        m_mouseMonitor = nil;
        m_observer = nil;
        m_windowIdentity = nullptr;
        ++m_fullScreenGeneration;
        m_fullScreenTransition = false;
        m_previousDragPress = false;
        m_restoreFrame = NSZeroRect;
        m_filledFrame = NSZeroRect;
    }

    void observe(NSWindow* native) {
        m_windowIdentity = (__bridge void*)native;
        m_observer = [NSNotificationCenter.defaultCenter
            addObserverForName:nil object:native queue:nil usingBlock:^(NSNotification* note) {
                NSString* name = note.name;
                if ([name isEqualToString:NSWindowWillEnterFullScreenNotification] ||
                    [name isEqualToString:NSWindowWillExitFullScreenNotification]) {
                    m_fullScreenTransition = true;
                    // AppKit's failure callbacks are delegate-only. Do not
                    // leave the chrome suspended if a transition is cancelled.
                    QTimer::singleShot(2000, this, [this, generation = ++m_fullScreenGeneration] {
                        if (generation != m_fullScreenGeneration) return;
                        m_fullScreenTransition = false;
                        scheduleSync();
                    });
                } else if ([name isEqualToString:NSWindowDidEnterFullScreenNotification] ||
                           [name isEqualToString:NSWindowDidExitFullScreenNotification]) {
                    ++m_fullScreenGeneration;
                    m_fullScreenTransition = false;
                    scheduleSync();
                } else if ([name isEqualToString:NSWindowDidResizeNotification] ||
                           [name isEqualToString:NSWindowDidChangeScreenNotification] ||
                           [name isEqualToString:NSWindowDidChangeBackingPropertiesNotification]) {
                    scheduleSync();
                }
            }];
        m_mouseMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
            handler:^NSEvent*(NSEvent* event) { return mouseDown(event); }];
    }

    void restoreTitleBar(NSWindow* native) {
        if (m_applied && native) {
            if (!m_hadFullSizeContent) native.styleMask &= ~NSWindowStyleMaskFullSizeContentView;
            native.titleVisibility = m_titleVisibility;
            native.titlebarAppearsTransparent = m_titlebarWasTransparent;
            native.titlebarSeparatorStyle = m_separator;
        }
        m_applied = false;
        if (m_disabledQtSafeArea) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
            window()->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea);
#endif
            m_disabledQtSafeArea = false;
        }
    }

    void sync() override {
        auto* view = (__bridge NSView*)reinterpret_cast<void*>(nativeId());
        NSWindow* native = view.window;
        if (m_windowIdentity != (__bridge void*)native) {
            stopObserving();
            restoreTitleBar(nil);
            if (native) observe(native);
        }
        const bool fullScreen = (native.styleMask & NSWindowStyleMaskFullScreen) != 0;
        if (m_fullScreenTransition) return;
        if (!native || !coversClientTop()) {
            if (!fullScreen) restoreTitleBar(native);
            setSafeLayout();
            return;
        }

        if (!fullScreen && (native.styleMask & NSWindowStyleMaskTitled)) {
            if (!m_applied) {
                m_hadFullSizeContent = (native.styleMask & NSWindowStyleMaskFullSizeContentView) != 0;
                m_titleVisibility = native.titleVisibility;
                m_titlebarWasTransparent = native.titlebarAppearsTransparent;
                m_separator = native.titlebarSeparatorStyle;
                m_applied = true;
            }
            if (!(native.styleMask & NSWindowStyleMaskFullSizeContentView))
                native.styleMask |= NSWindowStyleMaskFullSizeContentView;
            if (!native.titlebarAppearsTransparent) native.titlebarAppearsTransparent = YES;
            if (native.titleVisibility != NSWindowTitleHidden) native.titleVisibility = NSWindowTitleHidden;
            if (native.titlebarSeparatorStyle != NSTitlebarSeparatorStyleNone)
                native.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
        // Keep the layout background under the title bar, not underneath the
        // top inset that Qt ordinarily gives to the whole QMainWindow layout.
        if ((m_applied || fullScreen) && window()->testAttribute(Qt::WA_ContentsMarginsRespectsSafeArea)) {
            m_disabledQtSafeArea = true;
            window()->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
        }
#endif

        const qreal scale = logicalPerPoint(view, window()->windowHandle());
        QRectF caption;
        for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
            NSButton* button = [native standardWindowButton:kind];
            if (!button || button.window != native || button.hiddenOrHasHiddenAncestor) continue;
            const QRectF bounds = toClient(view, [button convertRect:button.bounds toView:view], scale);
            if (bounds.intersects(QRectF(window()->rect()))) caption |= bounds;
        }
        if (!caption.isEmpty()) caption.adjust(-2, -2, 2, 2);
        QMargins insets;
        if (fullScreen) {
            const NSEdgeInsets safe = view.safeAreaInsets;
            insets = QMargins(qCeil(safe.left * scale), qCeil(safe.top * scale),
                              qCeil(safe.right * scale), 0);
        }
        setSafeLayout(caption, insets, fullScreen ? 0 : qCeil(kResizeGuardPoints * scale));
    }

    NSEvent* mouseDown(NSEvent* event) {
        const bool previousDragPress = m_previousDragPress;
        m_previousDragPress = false;
        NSWindow* native = event.window;
        if (!m_applied || m_fullScreenTransition || !native ||
            (__bridge void*)native != m_windowIdentity || !native.movable || native.attachedSheet ||
            (native.styleMask & NSWindowStyleMaskFullScreen) ||
            (event.modifierFlags & NSEventModifierFlagControl)) return event;

        auto* view = (__bridge NSView*)reinterpret_cast<void*>(nativeId());
        if (view.window != native) return event;
        const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
        const qreal scale = logicalPerPoint(view, window()->windowHandle());
        const QRectF client = toClient(view, NSMakeRect(point.x, point.y, 0, 0), scale);
        // Leave the native resize edge and native controls out of the drag area.
        if (client.y() < kResizeGuardPoints * scale || !isDragPoint(client.topLeft())) return event;
        NSView* frame = view.superview;
        NSView* parent = frame.superview;
        const NSPoint framePoint = parent ? [parent convertPoint:event.locationInWindow fromView:nil]
                                          : event.locationInWindow;
        NSView* hit = [frame hitTest:framePoint];
        if (!hit || (hit != view && ![hit isDescendantOf:view])) return event;

        if (event.clickCount == 1) {
            m_previousDragPress = true;
            if (!(event.modifierFlags & NSEventModifierFlagCommand) && !native.keyWindow)
                [native makeKeyAndOrderFront:nil];
            [native performWindowDragWithEvent:event];
        } else if (event.clickCount == 2 && previousDragPress) {
            doubleClickTitle(native);
        }
        return nil;
    }

    void doubleClickTitle(NSWindow* native) {
        // AppKit no longer handles title clicks over the full-size content
        // view. Read the current Dock preference without writing any defaults.
        NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
        NSString* action = [defaults stringForKey:@"AppleActionOnDoubleClick"];
        if (!action) {
            if ([defaults boolForKey:@"AppleMiniaturizeOnDoubleClick"]) action = @"Minimize";
            else if (@available(macOS 15.0, *)) action = @"Fill";
            else action = @"Zoom";
        }
        if ([action isEqualToString:@"Minimize"]) {
            [native performMiniaturize:nil];
        } else if ([action isEqualToString:@"Zoom"] || [action isEqualToString:@"Maximize"]) {
            [native performZoom:nil];
        } else if ([action isEqualToString:@"Fill"] && native.screen &&
                   (native.styleMask & NSWindowStyleMaskResizable)) {
            if (!NSIsEmptyRect(m_restoreFrame) && NSEqualRects(native.frame, m_filledFrame)) {
                [native setFrame:[native constrainFrameRect:m_restoreFrame toScreen:native.screen]
                         display:YES animate:YES];
                m_restoreFrame = NSZeroRect;
            } else {
                m_restoreFrame = native.frame;
                [native setFrame:[native constrainFrameRect:native.screen.visibleFrame toScreen:native.screen]
                         display:YES animate:YES];
                m_filledFrame = native.frame;
            }
        }
    }

    id m_observer = nil;
    id m_mouseMonitor = nil;
    void* m_windowIdentity = nullptr;
    bool m_applied = false;
    bool m_hadFullSizeContent = false;
    bool m_titlebarWasTransparent = false;
    bool m_disabledQtSafeArea = false;
    bool m_fullScreenTransition = false;
    unsigned m_fullScreenGeneration = 0;
    bool m_previousDragPress = false;
    NSWindowTitleVisibility m_titleVisibility = NSWindowTitleVisible;
    NSTitlebarSeparatorStyle m_separator = NSTitlebarSeparatorStyleAutomatic;
    NSRect m_restoreFrame = NSZeroRect;
    NSRect m_filledFrame = NSZeroRect;
};

} // namespace

Chrome* makeCocoaChrome(QMainWindow* window, QWidget* masthead) {
    return new CocoaChrome(window, masthead);
}

} // namespace PaperWindowChrome::detail
