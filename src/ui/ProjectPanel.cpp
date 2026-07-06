#include "ProjectPanel.h"
#include "../core/ThumbnailGenerator.h"
#include "../core/RawImageLoader.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileInfo>
#include <QDir>
#include <QItemSelectionModel>
#include <QDebug>
#include <algorithm>

ProjectPanel::ProjectPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    
    m_thumbnailGen = new ThumbnailGenerator(this);
    connect(m_thumbnailGen, &ThumbnailGenerator::thumbnailReady,
            this, &ProjectPanel::onThumbnailReady);
}

ProjectPanel::~ProjectPanel() = default;

void ProjectPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // 标题
    auto* titleLabel = new QLabel("项目文件", this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #E6EDF3;");
    layout->addWidget(titleLabel);
    
    // 文件列表
    m_listView = new QListView(this);
    m_listView->setViewMode(QListView::ListMode);
    m_listView->setIconSize(QSize(m_thumbnailSize, m_thumbnailSize));
    m_listView->setSpacing(4);
    m_listView->setUniformItemSizes(true);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listView->setStyleSheet(
        "QListView { background-color: #161B22; border: 1px solid #30363D; border-radius: 6px; }"
        "QListView::item { background-color: #161B22; color: #E6EDF3; padding: 4px; border-radius: 4px; }"
        "QListView::item:selected { background-color: #30363D; }"
        "QListView::item:hover { background-color: #21262D; }"
    );
    
    m_model = new QStandardItemModel(this);
    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_listView->setModel(m_proxyModel);
    
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ProjectPanel::onSelectionChanged);
    connect(m_listView, &QListView::customContextMenuRequested,
            this, &ProjectPanel::onCustomContextMenu);
    
    layout->addWidget(m_listView, 1);
    
    // 右键菜单
    m_contextMenu = new QMenu(this);
    m_contextMenu->setStyleSheet(
        "QMenu { background-color: #161B22; color: #E6EDF3; border: 1px solid #30363D; }"
        "QMenu::item:selected { background-color: #30363D; }"
    );
    
    m_excludeAction = new QAction("排除所选文件", this);
    connect(m_excludeAction, &QAction::triggered, this, &ProjectPanel::onExcludeSelected);
    m_contextMenu->addAction(m_excludeAction);
    
    m_referenceAction = new QAction("设为参考帧", this);
    connect(m_referenceAction, &QAction::triggered, this, &ProjectPanel::onSetReferenceFrame);
    m_contextMenu->addAction(m_referenceAction);
    
    m_contextMenu->addSeparator();
    
    m_metadataAction = new QAction("查看元数据", this);
    connect(m_metadataAction, &QAction::triggered, this, &ProjectPanel::onViewMetadata);
    m_contextMenu->addAction(m_metadataAction);
}

void ProjectPanel::addFiles(const QStringList& filePaths) {
    for (const QString& filePath : filePaths) {
        // 检查是否已存在
        if (findRowByPath(filePath) >= 0) {
            continue;
        }
        
        FileItem item;
        item.filePath = filePath;
        item.fileName = QFileInfo(filePath).fileName();
        
        // 尝试读取 EXIF 元数据
        RawImageLoader loader;
        RawImageLoader::ImageData imageData;
        if (loader.loadRaw(filePath.toStdString(), imageData)) {
            item.iso = imageData.iso;
            item.exposureTime = imageData.exposureTime;
        }
        
        m_fileItems.append(item);
        
        // 创建模型项
        auto* standardItem = new QStandardItem();
        standardItem->setData(filePath, Qt::UserRole);
        m_model->appendRow(standardItem);
        
        updateItemDisplay(m_model->rowCount() - 1);
        
        // 异步请求缩略图
        m_thumbnailGen->generateAsync(filePath, m_thumbnailSize);
    }
    
    emit filesChanged();
}

void ProjectPanel::clearFiles() {
    m_model->clear();
    m_fileItems.clear();
    emit filesChanged();
}

void ProjectPanel::removeSelected() {
    QModelIndexList selected = m_listView->selectionModel()->selectedIndexes();
    // 从后往前删除，避免索引变化
    QList<int> rows;
    for (const QModelIndex& idx : selected) {
        rows.append(m_proxyModel->mapToSource(idx).row());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) {
        m_model->removeRow(row);
        m_fileItems.removeAt(row);
    }
    emit filesChanged();
}

void ProjectPanel::setReferenceFrame(const QString& filePath) {
    for (int i = 0; i < m_fileItems.size(); ++i) {
        m_fileItems[i].isReferenceFrame = (m_fileItems[i].filePath == filePath);
        updateItemDisplay(i);
    }
}

