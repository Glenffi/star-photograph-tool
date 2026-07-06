#pragma once

#include <QWidget>
#include <QListView>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QMenu>
#include <QAction>
#include <QStringList>
#include <QPixmap>

class ThumbnailGenerator;

class FileItem {
public:
    QString filePath;
    QString fileName;
    QPixmap thumbnail;
    int iso = 0;
    double exposureTime = 0.0;
    bool isReferenceFrame = false;
    bool isExcluded = false;
    bool hasThumbnail = false;
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
    void onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onThumbnailReady(const QString& filePath, const QPixmap& thumbnail);
    void onCustomContextMenu(const QPoint& pos);
    void onExcludeSelected();
    void onSetReferenceFrame();
    void onViewMetadata();
    
private:
    void setupUI();
    void updateItemDisplay(int row);
    int findRowByPath(const QString& filePath) const;
    
    QListView* m_listView;
    QStandardItemModel* m_model;
    QSortFilterProxyModel* m_proxyModel;
    ThumbnailGenerator* m_thumbnailGen;
    QMenu* m_contextMenu;
    
    QAction* m_excludeAction;
    QAction* m_referenceAction;
    QAction* m_metadataAction;
    
    QList<FileItem> m_fileItems;
    int m_thumbnailSize = 64;
};