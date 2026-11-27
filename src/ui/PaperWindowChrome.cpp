#include "PaperWindowChrome.h"
#include "PaperWindowChrome_p.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QtMath>
#include <algorithm>

namespace PaperWindowChrome::detail {
namespace {

bool isDecoration(const QWidget* widget) {
    if (widget->metaObject() == &QLabel::staticMetaObject) {
        const auto* label = static_cast<const QLabel*>(widget);
        return label->textFormat() == Qt::PlainText &&
               !label->textInteractionFlags().testAnyFlags(Qt::TextSelectableByMouse |
                                                           Qt::TextSelectableByKeyboard);
    }
    if (widget->metaObject() == &QFrame::staticMetaObject) {
        const auto shape = static_cast<const QFrame*>(widget)->frameShape();
        return shape == QFrame::VLine || shape == QFrame::HLine;
    }
    return false;
}

QMargins towardCaption(const QWidget& masthead, QHBoxLayout& row, QMargins margins,
                       qreal captionCenter, int resizeGuard) {
    const auto existing = row.contentsMargins();
    const int height = masthead.height();
    const int baseVertical = margins.top() + margins.bottom();
    const int available = height - baseVertical;
    const int top = std::max(margins.top(), resizeGuard);
    const int preferred = row.sizeHint().height() - existing.top() - existing.bottom();
    // Do not let our margins increase the row's preferred outer height or
    // preserve a temporary expansion after a tall control has gone away.
    const int budget = std::min(height, std::max(masthead.minimumHeight(), preferred + baseVertical));
    // Keep the entire preferred row and the unshifted height of its hit
    // targets. A vertically growing or nested control can veto the lift.
    int needed = preferred + 1;
    for (int i = 0; i < row.count(); ++i) {
        const auto* item = row.itemAt(i);
        if (item->isEmpty()) continue;
        const auto* widget = item->widget();
        if (!widget || widget->hasHeightForWidth()) return margins;
        if (!isDecoration(widget)) needed = std::max(needed, std::min(available, item->maximumSize().height()));
    }
    const int bottom = std::min(qRound(height + top - 2 * captionCenter), budget - top - needed);
    if (bottom - margins.bottom() > top - margins.top()) {
        margins.setTop(top);
        margins.setBottom(bottom);
    }
    return margins;
}

} // namespace

Chrome::Chrome(QMainWindow* window, QWidget* masthead)
    : QObject(window), m_window(window), m_masthead(masthead),
      m_baseMargins(masthead->layout()->contentsMargins()) {
    setObjectName(QStringLiteral("_nebPaperWindowChrome"));
}

void Chrome::start() {
    m_window->installEventFilter(this);
    m_masthead->installEventFilter(this);
    connect(m_masthead, &QObject::destroyed, this, &Chrome::scheduleSync);
    if (m_window->isVisible()) sync();
    scheduleSync();
}

WId Chrome::nativeId() const {
    // Unlike QWidget::winId(), this does not create a surface during teardown
    // or while the top-level widget is still being assembled.
    return m_window ? m_window->internalWinId() : WId{};
}

bool Chrome::coversClientTop() const {
    if (!m_window || !m_masthead || !m_masthead->isVisibleTo(m_window) ||
        m_masthead->window() != m_window) return false;
    return m_masthead->mapTo(m_window, QPoint()) == QPoint() &&
           m_masthead->width() >= m_window->width();
}

bool Chrome::isDragPoint(const QPointF& clientPoint) const {
    if (!m_window || !m_masthead || !m_masthead->isVisible() ||
        !m_masthead->isEnabled() || m_masthead->window() != m_window ||
        m_window->isFullScreen() || m_caption.contains(clientPoint) ||
        QApplication::activePopupWidget() || QApplication::activeModalWidget()) return false;

    // A layout spacer is empty; any widget (including non-focusable controls)
    // owns its clicks. Do not infer interactivity from a widget's focus policy.
    const QPoint point(qFloor(clientPoint.x()), qFloor(clientPoint.y()));
    return m_window->childAt(point) == m_masthead;
}

void Chrome::setSafeLayout(const QRectF& caption, const QMargins& screenInsets, int topResizeGuard) {
    m_caption = caption;
    if (!m_window || !m_masthead || !m_masthead->layout() ||
        m_masthead->window() != m_window) return;

    auto* layout = m_masthead->layout();
    const QRectF band(m_masthead->mapTo(m_window, QPoint()), m_masthead->size());
    int left = std::max(0, screenInsets.left() - qFloor(band.left()));
    int right = std::max(0, qCeil(band.right()) + screenInsets.right() - m_window->width());
    const int top = std::max(0, screenInsets.top() - qFloor(band.top()));
    const QRectF overlap = band.intersected(caption);
    if (!overlap.isEmpty()) {
        if (overlap.center().x() < band.center().x())
            left = std::max(left, qCeil(overlap.right() - band.left()));
        else
            right = std::max(right, qCeil(band.right() - overlap.left()));
    }
    QMargins margins = m_baseMargins + QMargins(left, top, right, 0);
    if (topResizeGuard > 0 && !overlap.isEmpty()) {
        if (auto* row = qobject_cast<QHBoxLayout*>(layout))
            margins = towardCaption(*m_masthead, *row, margins,
                                    caption.center().y() - band.top(), topResizeGuard);
    }
    if (layout->contentsMargins() != margins) layout->setContentsMargins(margins);
}

void Chrome::usePaperFallback() {
    if (!m_window || !m_masthead || m_masthead->window() != m_window) return;
    for (auto* widget = m_masthead.data(); widget && widget != m_window;
         widget = widget->parentWidget()) {
        if (widget->autoFillBackground() || widget->testAttribute(Qt::WA_StyledBackground) ||
            widget->testAttribute(Qt::WA_OpaquePaintEvent) || !widget->styleSheet().isEmpty()) return;
    }
    auto palette = m_masthead->palette();
    palette.setColor(QPalette::Window, QColor(0xe7, 0xde, 0xc8));
    m_masthead->setPalette(palette);
    m_masthead->setAutoFillBackground(true);
}

void Chrome::scheduleSync() {
    if (m_syncPending) return;
    m_syncPending = true;
    QTimer::singleShot(0, this, [this] {
        m_syncPending = false;
        if (m_window) sync();
    });
}

bool Chrome::eventFilter(QObject*, QEvent* event) {
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::WinIdChange:
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::LayoutRequest:
    case QEvent::ParentChange:
    case QEvent::WindowStateChange:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    case QEvent::DevicePixelRatioChange:
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    case QEvent::SafeAreaMarginsChange:
#endif
        scheduleSync();
        break;
    default:
        break;
    }
    return false;
}

class NativeFrameFallback final : public Chrome {
public:
    using Chrome::Chrome;

private:
    void sync() override { usePaperFallback(); }
};

} // namespace PaperWindowChrome::detail

void PaperWindowChrome::install(QMainWindow* window, QWidget* masthead) {
    if (!window || !masthead || window == masthead || !window->isWindow() ||
        masthead->window() != window || !masthead->layout() ||
        window->findChild<QObject*>(QStringLiteral("_nebPaperWindowChrome"),
                                   Qt::FindDirectChildrenOnly)) return;

    using namespace detail;
    Chrome* chrome = nullptr;
#if defined(Q_OS_MACOS)
    if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
        chrome = makeCocoaChrome(window, masthead);
#elif defined(Q_OS_WIN)
    if (QGuiApplication::platformName() == QStringLiteral("windows"))
        chrome = makeWindowsChrome(window, masthead);
#endif
    if (!chrome) chrome = new NativeFrameFallback(window, masthead);
    chrome->start();
}
