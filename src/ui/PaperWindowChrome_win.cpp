#include <QtCore/qt_windows.h>

#include "PaperWindowChrome_p.h"

#include <QWindow>
#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace PaperWindowChrome::detail {
namespace {

constexpr UINT_PTR kSubclassId = 1;
constexpr UINT kDefaultDpi = 96;

UINT dpiFor(HWND hwnd) {
    const UINT dpi = GetDpiForWindow(hwnd);
    return dpi ? dpi : kDefaultDpi;
}

int resizeFrame(HWND hwnd, int metric) {
    const UINT dpi = dpiFor(hwnd);
    return GetSystemMetricsForDpi(metric, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

int captionHeight(HWND hwnd) {
    return GetSystemMetricsForDpi(SM_CYCAPTION, dpiFor(hwnd)) + resizeFrame(hwnd, SM_CYSIZEFRAME);
}

LONG_PTR styleFor(HWND hwnd) {
    return GetWindowLongPtrW(hwnd, GWL_STYLE);
}

bool hasCaption(HWND hwnd) {
    return (styleFor(hwnd) & WS_CAPTION) == WS_CAPTION;
}

RECT clientInScreen(HWND hwnd) {
    RECT rect{};
    GetClientRect(hwnd, &rect);
    POINT points[] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
    MapWindowPoints(hwnd, nullptr, points, 2);
    return {points[0].x, points[0].y, points[1].x, points[1].y};
}

class WindowsChrome final : public Chrome {
public:
    using Chrome::Chrome;
    ~WindowsChrome() override {
        if (m_hwnd) RemoveWindowSubclass(m_hwnd, subclass, kSubclassId);
    }

private:
    static LRESULT CALLBACK subclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR, DWORD_PTR data) {
        auto* chrome = reinterpret_cast<WindowsChrome*>(data);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, subclass, kSubclassId);
            chrome->m_hwnd = nullptr;
            chrome->m_extended = false;
            chrome->m_extensionTop = 0;
            chrome->scheduleSync();
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }
        return chrome->handleMessage(hwnd, message, wParam, lParam);
    }

