#include "ProjectPanel.h"
#include "../core/ThumbnailGenerator.h"
#include "../core/RawImageLoader.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QDir>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QDebug>
#include <QPainter>
#include <QGraphicsOpacityEffect>
#include <QEnterEvent>

// ==================== FileCard ====================

FileCard::FileCard(const FileItem& item, QWidget* parent)
    : QWidget(parent)
    , m_isReference(item.isReferenceFrame)
    , m_isExcluded(item.isExcluded)
{
    setFixedHeight(72);
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(10);

    // 缩略图
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setFixedSize(48, 48);
    m_thumbnailLabel->setScaledContents(true);
    m_thumbnailLabel->setStyleSheet("border-radius: 4px; background-color: #21262D;");
    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        m_thumbnailLabel->setPixmap(item.thumbnail.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        QPixmap placeholder(48, 48);
        placeholder.fill(QColor("#30363D"));
        m_thumbnailLabel->setPixmap(placeholder);
    }
    layout->addWidget(m_thumbnailLabel, 0, Qt::AlignVCenter);

    // 文本区域
    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(2);
    textLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #E6EDF3; background-color: transparent;");
    m_nameLabel->setText(item.fileName);
    textLayout->addWidget(m_nameLabel);

    // 元数据行
    QStringList metaParts;
    if (item.iso > 0) metaParts.append(QString("ISO %1").arg(item.iso));
    if (item.exposureTime > 0) {
        if (item.exposureTime >= 1.0) {
            metaParts.append(QString("%1s").arg(item.exposureTime, 0, 'f', 1));
        } else {
            metaParts.append(QString("1/%1s").arg(qRound(1.0 / item.exposureTime)));
        }
    }
    // FWHM 占位，后续实现
    metaParts.append(QString::fromUtf8("★4.2"));

    m_metaLabel = new QLabel(this);
    m_metaLabel->setStyleSheet("font-size: 11px; color: #8B949E; background-color: transparent;");
    m_metaLabel->setText(metaParts.join(" | "));
    textLayout->addWidget(m_metaLabel);

    layout->addLayout(textLayout, 1);

    // 状态标签（参考/排除）
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("font-size: 10px; font-weight: bold; background-color: transparent; padding: 2px 6px; border-radius: 4px;");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    updateStyle();
}