QStringList ProjectPanel::filePaths() const {
    QStringList paths;
    for (const auto& item : m_fileItems) {
        if (!item.isExcluded) {
            paths.append(item.filePath);
        }
    }
    return paths;
}

QString ProjectPanel::currentFilePath() const {
    QModelIndexList selected = m_listView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) {
        return QString();
    }
    int srcRow = m_proxyModel->mapToSource(selected.first()).row();
    if (srcRow >= 0 && srcRow < m_fileItems.size()) {
        return m_fileItems[srcRow].filePath;
    }
    return QString();
}

void ProjectPanel::onSelectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
    Q_UNUSED(deselected)
    if (!selected.indexes().isEmpty()) {
        int srcRow = m_proxyModel->mapToSource(selected.indexes().first()).row();
        if (srcRow >= 0 && srcRow < m_fileItems.size()) {
            emit fileSelected(m_fileItems[srcRow].filePath);
        }
    }
}

void ProjectPanel::onThumbnailReady(const QString& filePath, const QPixmap& thumbnail) {
    int row = findRowByPath(filePath);
    if (row < 0 || row >= m_fileItems.size()) {
        return;
    }
    
    m_fileItems[row].thumbnail = thumbnail;
    m_fileItems[row].hasThumbnail = !thumbnail.isNull();
    updateItemDisplay(row);
}

void ProjectPanel::onCustomContextMenu(const QPoint& pos) {
    QModelIndex index = m_listView->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    m_contextMenu->exec(m_listView->mapToGlobal(pos));
}

void ProjectPanel::onExcludeSelected() {
    QModelIndexList selected = m_listView->selectionModel()->selectedIndexes();
    for (const QModelIndex& idx : selected) {
        int srcRow = m_proxyModel->mapToSource(idx).row();
        if (srcRow >= 0 && srcRow < m_fileItems.size()) {
            m_fileItems[srcRow].isExcluded = !m_fileItems[srcRow].isExcluded;
            updateItemDisplay(srcRow);
        }
    }
    emit filesChanged();
}

void ProjectPanel::onSetReferenceFrame() {
    QString path = currentFilePath();
    if (!path.isEmpty()) {
        setReferenceFrame(path);
        emit filesChanged();
    }
}

void ProjectPanel::onViewMetadata() {
    QString path = currentFilePath();
    if (!path.isEmpty()) {
        emit requestMetadata(path);
    }
}

void ProjectPanel::updateItemDisplay(int row) {
    if (row < 0 || row >= m_fileItems.size()) {
        return;
    }
    
    const FileItem& item = m_fileItems[row];
    QStandardItem* modelItem = m_model->item(row);
    if (!modelItem) return;
    
    // 构建显示文本
    QStringList parts;
    parts.append(item.fileName);
    
    QStringList metaParts;
    if (item.iso > 0) {
        metaParts.append(QString("ISO %1").arg(item.iso));
    }
    if (item.exposureTime > 0) {
        if (item.exposureTime >= 1.0) {
            metaParts.append(QString("%1s").arg(item.exposureTime, 0, 'f', 1));
        } else {
            metaParts.append(QString("1/%1s").arg(qRound(1.0 / item.exposureTime)));
        }
    }
    
    QString displayText = parts.join("\n");
    if (!metaParts.isEmpty()) {
        displayText += "\n" + metaParts.join("  ");
    }
    
    if (item.isReferenceFrame) {
        displayText = "[参考] " + displayText;
    }
    if (item.isExcluded) {
        displayText = "[排除] " + displayText;
    }
    
    modelItem->setText(displayText);
    
    if (item.hasThumbnail && !item.thumbnail.isNull()) {
        modelItem->setIcon(QIcon(item.thumbnail));
    } else {
        // 占位图标
        QPixmap placeholder(m_thumbnailSize, m_thumbnailSize);
        placeholder.fill(QColor("#30363D"));
        modelItem->setIcon(QIcon(placeholder));
    }
    
    modelItem->setData(item.filePath, Qt::UserRole);
    
    // 根据状态设置前景色
    if (item.isExcluded) {
        modelItem->setForeground(QColor("#8B949E"));
    } else if (item.isReferenceFrame) {
        modelItem->setForeground(QColor("#F0B90B"));
    } else {
        modelItem->setForeground(QColor("#E6EDF3"));
    }
}

int ProjectPanel::findRowByPath(const QString& filePath) const {
    for (int i = 0; i < m_fileItems.size(); ++i) {
        if (m_fileItems[i].filePath == filePath) {
            return i;
        }
    }
    return -1;
}