    LRESULT handleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ACTIVATE:
        case WM_DPICHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
        case WM_STYLECHANGED:
        case WM_THEMECHANGED:
        case WM_SETTINGCHANGE:
            m_extensionTop = -1;
            scheduleSync();
            break;
        case WM_WINDOWPOSCHANGED:
        case WM_SHOWWINDOW:
            scheduleSync();
            break;
        default:
            break;
        }

        if (!m_extended || !hasCaption(hwnd)) return DefSubclassProc(hwnd, message, wParam, lParam);
        if (message == WM_NCCALCSIZE && lParam) {
            RECT& client = wParam ? reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0]
                                  : *reinterpret_cast<RECT*>(lParam);
            const LONG frameTop = client.top;
            DefSubclassProc(hwnd, message, wParam, lParam);
            // Keep the native sides and bottom. At maximize, keep the resize
            // frame inset too so the topmost part of the paper is not clipped.
            client.top = IsZoomed(hwnd) ? std::min(client.top, frameTop + resizeFrame(hwnd, SM_CYSIZEFRAME))
                                        : frameTop;
            return 0;
        }

        if ((message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK) ||
            message == WM_NCMOUSELEAVE || message == WM_NCHITTEST || message == WM_NCACTIVATE) {
            LRESULT result = 0;
            if (DwmDefWindowProc(hwnd, message, wParam, lParam, &result)) return result;
        }
        if (message == WM_NCHITTEST) return hitTest(hwnd, wParam, lParam);
        if (message == WM_NCRBUTTONUP && wParam == HTCAPTION)
            return DefWindowProcW(hwnd, message, wParam, lParam);

        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT hitTest(HWND hwnd, WPARAM wParam, LPARAM lParam) const {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT client = clientInScreen(hwnd);
        if (!PtInRect(&client, point)) return DefSubclassProc(hwnd, WM_NCHITTEST, wParam, lParam);
        if (!window()) return HTCLIENT;

        RECT frame{};
        GetWindowRect(hwnd, &frame);
        if (!IsZoomed(hwnd) && (styleFor(hwnd) & WS_THICKFRAME) &&
            window()->minimumHeight() < window()->maximumHeight() &&
            point.y < frame.top + resizeFrame(hwnd, SM_CYSIZEFRAME)) {
            const int corner = 2 * resizeFrame(hwnd, SM_CXSIZEFRAME);
            if (window()->minimumWidth() < window()->maximumWidth()) {
                if (point.x < frame.left + corner) return HTTOPLEFT;
                if (point.x >= frame.right - corner) return HTTOPRIGHT;
            }
            return HTTOP;
        }

        const qreal dpr = window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1.0;
        const QPointF logical((point.x - client.left) / dpr, (point.y - client.top) / dpr);
        return isDragPoint(logical) ? HTCAPTION : HTCLIENT;
    }

    void updateExtension(bool extend) {
        const int top = extend ? captionHeight(m_hwnd) : 0;
        if (m_extensionTop == top && m_extended == extend) return;

        const bool wasExtended = m_extended;
        const MARGINS margins{0, 0, top, 0};
        const HRESULT result = DwmExtendFrameIntoClientArea(m_hwnd, &margins);
        m_extended = extend && SUCCEEDED(result);
        m_extensionTop = m_extended ? top : 0;
        if (wasExtended != m_extended) {
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    QRectF captionInClient() const {
        RECT frame{}, buttons{};
        GetWindowRect(m_hwnd, &frame);
        if (!IsWindowVisible(m_hwnd) ||
            FAILED(DwmGetWindowAttribute(m_hwnd, DWMWA_CAPTION_BUTTON_BOUNDS, &buttons, sizeof(buttons))) ||
            IsRectEmpty(&buttons) || buttons.right > frame.right - frame.left ||
            buttons.top >= captionHeight(m_hwnd)) {
            // DWM has no caption metrics before the first show (or while the
            // window is minimized). Reserve a conservative DPI-scaled cluster.
            const LONG frameWidth = frame.right - frame.left;
            const int buttonWidth = std::max(GetSystemMetricsForDpi(SM_CXSIZE, dpiFor(m_hwnd)),
                                             MulDiv(48, dpiFor(m_hwnd), kDefaultDpi));
            const LONG extent = std::min(frameWidth, static_cast<LONG>(3 * buttonWidth +
                                                                     resizeFrame(m_hwnd, SM_CXSIZEFRAME)));
            const bool rtl = (GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;
            buttons = {rtl ? 0 : frameWidth - extent, 0,
                       rtl ? extent : frameWidth, captionHeight(m_hwnd)};
        }
        const RECT client = clientInScreen(m_hwnd);
        const qreal dpr = window()->windowHandle() ? window()->windowHandle()->devicePixelRatio() : 1.0;
        return QRectF((frame.left + buttons.left - client.left) / dpr,
                      (frame.top + buttons.top - client.top) / dpr,
                      (buttons.right - buttons.left) / dpr, (buttons.bottom - buttons.top) / dpr)
            .adjusted(-2, -2, 2, 2);
    }

    void sync() override {
        const auto hwnd = reinterpret_cast<HWND>(nativeId());
        if (m_hwnd != hwnd) {
            if (m_hwnd) {
                updateExtension(false);
                RemoveWindowSubclass(m_hwnd, subclass, kSubclassId);
                m_hwnd = nullptr;
            }
            if (hwnd && SetWindowSubclass(hwnd, subclass, kSubclassId, reinterpret_cast<DWORD_PTR>(this)))
                m_hwnd = hwnd;
        }

        HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW), 0, nullptr};
        const bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0)
                                  && (contrast.dwFlags & HCF_HIGHCONTRASTON);
        if (m_hwnd) {
            updateExtension(!window()->isFullScreen() && hasCaption(m_hwnd) && !GetMenu(m_hwnd) &&
                            !highContrast && coversClientTop());
        }
        if (m_extended && IsIconic(m_hwnd)) return;
        setSafeLayout(m_extended ? captionInClient() : QRectF());
    }

    HWND m_hwnd = nullptr;
    bool m_extended = false;
    int m_extensionTop = 0;
};

} // namespace

Chrome* makeWindowsChrome(QMainWindow* window, QWidget* masthead) {
    return new WindowsChrome(window, masthead);
}

} // namespace PaperWindowChrome::detail
