#include "PreviewPanel.h"
#include "UiAssets.h"
#include "../core/PreviewToneMapper.h"
#include "../core/RawImageLoader.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QPointer>
#include <QMetaObject>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QDebug>
#include <QPainter>
#include <algorithm>
#include <cmath>

PreviewPanel::PreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    m_previewPool.setMaxThreadCount(1);
    m_previewPool.setExpiryTimeout(30000);
    setupUI();
}

PreviewPanel::~PreviewPanel() {
    ++m_previewGeneration;
    m_previewPool.clear();
    m_previewPool.waitForDone();
}

void PreviewPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    setupTopBar();
    layout->addWidget(m_topBar);

    // 图像区域（空状态 + 图像视图）
    auto* contentWidget = new QWidget(this);
    // The empty state is slightly lifted from the darker image canvas so the
    // central workspace stays legible before a source is selected.
    contentWidget->setStyleSheet("background-color: #1B2527;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    setupEmptyState();
    contentLayout->addWidget(m_emptyState, 1, Qt::AlignCenter);

    setupImageView();
    contentLayout->addWidget(m_scrollArea, 1);
    m_scrollArea->setVisible(false);

    layout->addWidget(contentWidget, 1);

    setupBottomBar();
    layout->addWidget(m_bottomBar);
}

void PreviewPanel::setupTopBar() {
    m_topBar = new QWidget(this);
    m_topBar->setFixedHeight(44);
    m_topBar->setStyleSheet("background-color: #171F21; border-bottom: 1px solid #2B393B;");

    auto* layout = new QHBoxLayout(m_topBar);
    layout->setContentsMargins(12, 0, 10, 0);
    layout->setSpacing(4);

    auto* title = new QLabel(QString::fromUtf8("预览"), m_topBar);
    title->setStyleSheet("font-size: 12px; font-weight: 700; color: #D2DDDA;");
    layout->addWidget(title);
    layout->addSpacing(8);

    auto createToolBtn = [this](const QString& text, const QString& tooltip) -> QPushButton* {
        auto* btn = new QPushButton(text, this);
        btn->setToolTip(tooltip);
        btn->setAccessibleName(tooltip);
        btn->setFixedHeight(28);
        btn->setIconSize(QSize(16, 16));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: transparent;"
            "  color: #A7B8B4;"
            "  border: 1px solid #344548;"
            "  border-radius: 5px;"
            "  padding: 2px 9px;"
            "  font-size: 11px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #273336;"
            "  color: #F3F7F6;"
            "  border-color: #4D6265;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #344548;"
            "}"
            "QPushButton:disabled {"
            "  color: #536763;"
            "  border-color: #263234;"
            "}"
        );
        return btn;
    };

    m_fitBtn = createToolBtn(QString(), QString::fromUtf8("适应视图"));
    m_fitBtn->setIcon(UiAssets::icon(UiAssets::Glyph::Fit, QColor("#A7B8B4")));
    m_fitBtn->setFixedWidth(30);
    connect(m_fitBtn, &QPushButton::clicked, this, &PreviewPanel::onFitView);
    layout->addWidget(m_fitBtn);

    m_zoom100Btn = createToolBtn("100%", QString::fromUtf8("显示实际像素"));
    connect(m_zoom100Btn, &QPushButton::clicked, this, &PreviewPanel::onZoom100);
    layout->addWidget(m_zoom100Btn);

    m_zoomInBtn = createToolBtn(QString(), QString::fromUtf8("放大"));
    m_zoomInBtn->setIcon(UiAssets::icon(UiAssets::Glyph::ZoomIn, QColor("#A7B8B4")));
    m_zoomInBtn->setFixedWidth(30);
    connect(m_zoomInBtn, &QPushButton::clicked, this, &PreviewPanel::onZoomIn);
    layout->addWidget(m_zoomInBtn);

    m_zoomOutBtn = createToolBtn(QString(), QString::fromUtf8("缩小"));
    m_zoomOutBtn->setIcon(UiAssets::icon(UiAssets::Glyph::ZoomOut, QColor("#A7B8B4")));
    m_zoomOutBtn->setFixedWidth(30);
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &PreviewPanel::onZoomOut);
    layout->addWidget(m_zoomOutBtn);

    m_zoomLabel = new QLabel("100%", m_topBar);
    m_zoomLabel->setMinimumWidth(38);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setStyleSheet("font-size: 10px; color: #81938F;");
    layout->addWidget(m_zoomLabel);

    layout->addStretch();

    m_resultBtn = createToolBtn(QString::fromUtf8("查看结果"),
                                QString::fromUtf8("返回最近一次处理结果"));
    m_resultBtn->setIcon(UiAssets::icon(UiAssets::Glyph::Result, QColor("#6FA8FF")));
    m_resultBtn->setVisible(false);
    connect(m_resultBtn, &QPushButton::clicked,
            this, &PreviewPanel::resultRequested);
    layout->addWidget(m_resultBtn);

    // 有真实的堆栈前预览后才显示三个比较模式。
    m_compareControl = new QWidget(m_topBar);
    m_compareControl->setStyleSheet(
        "QWidget { background-color: #111719; border: 1px solid #2B393B; border-radius: 5px; }"
        "QPushButton { background-color: transparent; color: #91A39F; border: none; "
        "  border-radius: 4px; padding: 4px 10px; font-size: 10px; }"
        "QPushButton:hover { color: #F3F7F6; }"
        "QPushButton:checked { background-color: #2B393B; color: #F3F7F6; font-weight: 700; }"
    );
    auto* compareLayout = new QHBoxLayout(m_compareControl);
    compareLayout->setContentsMargins(2, 2, 2, 2);
    compareLayout->setSpacing(0);
    m_viewModeGroup = new QButtonGroup(this);
    m_viewModeGroup->setExclusive(true);
    m_beforeBtn = new QPushButton(QString::fromUtf8("处理前"), m_compareControl);
    m_afterBtn = new QPushButton(QString::fromUtf8("处理后"), m_compareControl);
    m_splitBtn = new QPushButton(QString::fromUtf8("分屏"), m_compareControl);
    for (QPushButton* button : {m_beforeBtn, m_afterBtn, m_splitBtn}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        compareLayout->addWidget(button);
    }
    m_viewModeGroup->addButton(m_beforeBtn, 0);
    m_viewModeGroup->addButton(m_afterBtn, 1);
    m_viewModeGroup->addButton(m_splitBtn, 2);
    m_afterBtn->setChecked(true);
    connect(m_viewModeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &PreviewPanel::onViewModeChanged);
    m_compareControl->setVisible(false);
    layout->addWidget(m_compareControl);
    layout->addSpacing(6);

    m_infoBtn = createToolBtn(QString(), QString::fromUtf8("显示或隐藏图像信息"));
    m_infoBtn->setIcon(UiAssets::icon(UiAssets::Glyph::Info, QColor("#A7B8B4")));
    m_infoBtn->setFixedWidth(30);
    m_infoBtn->setCheckable(true);
    m_infoBtn->setChecked(true);
    connect(m_infoBtn, &QPushButton::clicked, this, &PreviewPanel::onToggleInfo);
    layout->addWidget(m_infoBtn);

}

