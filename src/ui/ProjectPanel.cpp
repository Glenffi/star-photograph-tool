#include "ProjectPanel.h"
#include "UiAssets.h"
#include "../core/ThumbnailGenerator.h"
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
#include <QGraphicsOpacityEffect>
#include <QEnterEvent>
#include <QResizeEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include <algorithm>
#include <utility>

namespace {

QString formatExposure(double exposureTime) {
    if (exposureTime <= 0.0) {
        return QString::fromUtf8("曝光 --");
    }

    if (exposureTime >= 1.0) {
        const int decimals = exposureTime < 10.0 ? 1 : 0;
        return QString::fromUtf8("%1 s").arg(exposureTime, 0, 'f', decimals);
    }

    return QString::fromUtf8("1/%1 s").arg(qRound(1.0 / exposureTime));
}

QString formatMetadata(const FileItem& item) {
    if (item.iso <= 0 && item.exposureTime <= 0.0 && item.focalLength <= 0) {
        return QString::fromUtf8("正在读取元数据...");
    }
    const QString iso = item.iso > 0
        ? QString::fromUtf8("ISO %1").arg(item.iso)
        : QString::fromUtf8("ISO --");
    const QString focalLength = item.focalLength > 0
        ? QString::fromUtf8("%1 mm").arg(item.focalLength)
        : QString::fromUtf8("焦距 --");
    return QString::fromUtf8("%1 · %2 · %3")
        .arg(iso, formatExposure(item.exposureTime), focalLength);
}

} // namespace

// ==================== FileCard ====================