void FileCard::updateFromItem(const FileItem& item) {
    m_isReference = item.isReferenceFrame;
    m_isExcluded = item.isExcluded;
    m_nameLabel->setText(item.fileName);

    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        m_thumbnailLabel->setPixmap(item.thumbnail.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QStringList metaParts;
    if (item.iso > 0) metaParts.append(QString("ISO %1").arg(item.iso));
    if (item.exposureTime > 0) {
        if (item.exposureTime >= 1.0) {
            metaParts.append(QString("%1s").arg(item.exposureTime, 0, 'f', 1));
        } else {
            metaParts.append(QString("1/%1s").arg(qRound(1.0 / item.exposureTime)));
        }
    }
    metaParts.append(QString::fromUtf8("★4.2"));
    m_metaLabel->setText(metaParts.join(" | "));

    updateStyle();
}

void FileCard::setSelected(bool selected) {
    m_selected = selected;
    updateStyle();
}

void FileCard::updateStyle() {
    QString borderColor = m_selected ? "#58A6FF" : "transparent";
    QString bgColor;
    if (m_selected) {
        bgColor = "#21262D";
    } else if (m_hovered) {
        bgColor = "#21262D";
    } else {
        bgColor = "transparent";
    }

    int leftBorder = m_isReference ? 3 : 0;
    QString leftBorderColor = m_isReference ? "#F0B90B" : "transparent";

    setStyleSheet(QString(
        "FileCard {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  border-left: %3px solid %4;"
        "  border-radius: 6px;"
        "}"
    ).arg(bgColor, borderColor).arg(leftBorder).arg(leftBorderColor));

    // 状态标签
    if (m_isReference) {
        m_statusLabel->setText(QString::fromUtf8("参考"));
        m_statusLabel->setStyleSheet("font-size: 10px; font-weight: bold; color: #F0B90B; background-color: transparent; padding: 2px 6px; border-radius: 4px;");
    } else if (m_isExcluded) {
        m_statusLabel->setText(QString::fromUtf8("排除"));
        m_statusLabel->setStyleSheet("font-size: 10px; font-weight: bold; color: #8B949E; background-color: transparent; padding: 2px 6px; border-radius: 4px;");
    } else {
        m_statusLabel->setText("");
    }

    // 排除状态：整体透明度
    if (m_isExcluded) {
        setGraphicsEffect(nullptr);
        QGraphicsOpacityEffect* opacity = new QGraphicsOpacityEffect(this);
        opacity->setOpacity(0.5);
        setGraphicsEffect(opacity);
    } else {
        setGraphicsEffect(nullptr);
    }

    update();
}

void FileCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void FileCard::enterEvent(QEnterEvent* event) {
    m_hovered = true;
    updateStyle();
    QWidget::enterEvent(event);
}

void FileCard::leaveEvent(QEvent* event) {
    m_hovered = false;
    updateStyle();
    QWidget::leaveEvent(event);
}

// ==================== ProjectPanel ====================

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
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标题栏
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(36);
    titleBar->setStyleSheet("background-color: #161B22; border-bottom: 1px solid #30363D;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 12, 0);
    titleLayout->setSpacing(0);
    auto* titleLabel = new QLabel(QString::fromUtf8("📁 项目文件"), titleBar);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #E6EDF3; background-color: transparent;");
    titleLayout->addWidget(titleLabel);
    titleLayout->addStretch();
    layout->addWidget(titleBar);

    // 内容区域（文件列表或空状态）
    auto* contentWidget = new QWidget(this);
    contentWidget->setStyleSheet("background-color: #0D1117;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // 空状态
    setupEmptyState();
    contentLayout->addWidget(m_emptyState, 1, Qt::AlignCenter);

    // 文件列表
    setupFileList();
    contentLayout->addWidget(m_scrollArea, 1);
    m_scrollArea->setVisible(false);

    layout->addWidget(contentWidget, 1);

    // 底部统计栏
    setupBottomBar();
    layout->addWidget(m_bottomLabel);

    // 右键菜单
    m_contextMenu = new QMenu(this);
    m_contextMenu->setStyleSheet(
        "QMenu { background-color: #161B22; color: #E6EDF3; border: 1px solid #30363D; padding: 4px; }"
        "QMenu::item { padding: 6px 20px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #30363D; }"
        "QMenu::separator { height: 1px; background-color: #30363D; margin: 4px 8px; }"
    );

    m_referenceAction = new QAction(QString::fromUtf8("⭐ 设为参考帧"), this);
    connect(m_referenceAction, &QAction::triggered, this, &ProjectPanel::onSetReferenceFrame);
    m_contextMenu->addAction(m_referenceAction);

    m_excludeAction = new QAction(QString::fromUtf8("🚫 排除 / 恢复"), this);
    connect(m_excludeAction, &QAction::triggered, this, &ProjectPanel::onExcludeSelected);
    m_contextMenu->addAction(m_excludeAction);

    m_contextMenu->addSeparator();

    m_metadataAction = new QAction(QString::fromUtf8("📄 查看元数据"), this);
    connect(m_metadataAction, &QAction::triggered, this, &ProjectPanel::onViewMetadata);
    m_contextMenu->addAction(m_metadataAction);

    m_removeAction = new QAction(QString::fromUtf8("❌ 从列表移除"), this);
    connect(m_removeAction, &QAction::triggered, this, &ProjectPanel::onRemoveFromList);
    m_contextMenu->addAction(m_removeAction);
}

void ProjectPanel::setupEmptyState() {
    m_emptyState = new QWidget(this);
    m_emptyState->setStyleSheet("background-color: transparent;");
    m_emptyLayout = new QVBoxLayout(m_emptyState);
    m_emptyLayout->setSpacing(12);
    m_emptyLayout->setAlignment(Qt::AlignCenter);

    auto* iconLabel = new QLabel(QString::fromUtf8("📁"), m_emptyState);
    iconLabel->setStyleSheet("font-size: 48px; color: #484F58; background-color: transparent;");
    iconLabel->setAlignment(Qt::AlignCenter);
    m_emptyLayout->addWidget(iconLabel);

    auto* textLabel = new QLabel(QString::fromUtf8("拖入 RAW 文件或点击导入"), m_emptyState);
    textLabel->setStyleSheet("font-size: 14px; color: #8B949E; background-color: transparent;");
    textLabel->setAlignment(Qt::AlignCenter);
    m_emptyLayout->addWidget(textLabel);

    auto* formatLabel = new QLabel(QString::fromUtf8("支持 NEF, CR2, ARW, DNG, RAW, ORF, RAF, PEF, CR3"), m_emptyState);
    formatLabel->setStyleSheet("font-size: 11px; color: #6E7681; background-color: transparent;");
    formatLabel->setAlignment(Qt::AlignCenter);
    m_emptyLayout->addWidget(formatLabel);

    m_emptyImportBtn = new QPushButton(QString::fromUtf8("📁 导入 RAW 文件"), m_emptyState);
    m_emptyImportBtn->setFixedHeight(36);
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    m_emptyImportBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #F0B90B;"
        "  color: #0D1117;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 8px 20px;"
        "  font-size: 13px;"
        "  font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "  background-color: #F5C518;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #D4A009;"
        "}"
    );
    connect(m_emptyImportBtn, &QPushButton::clicked, this, &ProjectPanel::onImportClicked);
    m_emptyLayout->addWidget(m_emptyImportBtn, 0, Qt::AlignCenter);
}

void ProjectPanel::setupFileList() {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background-color: #0D1117; border: none; }"
        "QScrollBar:vertical { background-color: #0D1117; width: 12px; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background-color: #30363D; border-radius: 6px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background-color: #484F58; }"
    );

    m_listContainer = new QWidget();
    m_listContainer->setStyleSheet("background-color: #0D1117;");
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(4);
    m_listLayout->addStretch();

    m_scrollArea->setWidget(m_listContainer);
}

