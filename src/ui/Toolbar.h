#pragma once

#include <QWidget>
#include <QIcon>

class QPushButton;
class QLabel;

class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget* parent = nullptr);
    void enableProcess(bool enabled);
    void enableExport(bool enabled);
    void setProcessing(bool processing);
    void setProjectSummary(const QString& summary);

signals:
    void importFilesClicked();
    void importFolderClicked();
    void clearProjectClicked();
    void startProcessClicked();
    void exportResultClicked();
    void settingsClicked();
    void aboutClicked();

private:
    void setupUI();
    void updateButtonStates();
    QPushButton* createIconButton(const QIcon& icon, const QString& tooltip);
    QPushButton* createActionButton(const QIcon& icon, const QString& text,
                                    bool isPrimary = false);

    QPushButton* m_importFilesBtn = nullptr;
    QPushButton* m_importFolderBtn = nullptr;
    QPushButton* m_clearProjectBtn = nullptr;
    QPushButton* m_startProcessBtn = nullptr;
    QPushButton* m_exportResultBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    QPushButton* m_aboutBtn = nullptr;
    QLabel* m_brandLabel = nullptr;
    QLabel* m_versionLabel = nullptr;
    QLabel* m_projectSummaryLabel = nullptr;
    bool m_canProcess = false;
    bool m_canExport = false;
    bool m_processing = false;
};