void PreviewPanel::setupEmptyState() {
    m_emptyState = new QWidget(this);
    m_emptyState->setStyleSheet("background-color: transparent;");
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);

    m_emptyIcon = new QLabel(m_emptyState);
    m_emptyIcon->setFixedSize(72, 72);
    m_emptyIcon->setPixmap(UiAssets::logoMark(54));
    m_emptyIcon->setStyleSheet(
        "background-color: #171F21; "
        "border: 1px solid #344548; border-radius: 6px;"
    );
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIcon);

    m_emptyText = new QLabel(QString::fromUtf8("导入一组连续拍摄的 RAW 照片"), m_emptyState);
    m_emptyText->setStyleSheet("font-size: 14px; font-weight: 600; color: #D2DDDA; background-color: transparent;");
    m_emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyText);

    m_emptyFormat = new QLabel(
        QString::fromUtf8("支持 NEF, CR2, ARW, DNG, RAW, ORF, RAF, PEF, CR3"),
        m_emptyState
    );
    m_emptyFormat->setStyleSheet("font-size: 11px; color: #718681; background-color: transparent;");
    m_emptyFormat->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyFormat);

    m_emptyImportBtn = new QPushButton(QString::fromUtf8("添加照片"), m_emptyState);
    m_emptyImportBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::AddPhotos, QColor("#0D211B")));
    m_emptyImportBtn->setIconSize(QSize(16, 16));
    m_emptyImportBtn->setFixedHeight(36);
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    m_emptyImportBtn->setStyleSheet(
        "QPushButton {"
        "  background-color: #4ED7AE;"
        "  color: #0D211B;"
        "  border: none;"
        "  border-radius: 5px;"
        "  padding: 8px 20px;"
        "  font-size: 13px;"
        "  font-weight: 700;"
        "}"
        "QPushButton:hover {"
        "  background-color: #67E2BE;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #32B98F;"
        "}"
    );
    connect(m_emptyImportBtn, &QPushButton::clicked, this, &PreviewPanel::importRequested);
    emptyLayout->addWidget(m_emptyImportBtn, 0, Qt::AlignCenter);

}

