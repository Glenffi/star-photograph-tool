#pragma once

#include <QIcon>
#include <QWidget>

class QAction;
class QMenu;
class QPushButton;

class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget* parent = nullptr);
    void enableProcess(bool enabled);
    void enableExport(bool enabled);
    void setProcessing(bool processing);

    // Compatibility shim for the previous branded toolbar. Project status now
    // belongs in the status bar and this method intentionally does nothing.
    void setProjectSummary(const QString& summary);

signals:
    // Kept while MainWindow migrates scene selection into ProjectPanel. The
    // compact toolbar no longer exposes a scene control, so it is not emitted.
    void sceneSelectorClicked();
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

    QPushButton* m_importFilesBtn = nullptr;
    QPushButton* m_importFolderBtn = nullptr;
    QPushButton* m_exportResultBtn = nullptr;
    QPushButton* m_startProcessBtn = nullptr;
    QPushButton* m_overflowBtn = nullptr;
    QMenu* m_overflowMenu = nullptr;
    QAction* m_clearProjectAction = nullptr;
    QAction* m_settingsAction = nullptr;
    QAction* m_checkUpdatesAction = nullptr;
    QAction* m_shortcutsAction = nullptr;
    QAction* m_aboutAction = nullptr;
    bool m_canProcess = false;
    bool m_canExport = false;
    bool m_processing = false;
};