void ProjectPanel::setupBottomBar() {
    m_bottomLabel = new QLabel(this);
    m_bottomLabel->setFixedHeight(28);
    m_bottomLabel->setStyleSheet(
        "QLabel { background-color: #161B22; color: #8B949E; font-size: 11px; "
        "padding: 4px 12px; border-top: 1px solid #30363D; }"
    );
    m_bottomLabel->setText(QString::fromUtf8("共 0 张 | 已选 0 张 | 参考帧 0 张"));
}

void ProjectPanel::showFileList() {
    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);
}

void ProjectPanel::showEmptyState() {
    m_emptyState->setVisible(true);
    m_scrollArea->setVisible(false);
}

void ProjectPanel::addFiles(const QStringList& filePaths) {
    bool added = false;
    for (const QString& filePath : filePaths) {
        if (findIndexByPath(filePath) >= 0) continue;

        FileItem item;
        item.filePath = filePath;
        item.fileName = QFileInfo(filePath).fileName();

        RawImageLoader loader;
        RawImageLoader::ImageData imageData;
        if (loader.loadRaw(filePath.toStdString(), imageData)) {
            item.iso = imageData.iso;
            item.exposureTime = imageData.exposureTime;
            item.aperture = imageData.aperture;
            item.focalLength = imageData.focalLength;
        }

        m_fileItems.append(item);
        int index = m_fileItems.size() - 1;
        addFileCard(item, index);
        m_thumbnailGen->generateAsync(filePath, 96);
        added = true;
    }

    if (added) {
        showFileList();
        updateBottomBar();
        emit filesChanged();
    }
}

void ProjectPanel::addFileCard(const FileItem& item, int index) {
    auto* card = new FileCard(item, m_listContainer);
    connect(card, &FileCard::clicked, this, [this, index]() {
        setCurrentIndex(index);
    });
    connect(card, &FileCard::customContextMenuRequested, this, [this, card, index](const QPoint& pos) {
        m_contextMenuIndex = index;
        m_contextMenu->exec(card->mapToGlobal(pos));
    });
    m_cards.append(card);
    // 插入到 stretch 之前
    m_listLayout->insertWidget(m_listLayout->count() - 1, card);
}

