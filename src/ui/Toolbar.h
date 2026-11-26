#pragma once

#include "ProcessingScene.h"

#include <QIcon>
#include <QWidget>

class QAction;
class QMenu;
class QPushButton;
class QComboBox;
class QLabel;
class QGraphicsDropShadowEffect;

class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget* parent = nullptr);
    void enableProcess(bool enabled);
    void enableExport(bool enabled);
    void setProcessing(bool processing);
    void setProgress(int value);
    void setScene(ProcessingScene scene);

    void setProjectSummary(const QString& summary);

signals:
    void printOrderRequested();
    void sceneChanged(ProcessingScene scene);
    void importFilesClicked();
    void importFolderClicked();
    void clearProjectClicked();
    void startProcessClicked();
    void exportResultClicked();
    void settingsClicked();
    void checkUpdatesClicked();
    void shortcutsClicked();
    void aboutClicked();

private:
    void setupUI();
    void updateButtonStates();
    void showOverflowMenu();
    void setButtonVariant(QPushButton* button, const char* variant);
    QPushButton* createIconButton(const QIcon& icon, const QString& tooltip);
    QPushButton* createActionButton(const QIcon& icon, const QString& text,
                                    const char* variant);

    QComboBox* m_sceneCombo = nullptr;
    QLabel* m_projectSummary = nullptr;
    QGraphicsDropShadowEffect* m_processGlow = nullptr;
    QPushButton* m_exportResultBtn = nullptr;
    QPushButton* m_startProcessBtn = nullptr;
    QPushButton* m_overflowBtn = nullptr;
    QMenu* m_overflowMenu = nullptr;
    QAction* m_importFilesAction = nullptr;
    QAction* m_importFolderAction = nullptr;
    QAction* m_clearProjectAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_checkUpdatesAction = nullptr;
    QAction* m_shortcutsAction = nullptr;
    QAction* m_aboutAction = nullptr;
    bool m_canProcess = false;
    bool m_canExport = false;
    bool m_processing = false;
    int m_progress = 0;
};