void PreviewPanel::setupImageView() {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background-color: #090D0F; border: none; }"
        "QScrollBar:vertical { background-color: #111719; width: 10px; }"
        "QScrollBar::handle:vertical { background-color: #344548; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background-color: #4D6265; }"
        "QScrollBar:horizontal { background-color: #111719; height: 10px; }"
        "QScrollBar::handle:horizontal { background-color: #344548; border-radius: 5px; min-width: 24px; }"
        "QScrollBar::handle:horizontal:hover { background-color: #4D6265; }"
    );

    m_imageContainer = new QWidget();
    m_imageContainer->setStyleSheet("background-color: #090D0F;");
    auto* containerLayout = new QVBoxLayout(m_imageContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setStyleSheet("background-color: transparent;");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMouseTracking(true);
    m_imageLabel->installEventFilter(this);
    containerLayout->addWidget(m_imageLabel, 0, Qt::AlignCenter);

    m_scrollArea->setWidget(m_imageContainer);
}

void PreviewPanel::setupBottomBar() {
    m_bottomBar = new QWidget(this);
    m_bottomBar->setFixedHeight(34);
    m_bottomBar->setStyleSheet("background-color: #171F21; border-top: 1px solid #2B393B;");

    auto* layout = new QHBoxLayout(m_bottomBar);
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(12);

    m_bottomInfo = new QLabel(this);
    m_bottomInfo->setStyleSheet("font-size: 10px; color: #91A39F; background-color: transparent;");
    m_bottomInfo->setText(QString::fromUtf8("缩放: 100% | 就绪"));
    layout->addWidget(m_bottomInfo);

    m_mouseInfo = new QLabel(this);
    layout->addStretch();
    m_mouseInfo->setStyleSheet("font-size: 10px; color: #718681; background-color: transparent;");
    m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
    layout->addWidget(m_mouseInfo);
}