FileCard::FileCard(const FileItem& item, QWidget* parent)
    : QWidget(parent)
    , m_isReference(item.isReferenceFrame)
    , m_isExcluded(item.isExcluded)
    , m_fileName(item.fileName)
{
    setFixedHeight(76);
    setMinimumWidth(0);
    setAttribute(Qt::WA_StyledBackground, true);
    setCursor(Qt::PointingHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(9);

    // 缩略图
    m_thumbnailLabel = new QLabel(this);
    m_thumbnailLabel->setFixedSize(52, 52);
    m_thumbnailLabel->setScaledContents(true);
    m_thumbnailLabel->setStyleSheet(
        "border: 1px solid #2B393B; border-radius: 5px; background-color: #202A2D;"
    );
    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        m_thumbnailLabel->setPixmap(item.thumbnail.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        QPixmap placeholder(52, 52);
        placeholder.fill(QColor("#202A2D"));
        m_thumbnailLabel->setPixmap(placeholder);
    }
    layout->addWidget(m_thumbnailLabel, 0, Qt::AlignVCenter);

    // 文本区域
    auto* textLayout = new QVBoxLayout();
    textLayout->setSpacing(4);
    textLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(this);
    m_nameLabel->setMinimumWidth(0);
    m_nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_nameLabel->setToolTip(item.fileName);
    m_nameLabel->setStyleSheet(
        "font-size: 13px; font-weight: 600; color: #F3F7F6; background-color: transparent;"
    );
    m_nameLabel->setText(m_fileName);
    textLayout->addWidget(m_nameLabel);

    // 元数据行
    m_metaLabel = new QLabel(this);
    m_metaLabel->setMinimumWidth(0);
    m_metaLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_metaLabel->setStyleSheet(
        "font-size: 11px; color: #889A96; background-color: transparent;"
    );
    m_metaLabel->setText(formatMetadata(item));
    textLayout->addWidget(m_metaLabel);

    layout->addLayout(textLayout, 1);

    // 状态标签（参考/排除）
    m_statusLabel = new QLabel(this);
    m_statusLabel->setFixedWidth(34);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(m_opacityEffect);
    updateStyle();
}

void FileCard::updateFromItem(const FileItem& item) {
    m_isReference = item.isReferenceFrame;
    m_isExcluded = item.isExcluded;
    m_fileName = item.fileName;
    m_nameLabel->setToolTip(m_fileName);
    updateElidedName();

    if (!item.thumbnail.isNull() && item.hasThumbnail) {
        m_thumbnailLabel->setPixmap(item.thumbnail.scaled(52, 52, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    m_metaLabel->setText(formatMetadata(item));

    updateStyle();
}

void FileCard::setSelected(bool selected) {
    m_selected = selected;
    updateStyle();
}

void FileCard::updateStyle() {
    const QString borderColor = m_selected ? "#6FA8FF" : "#2B393B";
    QString bgColor;
    if (m_selected) {
        bgColor = "#1C2B36";
    } else if (m_hovered) {
        bgColor = "#171F21";
    } else {
        bgColor = "#111719";
    }

    const int leftBorder = m_isReference ? 3 : 1;
    const QString leftBorderColor = m_isReference ? "#F2B65A" : borderColor;

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
        m_statusLabel->setStyleSheet(
            "font-size: 10px; font-weight: 600; color: #F2B65A; background-color: transparent;"
        );
    } else if (m_isExcluded) {
        m_statusLabel->setText(QString::fromUtf8("排除"));
        m_statusLabel->setStyleSheet(
            "font-size: 10px; font-weight: 600; color: #889A96; background-color: transparent;"
        );
    } else {
        m_statusLabel->setText("");
        m_statusLabel->setStyleSheet("background-color: transparent;");
    }

    m_opacityEffect->setOpacity(m_isExcluded ? 0.48 : 1.0);

    update();
}

void FileCard::updateElidedName() {
    const int availableWidth = m_nameLabel->width();
    if (availableWidth <= 0) {
        m_nameLabel->setText(m_fileName);
        return;
    }
    m_nameLabel->setText(
        m_nameLabel->fontMetrics().elidedText(m_fileName, Qt::ElideMiddle, availableWidth)
    );
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

void FileCard::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateElidedName();
}

// ==================== ProjectPanel ====================

ProjectPanel::ProjectPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setAcceptDrops(true);

    m_thumbnailGen = new ThumbnailGenerator(this);
    connect(m_thumbnailGen, &ThumbnailGenerator::thumbnailReady,
            this, &ProjectPanel::onThumbnailReady);
    connect(m_thumbnailGen, &ThumbnailGenerator::metadataReady,
            this, &ProjectPanel::onMetadataReady);
}

ProjectPanel::~ProjectPanel() = default;

void ProjectPanel::setupUI() {
    setStyleSheet("ProjectPanel { background-color: #111719; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 标题栏
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(44);
    titleBar->setStyleSheet("background-color: #171F21; border-bottom: 1px solid #2B393B;");
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 8, 0);
    titleLayout->setSpacing(8);
    auto* titleLabel = new QLabel(QString::fromUtf8("素材"), titleBar);
    titleLabel->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #F3F7F6; background-color: transparent;"
    );
    titleLayout->addWidget(titleLabel);

    m_countLabel = new QLabel(QStringLiteral("0"), titleBar);
    m_countLabel->setAlignment(Qt::AlignCenter);
    m_countLabel->setMinimumWidth(24);
    m_countLabel->setFixedHeight(20);
    m_countLabel->setStyleSheet(
        "color: #889A96; background-color: #202A2D; border: 1px solid #2B393B;"
        "border-radius: 5px; padding: 0 6px; font-size: 11px;"
    );
    titleLayout->addWidget(m_countLabel);
    titleLayout->addStretch();

    m_headerImportBtn = new QPushButton(titleBar);
    m_headerImportBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Add, QColor("#A7B8B4")));
    m_headerImportBtn->setIconSize(QSize(16, 16));
    m_headerImportBtn->setFixedSize(28, 28);
    m_headerImportBtn->setCursor(Qt::PointingHandCursor);
    m_headerImportBtn->setToolTip(QString::fromUtf8("导入 RAW 文件"));
    m_headerImportBtn->setStyleSheet(
        "QPushButton { color: #F3F7F6; background-color: transparent; border: 1px solid #2B393B;"
        "border-radius: 5px; font-size: 18px; font-weight: 400; padding: 0; }"
        "QPushButton:hover { color: #111719; background-color: #4ED7AE; border-color: #4ED7AE; }"
        "QPushButton:pressed { background-color: #32B98F; border-color: #32B98F; }"
    );
    connect(m_headerImportBtn, &QPushButton::clicked, this, &ProjectPanel::onImportClicked);
    titleLayout->addWidget(m_headerImportBtn);
    layout->addWidget(titleBar);

    // 内容区域（文件列表或空状态）
    auto* contentWidget = new QWidget(this);
    contentWidget->setStyleSheet("background-color: #111719;");
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
        "QMenu { background-color: #171F21; color: #F3F7F6; border: 1px solid #2B393B; padding: 5px; }"
        "QMenu::item { padding: 7px 22px 7px 10px; border-radius: 4px; }"
        "QMenu::item:selected { background-color: #202A2D; color: #F3F7F6; }"
        "QMenu::separator { height: 1px; background-color: #2B393B; margin: 5px 8px; }"
    );

    m_referenceAction = new QAction(QString::fromUtf8("设为参考帧"), this);
    connect(m_referenceAction, &QAction::triggered, this, &ProjectPanel::onSetReferenceFrame);
    m_contextMenu->addAction(m_referenceAction);

    m_excludeAction = new QAction(QString::fromUtf8("排除 / 恢复"), this);
    connect(m_excludeAction, &QAction::triggered, this, &ProjectPanel::onExcludeSelected);
    m_contextMenu->addAction(m_excludeAction);

    m_contextMenu->addSeparator();

    m_metadataAction = new QAction(QString::fromUtf8("查看元数据"), this);
    connect(m_metadataAction, &QAction::triggered, this, &ProjectPanel::onViewMetadata);
    m_contextMenu->addAction(m_metadataAction);

    m_removeAction = new QAction(QString::fromUtf8("从列表移除"), this);
    connect(m_removeAction, &QAction::triggered, this, &ProjectPanel::onRemoveFromList);
    m_contextMenu->addAction(m_removeAction);
}

