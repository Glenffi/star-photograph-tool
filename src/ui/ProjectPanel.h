#pragma once

#include "ProcessingScene.h"

#include <QWidget>
#include <QStringList>
#include <QPixmap>
#include <QPushButton>

#include <optional>

class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QComboBox;
class QMenu;
class QAction;
class QLabel;
class QEnterEvent;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;
class QTimer;
class ThumbnailGenerator;
class QImage;

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
    bool metadataLoaded = false;
    bool metadataFailed = false;
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
    void keyPressEvent(QKeyEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void updateStyle();
    void updateElidedName();
    bool m_selected = false;
    bool m_hovered = false;
    bool m_isReference = false;
    bool m_isExcluded = false;
    QString m_fileName;
    QLabel* m_thumbnailLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QWidget* m_referenceDot = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
};

class ProjectPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectPanel(QWidget* parent = nullptr);
    ~ProjectPanel();

    void addFiles(const QStringList& filePaths);
    void clearFiles();
    void removeSelected();
    void toggleSelectedExclusion();
    bool undoLastRemoval();
    void setReferenceFrame(const QString& filePath);
    void setReferenceFrame(int index);
    void setScene(ProcessingScene scene);
    ProcessingScene currentScene() const;
    void setEditingEnabled(bool enabled);
    void setCalibrationSummary(const QString& dark, const QString& flat,
                               const QString& bias, const QString& darkFlat,
                               bool ready);

    QStringList filePaths() const;
    QStringList includedFilePaths() const;  // 未排除的文件
    QString currentFilePath() const;
    QString referenceFramePath() const;
    void applyPreviewData(const QString& filePath, const QImage& image,
                          int iso, double exposureTime, double aperture,
                          int focalLength);
    void requestThumbnail(const QString& filePath);

signals:
    void fileSelected(const QString& filePath);
    void requestMetadata(const QString& filePath);
    void filesChanged();
    void filesDropped(const QStringList& filePaths);
    void referenceFrameChanged();
    void sceneChanged(ProcessingScene scene);
    void calibrationSettingsRequested();
    void undoAvailabilityChanged(bool available, const QString& fileName);
    void requestProcess();

private slots:
    void onThumbnailReady(const QString& filePath, const QPixmap& thumbnail);
    void onMetadataReady(const QString& filePath, int iso, double exposureTime,
                         double aperture, int focalLength, bool loaded);
    void onCustomContextMenu(const QPoint& pos);
    void onExcludeSelected();
    void onSetReferenceFrame();
    void onViewMetadata();
    void onRevealInFileManager();
    void onRemoveFromList();
    void onImportClicked();

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    QWidget* setupHeader();
    QWidget* setupCalibrationSummary();
    void setupEmptyState();
    void setupFileList();
    void setupBottomBar();
    void addFileCard(const FileItem& item, int index = -1);
    void removeAt(int index);
    void toggleExclusionAt(int index);
    void updateCard(int index);
    void updateCardSelection(int index);
    void updateAllCardStyles();
    void updateBottomBar();
    int findIndexByPath(const QString& filePath) const;
    void showFileList();
    void showEmptyState();
    void setCurrentIndex(int index);
    int minimumFrameCount() const;
    void updateEditingState();

    // UI 组件
    QComboBox* m_sceneCombo = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_listContainer = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QWidget* m_emptyState = nullptr;
    QVBoxLayout* m_emptyLayout = nullptr;
    QLabel* m_countLabel = nullptr;
    QLabel* m_bottomLabel = nullptr;
    QPushButton* m_headerImportBtn = nullptr;
    QPushButton* m_emptyImportBtn = nullptr;
    QWidget* m_calibrationSummary = nullptr;
    QLabel* m_darkSummary = nullptr;
    QLabel* m_flatSummary = nullptr;
    QLabel* m_biasSummary = nullptr;
    QLabel* m_darkFlatSummary = nullptr;
    QLabel* m_calibrationReady = nullptr;
    QPushButton* m_calibrationSettingsBtn = nullptr;

    // 右键菜单
    QMenu* m_contextMenu = nullptr;
    QAction* m_excludeAction = nullptr;
    QAction* m_referenceAction = nullptr;
    QAction* m_metadataAction = nullptr;
    QAction* m_revealAction = nullptr;
    QAction* m_removeAction = nullptr;

    // 数据
    ThumbnailGenerator* m_thumbnailGen = nullptr;
    QList<FileItem> m_fileItems;
    QList<FileCard*> m_cards;
    QStringList m_pendingThumbnailPaths;
    ProcessingScene m_scene = ProcessingScene::Nightscape;
    bool m_editingEnabled = true;
    std::optional<FileItem> m_lastRemovedItem;
    int m_lastRemovedIndex = -1;
    QTimer* m_undoTimer = nullptr;
    int m_currentIndex = -1;
    int m_contextMenuIndex = -1;
};