void PreviewPanel::loadImage(const QString& filePath) {
    clearMaskOverlay();
    const uint64_t generation = ++m_previewGeneration;
    m_previewPool.clear();
    if (filePath.isEmpty()) {
        clearImage();
        return;
    }

    QFileInfo info(filePath);
    if (!info.exists()) {
        qWarning() << "文件不存在:" << filePath;
        return;
    }
    m_currentFilePath = filePath;
    m_imageFileName = info.fileName();
    m_currentImage = QImage();
    m_showingResult = false;
    resetComparison();
    m_imageLabel->setPixmap(QPixmap());
    m_scrollArea->setVisible(false);
    m_emptyState->setVisible(true);
    m_emptyText->setText(QString::fromUtf8("正在生成 RAW 预览..."));
    m_emptyFormat->setText(info.fileName());
    m_emptyImportBtn->setVisible(false);
    m_bottomInfo->setText(QString::fromUtf8("正在加载 %1").arg(info.fileName()));

    QPointer<PreviewPanel> safePanel(this);
    m_previewPool.start([safePanel, filePath, generation]() {
        RawImageLoader loader;
        RawImageLoader::PreviewData preview;
        RawImageLoader::Metadata metadata;
        constexpr int kPreviewLongSide = 2400;
        const bool loaded = loader.loadPreview(
            filePath.toStdString(), kPreviewLongSide, preview, &metadata);

        QImage image;
        if (loaded && preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
            image = QImage::fromData(preview.bytes.data(),
                                     static_cast<int>(preview.bytes.size()), "JPEG");
        } else if (loaded && preview.width > 0 && preview.height > 0) {
            const QImage borrowed(preview.bytes.data(), preview.width,
                                  preview.height, preview.width * 3,
                                  QImage::Format_RGB888);
            image = borrowed.copy();
        }
        if (!image.isNull() &&
            std::max(image.width(), image.height()) > kPreviewLongSide) {
            image = image.scaled(kPreviewLongSide, kPreviewLongSide,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        }

        if (!safePanel) return;
        QMetaObject::invokeMethod(safePanel.data(),
            [safePanel, filePath, generation, image, metadata]() {
                if (!safePanel ||
                    safePanel->m_previewGeneration.load() != generation) {
                    return;
                }
                safePanel->m_emptyImportBtn->setVisible(true);
                if (image.isNull()) {
                    safePanel->m_emptyText->setText(
                        QString::fromUtf8("无法生成这张照片的预览"));
                    safePanel->m_emptyFormat->setText(
                        QString::fromUtf8("可选择其他素材，正式处理不会使用浏览预览"));
                    safePanel->m_bottomInfo->setText(
                        QString::fromUtf8("预览加载失败 | %1")
                            .arg(QFileInfo(filePath).fileName()));
                    emit safePanel->sourcePreviewFailed(filePath);
                    return;
                }

                safePanel->m_currentImage = image;
                safePanel->m_imageIso = metadata.iso;
                safePanel->m_imageExposure = metadata.exposureTime;
                safePanel->m_imageFocalLength = metadata.focalLength;
                safePanel->m_emptyState->setVisible(false);
                safePanel->m_scrollArea->setVisible(true);
                safePanel->updateImageDisplay();
                safePanel->onFitView();
                safePanel->updateZoomDisplay();
                emit safePanel->sourcePreviewReady(
                    filePath, image, metadata.iso, metadata.exposureTime,
                    metadata.aperture, metadata.focalLength);
            }, Qt::QueuedConnection);
    });
}

void PreviewPanel::loadImage(const QImage& image) {
    clearMaskOverlay();
    ++m_previewGeneration;
    m_previewPool.clear();
    if (image.isNull()) {
        clearImage();
        return;
    }
    m_currentImage = image;
    m_showingResult = false;
    resetComparison();

    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);

    updateImageDisplay();
    onFitView();
    updateZoomDisplay();
}

void PreviewPanel::load16BitImage(const std::vector<uint16_t>& data, int w, int h) {
    clearMaskOverlay();
    ++m_previewGeneration;
    m_previewPool.clear();
    const PreviewImage8 preview = PreviewToneMapper::mapMono16(data, w, h);
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }

    const QImage borrowed(preview.rgb.data(), preview.width, preview.height,
                          preview.width * 3, QImage::Format_RGB888);
    const QImage image = borrowed.copy();

    m_currentImage = image;
    m_showingResult = true;
    resetComparison();
    m_currentFilePath.clear();
    m_imageFileName = QString::fromUtf8("堆栈结果");
    m_imageIso = 0;
    m_imageExposure = 0.0;
    m_imageFocalLength = 0;

    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);

    updateImageDisplay();
    onFitView();
    updateZoomDisplay();
}

void PreviewPanel::loadRgb16BitImage(const std::vector<uint16_t>& rgb, int w, int h) {
    clearMaskOverlay();
    ++m_previewGeneration;
    m_previewPool.clear();
    const PreviewImage8 preview = PreviewToneMapper::mapRgb16(rgb, w, h);
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }

    const QImage borrowed(preview.rgb.data(), preview.width, preview.height,
                          preview.width * 3, QImage::Format_RGB888);
    const QImage image = borrowed.copy();

    m_currentImage = image;
    m_showingResult = true;
    resetComparison();
    m_currentFilePath.clear();
    m_imageFileName = QString::fromUtf8("堆栈结果");
    m_imageIso = 0;
    m_imageExposure = 0.0;
    m_imageFocalLength = 0;

    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);

    updateImageDisplay();
    onFitView();
    updateZoomDisplay();
}