void ProjectPanel::setupEmptyState() {
    m_emptyState = new QWidget(this);
    m_emptyState->setStyleSheet("background-color: transparent;");
    m_emptyLayout = new QVBoxLayout(m_emptyState);
    m_emptyLayout->setContentsMargins(24, 24, 24, 24);
    m_emptyLayout->setSpacing(8);
    m_emptyLayout->setAlignment(Qt::AlignCenter);

    auto* textLabel = new QLabel(QString::fromUtf8("拖放 RAW 文件到这里"), m_emptyState);
    textLabel->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #F3F7F6; background-color: transparent;"
    );
    textLabel->setAlignment(Qt::AlignCenter);
    m_emptyLayout->addWidget(textLabel);

    auto* formatLabel = new QLabel(QString::fromUtf8("支持常见相机 RAW 格式"), m_emptyState);
    formatLabel->setStyleSheet("font-size: 11px; color: #889A96; background-color: transparent;");
    formatLabel->setAlignment(Qt::AlignCenter);
    m_emptyLayout->addWidget(formatLabel);

    m_emptyLayout->addSpacing(8);

    m_emptyImportBtn = new QPushButton(QString::fromUtf8("导入文件"), m_emptyState);
    m_emptyImportBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::AddPhotos, QColor("#A7B8B4")));
    m_emptyImportBtn->setIconSize(QSize(16, 16));
    m_emptyImportBtn->setFixedSize(112, 34);
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    m_emptyImportBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #202A2D;"
        "  color: #D2DDDA;"
        "  border: 1px solid #344548;"
        "  border-radius: 5px;"
        "  padding: 0 16px;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QPushButton:hover {"
        "  background-color: #273336;"
        "  border-color: #4D6265;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #344548;"
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
        "QScrollArea { background-color: #111719; border: none; }"
        "QScrollBar:vertical { background-color: #111719; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background-color: #2B393B; border-radius: 3px; min-height: 28px; }"
        "QScrollBar::handle:vertical:hover { background-color: #465B5E; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    m_listContainer = new QWidget();
    m_listContainer->setStyleSheet("background-color: #111719;");
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(8, 8, 8, 8);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch();

    m_scrollArea->setWidget(m_listContainer);
}

