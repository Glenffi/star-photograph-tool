#include "TaskStatusBar.h"

#include "StyleTokens.h"

#include <QProgressBar>
#include <QResizeEvent>

TaskStatusBar::TaskStatusBar(QWidget* parent)
    : QStatusBar(parent),
      m_progressBar(new QProgressBar(this)) {
    setFixedHeight(StyleTokens::Layout::kStatusBarHeight);
    m_progressBar->setProperty(
        StyleTokens::Properties::kControlRole,
        StyleTokens::Properties::kProgressLine);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();
}

void TaskStatusBar::setProgressVisible(bool visible) {
    m_progressBar->setVisible(visible);
    if (visible) m_progressBar->raise();
}

void TaskStatusBar::resizeEvent(QResizeEvent* event) {
    QStatusBar::resizeEvent(event);
    updateProgressGeometry();
}

void TaskStatusBar::updateProgressGeometry() {
    m_progressBar->setGeometry(
        0, 0, width(), StyleTokens::Controls::kProgressHeight);
}