void PreviewPanel::loadRgb16BitComparison(const QImage& before,
                                           const std::vector<uint16_t>& afterRgb,
                                           int w, int h,
                                           uint16_t blackPoint,
                                           uint16_t whitePoint) {
    clearMaskOverlay();
    ++m_previewGeneration;
    m_previewPool.clear();
    const int comparisonLongSide = before.isNull()
        ? 2400 : std::max(before.width(), before.height());
    PreviewImage8 preview = PreviewToneMapper::mapRgb16WithRange(
        afterRgb, w, h, blackPoint, whitePoint, comparisonLongSide);
    if (preview.rgb.empty()) {
        preview = PreviewToneMapper::mapRgb16(
            afterRgb, w, h, comparisonLongSide);
    }
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }

    const QImage borrowed(preview.rgb.data(), preview.width, preview.height,
                          preview.width * 3, QImage::Format_RGB888);
    const QImage after = borrowed.copy();
    m_currentImage = after;
    m_showingResult = true;
    m_currentFilePath.clear();
    m_imageFileName = QString::fromUtf8("处理结果");
    m_imageIso = 0;
    m_imageExposure = 0.0;
    m_imageFocalLength = 0;

    // Both sides use the pre-finishing display range and the same long-side
    // limit, so the split does not manufacture contrast or sharpness changes.
    if (!before.isNull()) {
        setComparisonImages(
            before.size() == after.size()
                ? before
                : before.scaled(after.size(), Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation),
            after);
    } else {
        resetComparison();
    }

    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);
    updateImageDisplay();
    onFitView();
    updateZoomDisplay();
}

void PreviewPanel::clearImage() {
    ++m_previewGeneration;
    m_previewPool.clear();
    m_maskOverlay = QImage();
    m_maskOverlayVisible = false;
    m_currentImage = QImage();
    m_beforeImage = QImage();
    m_afterImage = QImage();
    m_currentFilePath.clear();
    m_imageFileName.clear();
    m_imageIso = 0;
    m_imageExposure = 0.0;
    m_imageFocalLength = 0;
    m_zoom = 1.0;
    m_showingResult = false;
    m_viewMode = 1;
    m_beforeAfterMode = false;
    if (m_compareControl) m_compareControl->setVisible(false);
    if (m_afterBtn) m_afterBtn->setChecked(true);
    emit comparisonAvailabilityChanged(false);

    m_imageLabel->setPixmap(QPixmap());
    m_emptyState->setVisible(true);
    m_emptyText->setText(QString::fromUtf8("导入一组连续拍摄的 RAW 照片"));
    m_emptyFormat->setText(
        QString::fromUtf8("支持 NEF, CR2, ARW, DNG, RAW, ORF, RAF, PEF, CR3"));
    m_emptyImportBtn->setVisible(true);
    m_scrollArea->setVisible(false);
    m_bottomInfo->setText(QString::fromUtf8("缩放: 100% | 就绪"));
    m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
}

void PreviewPanel::setZoom(double zoom) {
    m_zoom = std::clamp(zoom, 0.01, maximumSafeZoom());
    applyZoom();
    updateZoomDisplay();
    emit zoomChanged(m_zoom);
}

double PreviewPanel::zoom() const {
    return m_zoom;
}

void PreviewPanel::setInfo(const QString& info) {
    m_bottomInfo->setText(info);
}

void PreviewPanel::setResultLabel(const QString& label) {
    if (!m_showingResult) return;
    m_imageFileName = label;
    updateZoomDisplay();
}

void PreviewPanel::onFitView() {
    if (m_currentImage.isNull()) return;

    int viewW = m_scrollArea->viewport()->width() - 16;
    int viewH = m_scrollArea->viewport()->height() - 16;
    if (viewW <= 0 || viewH <= 0) return;

    double scaleW = double(viewW) / m_currentImage.width();
    double scaleH = double(viewH) / m_currentImage.height();
    m_zoom = std::clamp(std::min(scaleW, scaleH), 0.01, maximumSafeZoom());

    applyZoom();
    updateZoomDisplay();
    emit zoomChanged(m_zoom);
}

void PreviewPanel::onZoom100() {
    setZoom(1.0);
}

void PreviewPanel::onZoomIn() {
    setZoom(m_zoom * 1.25);
}

void PreviewPanel::onZoomOut() {
    setZoom(m_zoom * 0.8);
}

void PreviewPanel::onToggleBeforeAfter() {
    if (!hasComparison()) return;
    m_beforeAfterMode = true;
    m_viewMode = 2;
    if (m_splitBtn) m_splitBtn->setChecked(true);
    applyZoom();
}