void ProjectPanel::setupBottomBar() {
    m_bottomLabel = new QLabel(this);
    m_bottomLabel->setFixedHeight(32);
    m_bottomLabel->setStyleSheet(
        "QLabel { background-color: #171F21; color: #889A96; font-size: 11px; "
        "padding: 4px 12px; border-top: 1px solid #2B393B; }"
    );
    m_bottomLabel->setText(QString::fromUtf8("可用 0 / 共 0 · 排除 0 · 参考 自动"));
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
    QStringList thumbnailQueue;
    for (const QString& filePath : filePaths) {
        if (findIndexByPath(filePath) >= 0) continue;

        FileItem item;
        item.filePath = filePath;
        item.fileName = QFileInfo(filePath).fileName();

        // 元数据提取已改为异步：由 ThumbnailGenerator 在 worker 线程加载
        // 完成后通过 metadataReady 信号传回主线程

        m_fileItems.append(item);
        addFileCard(item);
        thumbnailQueue.append(filePath);
        added = true;
    }

    if (added) {
        showFileList();
        // Import should immediately produce a useful preview instead of
        // leaving the centre canvas in its empty state.
        QString centralPreviewPath;
        if (m_currentIndex < 0 && !m_cards.isEmpty()) {
            setCurrentIndex(0);
            centralPreviewPath = m_fileItems[0].filePath;
        }
        // The selected first frame is decoded by PreviewPanel and feeds its
        // thumbnail back into this list. Skip a duplicate task for that file;
        // the remaining cards can populate concurrently in the background.
        for (const QString& filePath : thumbnailQueue) {
            if (filePath == centralPreviewPath) continue;
            if (centralPreviewPath.isEmpty()) {
                m_thumbnailGen->generateAsync(filePath, 96);
            } else {
                m_pendingThumbnailPaths.append(filePath);
            }
        }
        updateBottomBar();
        emit filesChanged();
    }
}

