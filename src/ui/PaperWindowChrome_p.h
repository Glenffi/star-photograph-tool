#pragma once

#include <QMainWindow>
#include <QMargins>
#include <QPointer>
#include <QRectF>

namespace PaperWindowChrome::detail {

class Chrome : public QObject {
public:
    Chrome(QMainWindow* window, QWidget* masthead);
    void start();

protected:
    QMainWindow* window() const { return m_window; }
    WId nativeId() const;
    bool coversClientTop() const;
    bool isDragPoint(const QPointF& clientPoint) const;
    void setSafeLayout(const QRectF& caption = {}, const QMargins& screenInsets = {},
                       int topResizeGuard = 0);
    void usePaperFallback();
    void scheduleSync();

private:
    bool eventFilter(QObject* watched, QEvent* event) override;
    virtual void sync() = 0;

    QPointer<QMainWindow> m_window;
    QPointer<QWidget> m_masthead;
    const QMargins m_baseMargins;
    QRectF m_caption;
    bool m_syncPending = false;
};

#if defined(Q_OS_MACOS)
Chrome* makeCocoaChrome(QMainWindow* window, QWidget* masthead);
#elif defined(Q_OS_WIN)
Chrome* makeWindowsChrome(QMainWindow* window, QWidget* masthead);
#endif

} // namespace PaperWindowChrome::detail
