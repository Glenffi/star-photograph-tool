#pragma once

#include <QWidget>

class QPushButton;
class QLabel;

class Toolbar : public QWidget {
    Q_OBJECT
public:
    explicit Toolbar(QWidget* parent = nullptr);
    void enableProcess(bool enabled);
    void enableExport(bool enabled);

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
    QPushButton* createIconButton(const QString& icon, const QString& tooltip);
    QPushButton* createActionButton(const QString& icon, const QString& text, bool isPrimary = false);

    QPushButton* m_importFilesBtn = nullptr;
    QPushButton* m_importFolderBtn = nullptr;
    QPushButton* m_clearProjectBtn = nullptr;
    QPushButton* m_startProcessBtn = nullptr;
    QPushButton* m_exportResultBtn = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    QPushButton* m_aboutBtn = nullptr;
    QLabel* m_brandLabel = nullptr;
    QLabel* m_versionLabel = nullptr;
};