void ProjectPanel::clearFiles() {
    for (auto* card : m_cards) {
        card->deleteLater();
    }
    m_cards.clear();
    m_fileItems.clear();
    m_currentIndex = -1;
    showEmptyState();
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::removeSelected() {
    if (m_currentIndex < 0 || m_currentIndex >= m_cards.size()) return;

    // 单选模式下移除当前选中
    int idx = m_currentIndex;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);
    m_currentIndex = -1;

    // 重新索引所有卡片
    for (int i = 0; i < m_cards.size(); ++i) {
        disconnect(m_cards[i], &FileCard::clicked, nullptr, nullptr);
        connect(m_cards[i], &FileCard::clicked, this, [this, i]() {
            setCurrentIndex(i);
        });
    }

    if (m_cards.isEmpty()) {
        showEmptyState();
    }
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::setReferenceFrame(const QString& filePath) {
    for (int i = 0; i < m_fileItems.size(); ++i) {
        m_fileItems[i].isReferenceFrame = (m_fileItems[i].filePath == filePath);
        updateCard(i);
    }
    updateBottomBar();
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
    if (m_currentIndex < 0 || m_currentIndex >= m_fileItems.size()) {
        return QString();
    }
    return m_fileItems[m_currentIndex].filePath;
}

void ProjectPanel::setCurrentIndex(int index) {
    if (index < 0 || index >= m_fileItems.size()) return;

    // 清除旧选中
    if (m_currentIndex >= 0 && m_currentIndex < m_cards.size()) {
        m_cards[m_currentIndex]->setSelected(false);
    }

    m_currentIndex = index;
    m_cards[m_currentIndex]->setSelected(true);

    // 确保可见
    m_scrollArea->ensureWidgetVisible(m_cards[m_currentIndex]);

    emit fileSelected(m_fileItems[m_currentIndex].filePath);
    updateBottomBar();
}

void ProjectPanel::updateCard(int index) {
    if (index < 0 || index >= m_cards.size()) return;
    m_cards[index]->updateFromItem(m_fileItems[index]);
}

void ProjectPanel::updateAllCardStyles() {
    for (int i = 0; i < m_cards.size(); ++i) {
        m_cards[i]->setSelected(i == m_currentIndex);
    }
}

void ProjectPanel::updateBottomBar() {
    int total = m_fileItems.size();
    int selected = (m_currentIndex >= 0) ? 1 : 0;
    int ref = 0;
    for (const auto& item : m_fileItems) {
        if (item.isReferenceFrame) ref++;
    }
    m_bottomLabel->setText(
        QString::fromUtf8("共 %1 张 | 已选 %2 张 | 参考帧 %3 张")
            .arg(total).arg(selected).arg(ref)
    );
}

int ProjectPanel::findIndexByPath(const QString& filePath) const {
    for (int i = 0; i < m_fileItems.size(); ++i) {
        if (m_fileItems[i].filePath == filePath) return i;
    }
    return -1;
}

void ProjectPanel::onThumbnailReady(const QString& filePath, const QPixmap& thumbnail) {
    int idx = findIndexByPath(filePath);
    if (idx < 0) return;
    m_fileItems[idx].thumbnail = thumbnail;
    m_fileItems[idx].hasThumbnail = !thumbnail.isNull();
    updateCard(idx);
}

void ProjectPanel::onCustomContextMenu(const QPoint& pos) {
    Q_UNUSED(pos)
    // 通过卡片触发
}

void ProjectPanel::onExcludeSelected() {
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;
    m_fileItems[m_contextMenuIndex].isExcluded = !m_fileItems[m_contextMenuIndex].isExcluded;
    updateCard(m_contextMenuIndex);
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::onSetReferenceFrame() {
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;
    setReferenceFrame(m_fileItems[m_contextMenuIndex].filePath);
    emit filesChanged();
}

void ProjectPanel::onViewMetadata() {
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;
    emit requestMetadata(m_fileItems[m_contextMenuIndex].filePath);
}

void ProjectPanel::onRemoveFromList() {
    if (m_contextMenuIndex < 0 || m_contextMenuIndex >= m_fileItems.size()) return;

    int idx = m_contextMenuIndex;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);

    if (m_currentIndex == idx) {
        m_currentIndex = -1;
    } else if (m_currentIndex > idx) {
        m_currentIndex--;
    }

    // 重新连接
    for (int i = 0; i < m_cards.size(); ++i) {
        disconnect(m_cards[i], &FileCard::clicked, nullptr, nullptr);
        connect(m_cards[i], &FileCard::clicked, this, [this, i]() {
            setCurrentIndex(i);
        });
    }

    if (m_cards.isEmpty()) {
        showEmptyState();
    }
    updateAllCardStyles();
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::onImportClicked() {
    // 占位：空状态导入按钮点击后通知主窗口刷新
    // 实际文件导入由主窗口通过 Toolbar 按钮处理
    emit filesChanged();
}