void PreviewPanel::onViewModeChanged(int id) {
    if (!hasComparison()) return;
    m_viewMode = std::clamp(id, 0, 2);
    m_beforeAfterMode = m_viewMode == 2;
    emit beforeAfterModeChanged(m_beforeAfterMode);
    applyZoom();
}

void PreviewPanel::onToggleInfo() {
    m_showInfo = m_infoBtn->isChecked();
    m_bottomBar->setVisible(m_showInfo);
}

void PreviewPanel::updateImageDisplay() {
    if (m_currentImage.isNull()) return;
    applyZoom();
}

void PreviewPanel::applyZoom() {
    const QImage& image = displayedImage();
    if (image.isNull()) return;

    const int w = std::max(1, qRound(image.width() * m_zoom));
    const int h = std::max(1, qRound(image.height() * m_zoom));

    QPixmap pixmap;
    if (m_viewMode == 2 && hasComparison()) {
        const QPixmap before = QPixmap::fromImage(m_beforeImage).scaled(
            w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const QPixmap after = QPixmap::fromImage(m_afterImage).scaled(
            w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        pixmap = QPixmap(w, h);
        pixmap.fill(Qt::transparent);
        QPainter comparisonPainter(&pixmap);
        comparisonPainter.drawPixmap(0, 0, before);
        comparisonPainter.save();
        comparisonPainter.setClipRect(w / 2, 0, w - w / 2, h);
        comparisonPainter.drawPixmap(0, 0, after);
        comparisonPainter.restore();
        comparisonPainter.setPen(QPen(QColor("#F5FAF8"), 1));
        comparisonPainter.drawLine(w / 2, 0, w / 2, h);
        if (w > 180 && h > 48) {
            comparisonPainter.setPen(Qt::NoPen);
            comparisonPainter.setBrush(QColor(12, 14, 16, 190));
            comparisonPainter.drawRoundedRect(QRect(8, 8, 52, 22), 4, 4);
            comparisonPainter.drawRoundedRect(QRect(w - 60, 8, 52, 22), 4, 4);
            comparisonPainter.setPen(QColor("#F5FAF8"));
            comparisonPainter.drawText(QRect(8, 8, 52, 22), Qt::AlignCenter,
                                       QString::fromUtf8("处理前"));
            comparisonPainter.drawText(QRect(w - 60, 8, 52, 22), Qt::AlignCenter,
                                       QString::fromUtf8("处理后"));
        }
        comparisonPainter.end();
    } else {
        pixmap = QPixmap::fromImage(image);
        if (w != image.width() || h != image.height()) {
            pixmap = pixmap.scaled(w, h, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        }
    }

    // 叠加蒙版
    if (m_maskOverlayVisible && !m_maskOverlay.isNull()) {
        QPainter painter(&pixmap);
        QImage scaledMask = m_maskOverlay.scaled(pixmap.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(0, 0, scaledMask);
        painter.end();
    }

    m_imageLabel->setPixmap(pixmap);
    m_imageLabel->setFixedSize(pixmap.size());
}

double PreviewPanel::maximumSafeZoom() const {
    const QImage& image = displayedImage();
    if (image.isNull()) return 1.0;

    // A scaled QPixmap may require several temporary copies. Bound it to about
    // 32 megapixels so zoom cannot allocate multiple gigabytes on a large frame.
    constexpr double maxRenderedPixels = 32.0 * 1024.0 * 1024.0;
    const double pixels = static_cast<double>(image.width()) * image.height();
    return std::clamp(std::sqrt(maxRenderedPixels / std::max(1.0, pixels)), 1.0, 8.0);
}

const QImage& PreviewPanel::displayedImage() const {
    if (m_viewMode == 0 && hasComparison()) return m_beforeImage;
    if ((m_viewMode == 1 || m_viewMode == 2) && hasComparison()) return m_afterImage;
    return m_currentImage;
}

void PreviewPanel::updateZoomDisplay() {
    QString info;
    if (!m_currentImage.isNull()) {
        info = QString::fromUtf8("缩放: %1% | %2×%3")
            .arg(qRound(m_zoom * 100))
            .arg(m_currentImage.width())
            .arg(m_currentImage.height());

        if (m_imageIso > 0) {
            info += QString::fromUtf8(" | ISO %1").arg(m_imageIso);
        }
        if (m_imageExposure > 0) {
            info += QString::fromUtf8(" | %1").arg(formatExposureTime(m_imageExposure));
        }
        if (m_imageFocalLength > 0) {
            info += QString::fromUtf8(" | %1mm").arg(m_imageFocalLength);
        }
        if (!m_imageFileName.isEmpty()) {
            info += QString::fromUtf8(" | %1").arg(m_imageFileName);
        }
    } else {
        info = QString::fromUtf8("缩放: %1% | 就绪").arg(qRound(m_zoom * 100));
    }
    m_bottomInfo->setText(info);
    if (m_zoomLabel) {
        m_zoomLabel->setText(QString("%1%").arg(qRound(m_zoom * 100)));
    }
}

void PreviewPanel::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double delta = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        setZoom(m_zoom * delta);
        event->accept();
    } else {
        // 普通滚轮传递给滚动区域
        QWidget::wheelEvent(event);
    }
}

void PreviewPanel::mousePressEvent(QMouseEvent* event) {
    if (!m_currentImage.isNull() && event->button() == Qt::LeftButton) {
        m_panning = true;
        m_lastPanPos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void PreviewPanel::mouseMoveEvent(QMouseEvent* event) {
    if (m_panning) {
        QPoint delta = event->pos() - m_lastPanPos;
        m_lastPanPos = event->pos();

        m_scrollArea->horizontalScrollBar()->setValue(
            m_scrollArea->horizontalScrollBar()->value() - delta.x()
        );
        m_scrollArea->verticalScrollBar()->setValue(
            m_scrollArea->verticalScrollBar()->value() - delta.y()
        );
    }

    // 鼠标像素信息
    if (!m_currentImage.isNull() && m_scrollArea->isVisible()) {
        QPoint labelPos = m_imageLabel->mapFrom(this, event->pos());
        int x = int(labelPos.x() / m_zoom);
        int y = int(labelPos.y() / m_zoom);

        if (x >= 0 && x < m_currentImage.width() && y >= 0 && y < m_currentImage.height()) {
            QRgb pixel = m_currentImage.pixel(x, y);
            emit mousePixelInfo(x, y, qRed(pixel), qGreen(pixel), qBlue(pixel));
            m_mouseInfo->setText(
                QString::fromUtf8("鼠标: %1,%2 | RGB: (%3, %4, %5)")
                    .arg(x).arg(y)
                    .arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel))
            );
        } else {
            m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
        }
    }

    QWidget::mouseMoveEvent(event);
}

void PreviewPanel::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
    QWidget::mouseReleaseEvent(event);
}

bool PreviewPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched != m_imageLabel || m_currentImage.isNull()) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers() & Qt::ControlModifier) {
            setZoom(m_zoom * (wheel->angleDelta().y() > 0 ? 1.25 : 0.8));
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_panning = true;
            m_lastPanPos = mouse->globalPosition().toPoint();
            m_imageLabel->setCursor(Qt::ClosedHandCursor);
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_panning) {
            const QPoint global = mouse->globalPosition().toPoint();
            const QPoint delta = global - m_lastPanPos;
            m_lastPanPos = global;
            m_scrollArea->horizontalScrollBar()->setValue(
                m_scrollArea->horizontalScrollBar()->value() - delta.x());
            m_scrollArea->verticalScrollBar()->setValue(
                m_scrollArea->verticalScrollBar()->value() - delta.y());
        }

        const QPoint position = mouse->position().toPoint();
        const int labelWidth = std::max(1, m_imageLabel->width());
        const int labelHeight = std::max(1, m_imageLabel->height());
        const QImage* sampled = &m_currentImage;
        if (hasComparison()) {
            if (m_viewMode == 0 ||
                (m_viewMode == 2 && position.x() < labelWidth / 2)) {
                sampled = &m_beforeImage;
            } else {
                sampled = &m_afterImage;
            }
        }
        const int x = static_cast<int>(
            static_cast<int64_t>(position.x()) * sampled->width() / labelWidth);
        const int y = static_cast<int>(
            static_cast<int64_t>(position.y()) * sampled->height() / labelHeight);
        if (x >= 0 && x < sampled->width() && y >= 0 && y < sampled->height()) {
            const QRgb pixel = sampled->pixel(x, y);
            emit mousePixelInfo(x, y, qRed(pixel), qGreen(pixel), qBlue(pixel));
            m_mouseInfo->setText(
                QString::fromUtf8("鼠标: %1,%2 | RGB: (%3, %4, %5)")
                    .arg(x).arg(y)
                    .arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel)));
        }
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            m_panning = false;
            m_imageLabel->setCursor(Qt::ArrowCursor);
            return true;
        }
    }

    if (event->type() == QEvent::Leave) {
        m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
    }
    return QWidget::eventFilter(watched, event);
}