void ProjectPanel::applyPreviewData(const QString& filePath,
                                    const QImage& image, int iso,
                                    double exposureTime, double aperture,
                                    int focalLength) {
    const int index = findIndexByPath(filePath);
    if (index < 0 || image.isNull()) return;
    m_fileItems[index].thumbnail = QPixmap::fromImage(image.scaled(
        96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_fileItems[index].hasThumbnail = true;
    m_fileItems[index].iso = iso;
    m_fileItems[index].exposureTime = exposureTime;
    m_fileItems[index].aperture = aperture;
    m_fileItems[index].focalLength = focalLength;
    updateCard(index);
    updateBottomBar();
    for (const QString& pending : std::exchange(
             m_pendingThumbnailPaths, QStringList())) {
        m_thumbnailGen->generateAsync(pending, 96);
    }
}

void ProjectPanel::requestThumbnail(const QString& filePath) {
    if (findIndexByPath(filePath) >= 0) {
        m_thumbnailGen->generateAsync(filePath, 96);
    }
    for (const QString& pending : std::exchange(
             m_pendingThumbnailPaths, QStringList())) {
        m_thumbnailGen->generateAsync(pending, 96);
    }
}

void ProjectPanel::addFileCard(const FileItem& item) {
    auto* card = new FileCard(item, m_listContainer);
    connect(card, &FileCard::clicked, this, [this, card]() {
        setCurrentIndex(m_cards.indexOf(card));
    });
    connect(card, &FileCard::customContextMenuRequested, this, [this, card](const QPoint& pos) {
        m_contextMenuIndex = m_cards.indexOf(card);
        if (m_contextMenuIndex < 0) return;
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
    m_pendingThumbnailPaths.clear();
    m_currentIndex = -1;
    showEmptyState();
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::removeSelected() {
    if (m_currentIndex < 0 || m_currentIndex >= m_cards.size()) return;

    // Keep the canvas and list selection in sync after removing the active
    // source. Selecting the nearest surviving frame also invalidates any
    // asynchronous preview result that may arrive for the removed file.
    const int idx = m_currentIndex;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);
    m_currentIndex = -1;

    if (m_cards.isEmpty()) {
        showEmptyState();
        emit fileSelected(QString());
    } else {
        setCurrentIndex(std::min(idx, static_cast<int>(m_cards.size()) - 1));
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
    emit referenceFrameChanged();
}

void ProjectPanel::setReferenceFrame(int index) {
    if (index < 0 || index >= m_fileItems.size()) return;
    setReferenceFrame(m_fileItems[index].filePath);
}

QStringList ProjectPanel::includedFilePaths() const {
    QStringList paths;
    for (const auto& item : m_fileItems) {
        if (!item.isExcluded) {
            paths.append(item.filePath);
        }
    }
    return paths;
}

QString ProjectPanel::referenceFramePath() const {
    for (const auto& item : m_fileItems) {
        if (item.isReferenceFrame) return item.filePath;
    }
    return QString();
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
    const int total = m_fileItems.size();
    int excluded = 0;
    QString referenceName;
    for (const auto& item : m_fileItems) {
        if (item.isExcluded) {
            ++excluded;
        }
        if (item.isReferenceFrame) {
            referenceName = item.fileName;
        }
    }

    const int available = total - excluded;
    const QString reference = referenceName.isEmpty()
        ? QString::fromUtf8("自动")
        : referenceName;
    const QString prefix = QString::fromUtf8("可用 %1 / 共 %2 · 排除 %3 · 参考 ")
        .arg(available)
        .arg(total)
        .arg(excluded);
    const QString summary = prefix + reference;

    const int textWidth = qMax(0, m_bottomLabel->width() - 24);
    const int referenceWidth = qMax(
        0,
        textWidth - m_bottomLabel->fontMetrics().horizontalAdvance(prefix)
    );
    const QString elidedReference = m_bottomLabel->fontMetrics().elidedText(
        reference,
        Qt::ElideMiddle,
        referenceWidth
    );
    m_bottomLabel->setText(prefix + elidedReference);
    m_bottomLabel->setToolTip(summary);
    if (m_countLabel) {
        m_countLabel->setText(QString::number(total));
    }
}

void ProjectPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateBottomBar();
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

void ProjectPanel::onMetadataReady(const QString& filePath, int iso, double exposureTime, double aperture, int focalLength) {
    int idx = findIndexByPath(filePath);
    if (idx < 0) return;
    m_fileItems[idx].iso = iso;
    m_fileItems[idx].exposureTime = exposureTime;
    m_fileItems[idx].aperture = aperture;
    m_fileItems[idx].focalLength = focalLength;
    updateCard(idx);
    updateBottomBar();
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

    const int idx = m_contextMenuIndex;
    const bool removedCurrent = m_currentIndex == idx;
    auto* card = m_cards.takeAt(idx);
    card->deleteLater();
    m_fileItems.removeAt(idx);

    if (removedCurrent) {
        m_currentIndex = -1;
    } else if (m_currentIndex > idx) {
        m_currentIndex--;
    }

    if (m_cards.isEmpty()) {
        showEmptyState();
        emit fileSelected(QString());
    } else if (removedCurrent) {
        setCurrentIndex(std::min(idx, static_cast<int>(m_cards.size()) - 1));
    }
    updateAllCardStyles();
    updateBottomBar();
    emit filesChanged();
}

void ProjectPanel::onImportClicked() {
    // 空状态按钮点击时通知主窗口打开导入对话框
    emit filesDropped(QStringList());
}

void ProjectPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ProjectPanel::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    QStringList filePaths;
    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            filePaths.append(url.toLocalFile());
        }
    }

    if (!filePaths.isEmpty()) {
        addFiles(filePaths);
        emit filesDropped(filePaths);
    }
}
