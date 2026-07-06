#pragma once

#include <QWidget>
#include <QStringList>
#include <QPixmap>
#include <QPushButton>

class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QMenu;
class QAction;
class QLabel;
class QEnterEvent;
class QGraphicsOpacityEffect;
class ThumbnailGenerator;

struct FileItem {
    QString filePath;
    QString fileName;
    QPixmap thumbnail;
    int iso = 0;
    double exposureTime = 0.0;
    double aperture = 0.0;
    int focalLength = 0;
    bool isReferenceFrame = false;
    bool isExcluded = false;
    bool hasThumbnail = false;
};

class FileCard : public QWidget {
    Q_OBJECT
public:
    explicit FileCard(const FileItem& item, QWidget* parent = nullptr);
    void updateFromItem(const FileItem& item);
    void setSelected(bool selected);

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateStyle();
    bool m_selected = false;
    bool m_hovered = false;
    bool m_isReference = false;
    bool m_isExcluded = false;
    QLabel* m_thumbnailLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
};

class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget* parent = nullptr);
    ~ProjectPanel();

    void addFiles(const QStringList& filePaths);
    void clearFiles();
    void removeSelected();
    void setReferenceFrame(const QString& filePath);

    QStringList filePaths() const;
    QString currentFilePath() const;

signals:
    void fileSelected(const QString& filePath);
    void requestMetadata(const QString& filePath);
    void filesChanged();

private slots:
    void onThumbnailReady(const QString& filePath, const QPixmap& thumbnail);
    void onCustomContextMenu(const QPoint& pos);
    void onExcludeSelected();
    void onSetReferenceFrame();
    void onViewMetadata();
    void onRemoveFromList();
    void onImportClicked();

private:
    void setupUI();
    void setupEmptyState();
    void setupFileList();
    void setupBottomBar();
    void addFileCard(const FileItem& item, int index);
    void updateCard(int index);
    void updateCardSelection(int index);
    void updateAllCardStyles();
    void updateBottomBar();
    int findIndexByPath(const QString& filePath) const;
    void showFileList();
    void showEmptyState();
    void setCurrentIndex(int index);

    // UI 组件
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_listContainer = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QWidget* m_emptyState = nullptr;
    QVBoxLayout* m_emptyLayout = nullptr;
    QLabel* m_bottomLabel = nullptr;
    QPushButton* m_emptyImportBtn = nullptr;

    // 右键菜单
    QMenu* m_contextMenu = nullptr;
    QAction* m_excludeAction = nullptr;
    QAction* m_referenceAction = nullptr;
    QAction* m_metadataAction = nullptr;
    QAction* m_removeAction = nullptr;

    // 数据
    ThumbnailGenerator* m_thumbnailGen = nullptr;
    QList<FileItem> m_fileItems;
    QList<FileCard*> m_cards;
    int m_currentIndex = -1;
    int m_contextMenuIndex = -1;
};
