#pragma once

#include <QStatusBar>

class QProgressBar;

class TaskStatusBar final : public QStatusBar {
    Q_OBJECT

public:
    explicit TaskStatusBar(QWidget* parent = nullptr);

    QProgressBar* progressBar() const { return m_progressBar; }
    void setProgressVisible(bool visible);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateProgressGeometry();

    QProgressBar* m_progressBar = nullptr;
};