void PreviewPanel::fitToView() {
    onFitView();
}

void PreviewPanel::resetZoom() {
    onZoom100();
}

void PreviewPanel::setBeforeAfterMode(bool enabled) {
    if (!hasComparison()) return;
    m_viewMode = enabled ? 2 : 1;
    m_beforeAfterMode = enabled;
    if (enabled && m_splitBtn) m_splitBtn->setChecked(true);
    if (!enabled && m_afterBtn) m_afterBtn->setChecked(true);
    emit beforeAfterModeChanged(enabled);
    applyZoom();
}

void PreviewPanel::setBeforeImage(const QImage& image) {
    m_beforeImage = image;
    if (!m_beforeImage.isNull() && !m_afterImage.isNull()) {
        setComparisonImages(m_beforeImage, m_afterImage);
    }
}

void PreviewPanel::setAfterImage(const QImage& image) {
    m_afterImage = image;
    if (!m_beforeImage.isNull() && !m_afterImage.isNull()) {
        setComparisonImages(m_beforeImage, m_afterImage);
    }
}

void PreviewPanel::setComparisonImages(const QImage& before, const QImage& after) {
    if (before.isNull() || after.isNull() || before.size() != after.size()) {
        resetComparison();
        return;
    }
    m_beforeImage = before;
    m_afterImage = after;
    m_currentImage = after;
    m_viewMode = 1;
    m_beforeAfterMode = false;
    if (m_afterBtn) m_afterBtn->setChecked(true);
    if (m_compareControl) m_compareControl->setVisible(true);
    emit comparisonAvailabilityChanged(true);
    emit beforeAfterModeChanged(false);
}

bool PreviewPanel::hasComparison() const {
    return !m_beforeImage.isNull() && !m_afterImage.isNull()
        && m_beforeImage.size() == m_afterImage.size();
}

bool PreviewPanel::isShowingResult() const {
    return m_showingResult;
}

void PreviewPanel::setResultAvailable(bool available, bool viewingResult) {
    if (m_resultBtn) m_resultBtn->setVisible(available && !viewingResult);
}

void PreviewPanel::clearComparison() {
    resetComparison();
}

void PreviewPanel::resetComparison() {
    m_beforeImage = QImage();
    m_afterImage = QImage();
    m_viewMode = 1;
    m_beforeAfterMode = false;
    if (m_afterBtn) m_afterBtn->setChecked(true);
    if (m_compareControl) m_compareControl->setVisible(false);
    emit comparisonAvailabilityChanged(false);
    emit beforeAfterModeChanged(false);
}

QImage PreviewPanel::currentImage() const {
    return m_currentImage;
}

void PreviewPanel::setMaskOverlay(const std::vector<uint8_t>& mask, int w, int h) {
    if (w <= 0 || h <= 0 || mask.size() != static_cast<size_t>(w * h)) return;

    m_maskOverlay = QImage(w, h, QImage::Format_ARGB32);
    m_maskOverlay.fill(Qt::transparent);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t val = mask[y * w + x];
            float a = val / 255.0f;
            // 天空(255)=蓝色(68,136,255)，地景(0)=绿色(68,255,136)
            int r = 68;
            int g = static_cast<int>(255 + a * (136 - 255));
            int b = static_cast<int>(136 + a * (255 - 136));
            m_maskOverlay.setPixelColor(x, y, QColor(r, g, b, 77));
        }
    }
    m_maskOverlayVisible = true;
    updateImageDisplay();
}

void PreviewPanel::clearMaskOverlay() {
    m_maskOverlay = QImage();
    m_maskOverlayVisible = false;
    updateImageDisplay();
}

QString PreviewPanel::formatExposureTime(double seconds) const {
    if (seconds >= 1.0) {
        return QString("%1s").arg(seconds, 0, 'f', 1);
    } else if (seconds > 0) {
        return QString("1/%1s").arg(qRound(1.0 / seconds));
    }
    return QString();
}
