#include "PreviewPanel.h"
#include "StyleTokens.h"
#include "UiAssets.h"
#include "../core/PreviewToneMapper.h"
#include "../core/RawImageLoader.h"
#include "../core/SkyGroundMask.h"
#include <QScrollArea>
#include <QFrame>
#include <QStackedLayout>
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
#include <QSlider>
#include <QFileInfo>
#include <QDebug>
#include <QPainter>
#include <QTimer>
#include <QVariantAnimation>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QStyle>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kPreviewMargin = StyleTokens::Spacing::kCanvasPadding * 2;
constexpr int kSplitGutter = 12;

QColor tokenColor(const char* value) {
    return QColor(QString::fromLatin1(value));
}

void setButtonVariant(QPushButton* button, const char* variant) {
    if (!button) return;
    button->setProperty(StyleTokens::Properties::kVariant,
                        QString::fromLatin1(variant));
    button->style()->unpolish(button);
    button->style()->polish(button);
}

} // namespace

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

    m_canvasHost = new QWidget(this);
    m_canvasHost->setObjectName("previewCanvas");
    m_canvasHost->setProperty(StyleTokens::Properties::kUiRole,
                              StyleTokens::Properties::kCanvas);
    m_canvasHost->installEventFilter(this);

    setupEmptyState();
    setupImageView();
    auto* contentStack = new QStackedLayout(m_canvasHost);
    contentStack->setContentsMargins(0, 0, 0, 0);
    contentStack->setStackingMode(QStackedLayout::StackAll);
    contentStack->addWidget(m_emptyState);
    contentStack->addWidget(m_scrollArea);
    m_scrollArea->setVisible(false);
    layout->addWidget(m_canvasHost, 1);

    setupTopBar();
    setupBottomBar();
    m_resultSummary = new QWidget(m_canvasHost);
    m_resultSummary->setObjectName(QStringLiteral("resultSummaryOverlay"));
    m_resultSummary->setProperty(StyleTokens::Properties::kUiRole,
                                 StyleTokens::Properties::kOverlay);
    auto* summaryLayout = new QVBoxLayout(m_resultSummary);
    summaryLayout->setContentsMargins(12, 8, 12, 8);
    m_resultSummaryLabel = new QLabel(m_resultSummary);
    m_resultSummaryLabel->setProperty(
        StyleTokens::Properties::kTextRole,
        StyleTokens::Properties::kMono);
    summaryLayout->addWidget(m_resultSummaryLabel);
    m_resultSummary->hide();
    m_resultSummaryTimer = new QTimer(this);
    m_resultSummaryTimer->setSingleShot(true);
    m_resultSummaryTimer->setInterval(3500);
    connect(m_resultSummaryTimer, &QTimer::timeout,
            m_resultSummary, &QWidget::hide);
    m_topBar->raise();
    m_maskEditControls->raise();
    m_bottomBar->raise();
    m_resultSummary->raise();
}

void PreviewPanel::setupTopBar() {
    m_topBar = new QWidget(m_canvasHost);
    m_topBar->setObjectName("previewFloatingToolbar");
    m_topBar->setProperty(StyleTokens::Properties::kUiRole,
                          StyleTokens::Properties::kOverlay);
    m_topBar->setFixedHeight(32);
    m_topBar->setVisible(false);

    auto* layout = new QHBoxLayout(m_topBar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    auto createToolBtn = [](QWidget* parent, const QString& text,
                            const QString& tooltip) -> QPushButton* {
        auto* btn = new QPushButton(text, parent);
        btn->setToolTip(tooltip);
        btn->setAccessibleName(tooltip);
        btn->setProperty(StyleTokens::Properties::kVariant,
                         StyleTokens::Properties::kGhost);
        btn->setIconSize(QSize(16, 16));
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    m_resultBtn = createToolBtn(m_topBar, QString::fromUtf8("结果"),
                                QString::fromUtf8("回到当前处理结果 (R)"));
    m_resultBtn->setIcon(UiAssets::icon(
        UiAssets::Glyph::Result,
        tokenColor(StyleTokens::Colors::kAccent)));
    m_resultBtn->setVisible(false);
    connect(m_resultBtn, &QPushButton::clicked,
            this, &PreviewPanel::resultRequested);
    layout->addWidget(m_resultBtn);

    m_fitBtn = createToolBtn(m_topBar, QString(),
                             QString::fromUtf8("适应视图 (F)"));
    m_fitBtn->setProperty(StyleTokens::Properties::kVariant,
                          StyleTokens::Properties::kIcon);
    m_fitBtn->setIcon(UiAssets::icon(
        UiAssets::Glyph::Fit,
        tokenColor(StyleTokens::Colors::kTextSecondary)));
    connect(m_fitBtn, &QPushButton::clicked, this, &PreviewPanel::onFitView);
    layout->addWidget(m_fitBtn);

    m_zoom100Btn = createToolBtn(m_topBar, "100%",
                                 QString::fromUtf8("显示实际像素 (1)"));
    connect(m_zoom100Btn, &QPushButton::clicked, this, &PreviewPanel::onZoom100);
    layout->addWidget(m_zoom100Btn);

    m_zoomOutBtn = createToolBtn(m_topBar, QString(), QString::fromUtf8("缩小 (-)"));
    m_zoomOutBtn->setProperty(StyleTokens::Properties::kVariant,
                              StyleTokens::Properties::kIcon);
    m_zoomOutBtn->setIcon(UiAssets::icon(
        UiAssets::Glyph::ZoomOut,
        tokenColor(StyleTokens::Colors::kTextSecondary)));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &PreviewPanel::onZoomOut);
    layout->addWidget(m_zoomOutBtn);

    m_zoomInBtn = createToolBtn(m_topBar, QString(), QString::fromUtf8("放大 (+)"));
    m_zoomInBtn->setProperty(StyleTokens::Properties::kVariant,
                             StyleTokens::Properties::kIcon);
    m_zoomInBtn->setIcon(UiAssets::icon(
        UiAssets::Glyph::ZoomIn,
        tokenColor(StyleTokens::Colors::kTextSecondary)));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &PreviewPanel::onZoomIn);
    layout->addWidget(m_zoomInBtn);

    m_zoomLabel = new QLabel("100%", m_topBar);
    m_zoomLabel->setProperty(StyleTokens::Properties::kTextRole,
                             StyleTokens::Properties::kMono);
    m_zoomLabel->setMinimumWidth(42);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_zoomLabel);

    // 蒙版编辑工具在主浮层下独立排列，避免窄画布时挤压缩放控件。
    m_maskEditControls = new QWidget(m_canvasHost);
    m_maskEditControls->setObjectName("previewMaskToolbar");
    m_maskEditControls->setProperty(StyleTokens::Properties::kUiRole,
                                    StyleTokens::Properties::kOverlay);
    m_maskEditControls->setFixedHeight(32);
    auto* maskLayout = new QHBoxLayout(m_maskEditControls);
    maskLayout->setContentsMargins(4, 2, 4, 2);
    maskLayout->setSpacing(4);
    m_maskBrushBtn = createToolBtn(m_maskEditControls,
        QString::fromUtf8("修补地景"),
        QString::fromUtf8("粗略涂过漏检地景，松手后自动贴合真实边缘"));
    m_maskBrushBtn->setObjectName("maskGroundHintButton");
    m_maskBrushBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Brush,
                       tokenColor(StyleTokens::Colors::kWarning)));
    m_maskBrushBtn->setCheckable(true);
    m_maskBrushBtn->setChecked(true);
    connect(m_maskBrushBtn, &QPushButton::toggled, this,
            [this](bool checked) {
                m_maskEditingActive = checked && !m_initialMask.empty();
                updateImageCursor();
            });
    maskLayout->addWidget(m_maskBrushBtn);

    m_maskBrushSize = new QSlider(Qt::Horizontal, m_maskEditControls);
    m_maskBrushSize->setObjectName("maskGroundHintSize");
    m_maskBrushSize->setRange(8, 120);
    m_maskBrushSize->setValue(36);
    m_maskBrushSize->setFixedWidth(84);
    m_maskBrushSize->setToolTip(QString::fromUtf8("粗略提示笔刷直径"));
    maskLayout->addWidget(m_maskBrushSize);
    m_maskBrushSizeLabel = new QLabel("36 px", m_maskEditControls);
    m_maskBrushSizeLabel->setProperty(StyleTokens::Properties::kTextRole,
                                      StyleTokens::Properties::kMono);
    m_maskBrushSizeLabel->setProperty(StyleTokens::Properties::kStatusRole,
                                      StyleTokens::Properties::kWarning);
    m_maskBrushSizeLabel->setMinimumWidth(40);
    connect(m_maskBrushSize, &QSlider::valueChanged, this,
            [this](int value) {
                m_maskBrushSizeLabel->setText(QString("%1 px").arg(value));
            });
    maskLayout->addWidget(m_maskBrushSizeLabel);

    m_maskUndoBtn = createToolBtn(m_maskEditControls, QString(),
                                  QString::fromUtf8("撤销上一笔"));
    m_maskUndoBtn->setProperty(StyleTokens::Properties::kVariant,
                               StyleTokens::Properties::kIcon);
    m_maskUndoBtn->setObjectName("maskGroundHintUndo");
    m_maskUndoBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Undo,
                       tokenColor(StyleTokens::Colors::kTextSecondary)));
    connect(m_maskUndoBtn, &QPushButton::clicked,
            this, &PreviewPanel::undoGroundHint);
    maskLayout->addWidget(m_maskUndoBtn);
    m_maskResetBtn = createToolBtn(m_maskEditControls, QString(),
                                   QString::fromUtf8("恢复自动检测"));
    m_maskResetBtn->setProperty(StyleTokens::Properties::kVariant,
                                StyleTokens::Properties::kIcon);
    m_maskResetBtn->setObjectName("maskGroundHintReset");
    m_maskResetBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Reset,
                       tokenColor(StyleTokens::Colors::kTextSecondary)));
    connect(m_maskResetBtn, &QPushButton::clicked,
            this, &PreviewPanel::resetGroundHints);
    maskLayout->addWidget(m_maskResetBtn);
    m_maskDoneBtn = createToolBtn(m_maskEditControls, QString(),
                                  QString::fromUtf8("完成蒙版修补"));
    m_maskDoneBtn->setProperty(StyleTokens::Properties::kVariant,
                               StyleTokens::Properties::kIcon);
    m_maskDoneBtn->setObjectName("maskGroundHintDone");
    m_maskDoneBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::Done,
                       tokenColor(StyleTokens::Colors::kOk)));
    connect(m_maskDoneBtn, &QPushButton::clicked, this, [this]() {
        m_maskBrushBtn->setChecked(false);
    });
    maskLayout->addWidget(m_maskDoneBtn);
    m_maskEditControls->setVisible(false);

    // 有真实的堆栈前预览后才显示三个比较模式。
    m_compareControl = new QWidget(m_topBar);
    m_compareControl->setProperty(StyleTokens::Properties::kUiRole,
                                  StyleTokens::Properties::kRaised);
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
        button->setProperty(StyleTokens::Properties::kVariant,
                            StyleTokens::Properties::kGhost);
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

    m_maskOverlayBtn = createToolBtn(m_topBar, QString::fromUtf8("蒙版"),
                                     QString::fromUtf8("显示或隐藏蒙版叠加 (M)"));
    m_maskOverlayBtn->setCheckable(true);
    m_maskOverlayBtn->setVisible(false);
    connect(m_maskOverlayBtn, &QPushButton::clicked,
            this, &PreviewPanel::onToggleMaskOverlay);
    layout->addWidget(m_maskOverlayBtn);

    m_infoBtn = createToolBtn(m_topBar, QString(),
                              QString::fromUtf8("灰点吸管 (G)"));
    m_infoBtn->setProperty(StyleTokens::Properties::kVariant,
                           StyleTokens::Properties::kIcon);
    m_infoBtn->setIcon(UiAssets::icon(
        UiAssets::Glyph::Eyedropper,
        tokenColor(StyleTokens::Colors::kTextSecondary)));
    m_infoBtn->setCheckable(true);
    m_infoBtn->setChecked(false);
    connect(m_infoBtn, &QPushButton::clicked,
            this, &PreviewPanel::onTogglePointSelection);
    layout->addWidget(m_infoBtn);
}

void PreviewPanel::setupEmptyState() {
    m_emptyState = new QWidget(m_canvasHost);
    m_emptyState->setProperty(StyleTokens::Properties::kUiRole,
                              StyleTokens::Properties::kCanvas);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);

    m_emptyIcon = new QLabel(m_emptyState);
    m_emptyIcon->setFixedSize(72, 72);
    m_emptyIcon->setPixmap(UiAssets::icon(
        UiAssets::Glyph::Nightscape,
        tokenColor(StyleTokens::Colors::kLineStrong), 64).pixmap(64, 64));
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIcon);

    m_emptyText = new QLabel(QString::fromUtf8("今晚的星空还在等你"), m_emptyState);
    m_emptyText->setProperty(StyleTokens::Properties::kTextRole,
                             StyleTokens::Properties::kDisplay);
    m_emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyText);

    m_emptyFormat = new QLabel(
        QString::fromUtf8("拖入一组 RAW，或选择文件"),
        m_emptyState
    );
    m_emptyFormat->setProperty(StyleTokens::Properties::kTextRole,
                               StyleTokens::Properties::kSecondary);
    m_emptyFormat->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyFormat);

    m_emptyImportBtn = new QPushButton(QString::fromUtf8("选择文件"), m_emptyState);
    m_emptyImportBtn->setProperty(StyleTokens::Properties::kVariant,
                                  StyleTokens::Properties::kPrimary);
    m_emptyImportBtn->setIcon(
        UiAssets::icon(UiAssets::Glyph::AddPhotos,
                       tokenColor(StyleTokens::Colors::kActionText)));
    m_emptyImportBtn->setIconSize(QSize(16, 16));
    m_emptyImportBtn->setCursor(Qt::PointingHandCursor);
    connect(m_emptyImportBtn, &QPushButton::clicked, this, &PreviewPanel::importRequested);
    emptyLayout->addWidget(m_emptyImportBtn, 0, Qt::AlignCenter);

}

void PreviewPanel::setupImageView() {
    m_scrollArea = new QScrollArea(m_canvasHost);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setProperty(StyleTokens::Properties::kUiRole,
                              StyleTokens::Properties::kCanvas);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_imageContainer = new QWidget();
    m_imageContainer->setProperty(StyleTokens::Properties::kUiRole,
                                  StyleTokens::Properties::kCanvas);
    auto* containerLayout = new QVBoxLayout(m_imageContainer);
    containerLayout->setContentsMargins(
        StyleTokens::Spacing::kCanvasPadding,
        StyleTokens::Spacing::kCanvasPadding,
        StyleTokens::Spacing::kCanvasPadding,
        StyleTokens::Spacing::kCanvasPadding);
    containerLayout->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setObjectName("previewImageLabel");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setMouseTracking(true);
    m_imageLabel->setFocusPolicy(Qt::StrongFocus);
    m_imageLabel->installEventFilter(this);
    m_scrollArea->viewport()->installEventFilter(this);
    containerLayout->addWidget(m_imageLabel, 0, Qt::AlignCenter);

    m_scrollArea->setWidget(m_imageContainer);
}

void PreviewPanel::setupBottomBar() {
    m_bottomBar = new QWidget(m_canvasHost);
    m_bottomBar->setObjectName("previewInfoOverlay");
    m_bottomBar->setProperty(StyleTokens::Properties::kUiRole,
                             StyleTokens::Properties::kOverlay);
    m_bottomBar->setFixedHeight(26);
    m_bottomBar->setVisible(false);

    auto* layout = new QHBoxLayout(m_bottomBar);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(12);

    m_bottomInfo = new QLabel(this);
    m_bottomInfo->setProperty(StyleTokens::Properties::kTextRole,
                              StyleTokens::Properties::kCaption);
    m_bottomInfo->setText(QString::fromUtf8("缩放: 100% | 就绪"));
    layout->addWidget(m_bottomInfo);

    m_mouseInfo = new QLabel(this);
    layout->addStretch();
    m_mouseInfo->setProperty(StyleTokens::Properties::kTextRole,
                             StyleTokens::Properties::kMono);
    m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
    layout->addWidget(m_mouseInfo);
}

void PreviewPanel::positionCanvasOverlays() {
    if (!m_canvasHost) return;
    const int availableWidth = std::max(1, m_canvasHost->width() - 32);
    const auto placeCentered = [this, availableWidth](QWidget* widget, int y) {
        if (!widget) return;
        widget->adjustSize();
        const int width = std::min(availableWidth, widget->sizeHint().width());
        widget->resize(width, widget->height());
        widget->move(std::max(16, (m_canvasHost->width() - width) / 2), y);
        widget->raise();
    };
    placeCentered(m_topBar, 12);
    placeCentered(m_maskEditControls, 52);
    if (m_bottomBar) {
        const int width = std::max(1, m_canvasHost->width() - 48);
        m_bottomBar->setGeometry(
            24, std::max(24, m_canvasHost->height() - 50), width, 26);
        m_bottomBar->raise();
    }
    if (m_resultSummary && m_resultSummary->isVisible()) {
        m_resultSummary->adjustSize();
        m_resultSummary->move(
            std::max(24, m_canvasHost->width() -
                             m_resultSummary->width() - 24),
            56);
        m_resultSummary->raise();
    }
}

void PreviewPanel::showImageCanvas() {
    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);
    m_topBar->setVisible(true);
    if (m_infoBtn) m_infoBtn->setVisible(m_showingResult);
    m_bottomBar->setVisible(false);
    positionCanvasOverlays();
}

QString PreviewPanel::sourceContentKey(const QString& filePath) const {
    QFileInfo info(filePath);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString PreviewPanel::legacyResultContentKey() const {
    if (m_showingResult &&
        (m_currentContentKey.startsWith(QStringLiteral("result:")) ||
         m_currentContentKey.startsWith(QStringLiteral("quick:")))) {
        return m_currentContentKey;
    }
    return QStringLiteral("result:legacy");
}

void PreviewPanel::saveCurrentViewState() {
    if (m_currentContentKey.isEmpty() || m_currentImage.isNull() ||
        !m_scrollArea) {
        return;
    }
    ViewStateStore::ViewState state;
    state.zoomMode = m_fitToView ? ViewStateStore::ZoomMode::Fit
                                : ViewStateStore::ZoomMode::Manual;
    state.zoom = m_zoom;
    state.horizontalScroll = m_scrollArea->horizontalScrollBar()->value();
    state.verticalScroll = m_scrollArea->verticalScrollBar()->value();
    state.comparisonMode = static_cast<ViewStateStore::ComparisonMode>(
        std::clamp(m_viewMode, 0, 2));
    state.maskOverlayVisible = m_maskOverlayVisible;
    m_viewStateStore.save(m_currentContentKey, state);
}

bool PreviewPanel::beginContentSwitch(const QString& contentKey) {
    const QString key = contentKey.trimmed();
    const bool sameContent = !key.isEmpty() && key == m_currentContentKey;
    saveCurrentViewState();
    ++m_viewRestoreGeneration;
    m_currentContentKey = key;
    m_pendingViewState = key.isEmpty()
        ? std::optional<ViewStateStore::ViewState>()
        : m_viewStateStore.stateFor(key);
    return sameContent;
}

void PreviewPanel::finishContentSwitch() {
    if (m_currentImage.isNull()) return;
    showImageCanvas();
    updateImageDisplay();
    onFitView();
    const uint64_t generation = m_viewRestoreGeneration;
    const QString key = m_currentContentKey;
    const auto pending = m_pendingViewState;
    m_pendingViewState.reset();
    QTimer::singleShot(0, this, [this, generation, key, pending]() {
        if (generation != m_viewRestoreGeneration ||
            key != m_currentContentKey || m_currentImage.isNull()) {
            return;
        }
        if (pending) {
            restoreViewState(*pending);
        } else if (m_fitToView) {
            applyFitZoom();
        }
        updateZoomDisplay();
    });
}

void PreviewPanel::restoreViewState(const ViewStateStore::ViewState& state) {
    m_viewMode = hasComparison()
        ? std::clamp(static_cast<int>(state.comparisonMode), 0, 2)
        : 1;
    m_beforeAfterMode = m_viewMode == 2;
    updateComparisonButtons();
    emit beforeAfterModeChanged(m_beforeAfterMode);
    setMaskOverlayVisible(state.maskOverlayVisible && hasMaskData());

    if (state.zoomMode == ViewStateStore::ZoomMode::Fit) {
        m_fitToView = true;
        applyFitZoom();
        return;
    }

    m_fitToView = false;
    m_zoom = std::clamp(state.zoom, 0.01, maximumSafeZoom());
    applyZoom();
    if (m_imageContainer->layout()) m_imageContainer->layout()->activate();
    m_scrollArea->horizontalScrollBar()->setValue(state.horizontalScroll);
    m_scrollArea->verticalScrollBar()->setValue(state.verticalScroll);
    updateZoomDisplay();
    emit zoomChanged(m_zoom);
}

void PreviewPanel::updateComparisonButtons() {
    if (m_beforeBtn) {
        const QSignalBlocker blocker(m_beforeBtn);
        m_beforeBtn->setChecked(m_viewMode == 0);
    }
    if (m_afterBtn) {
        const QSignalBlocker blocker(m_afterBtn);
        m_afterBtn->setChecked(m_viewMode == 1);
    }
    if (m_splitBtn) {
        const QSignalBlocker blocker(m_splitBtn);
        m_splitBtn->setChecked(m_viewMode == 2);
    }
    setButtonVariant(m_beforeBtn, m_viewMode == 0
        ? StyleTokens::Properties::kSecondaryButton
        : StyleTokens::Properties::kGhost);
    setButtonVariant(m_afterBtn, m_viewMode == 1
        ? StyleTokens::Properties::kSecondaryButton
        : StyleTokens::Properties::kGhost);
    setButtonVariant(m_splitBtn, m_viewMode == 2
        ? StyleTokens::Properties::kSecondaryButton
        : StyleTokens::Properties::kGhost);
}

void PreviewPanel::setMaskOverlayVisible(bool visible) {
    m_maskOverlayVisible = visible && hasMaskData();
    if (m_maskOverlayBtn) {
        const QSignalBlocker blocker(m_maskOverlayBtn);
        m_maskOverlayBtn->setChecked(m_maskOverlayVisible);
        setButtonVariant(m_maskOverlayBtn, m_maskOverlayVisible
            ? StyleTokens::Properties::kSecondaryButton
            : StyleTokens::Properties::kGhost);
    }
    if (!m_currentImage.isNull()) updateImageDisplay();
}

QPointF PreviewPanel::viewportCenter() const {
    if (!m_scrollArea || !m_scrollArea->viewport()) return {};
    return QPointF(m_scrollArea->viewport()->rect().center());
}

void PreviewPanel::setZoomAnchored(double zoom,
                                   const QPointF& viewportAnchor) {
    if (m_currentImage.isNull()) return;
    const QSize oldLabelSize = m_imageLabel->size();
    const QPointF oldLabelPoint = m_imageLabel->mapFrom(
        m_scrollArea->viewport(), viewportAnchor.toPoint());
    const double normalizedX = oldLabelSize.width() > 0
        ? oldLabelPoint.x() / oldLabelSize.width() : 0.5;
    const double normalizedY = oldLabelSize.height() > 0
        ? oldLabelPoint.y() / oldLabelSize.height() : 0.5;

    m_fitToView = false;
    m_zoom = std::clamp(zoom, 0.01, maximumSafeZoom());
    applyZoom();
    if (m_imageContainer->layout()) m_imageContainer->layout()->activate();

    const QPoint newTopLeft = m_imageLabel->mapTo(
        m_scrollArea->viewport(), QPoint(0, 0));
    const QPointF newPoint(
        newTopLeft.x() + normalizedX * m_imageLabel->width(),
        newTopLeft.y() + normalizedY * m_imageLabel->height());
    const QPointF delta = newPoint - viewportAnchor;
    m_scrollArea->horizontalScrollBar()->setValue(
        m_scrollArea->horizontalScrollBar()->value() + qRound(delta.x()));
    m_scrollArea->verticalScrollBar()->setValue(
        m_scrollArea->verticalScrollBar()->value() + qRound(delta.y()));
    updateZoomDisplay();
    emit zoomChanged(m_zoom);
}

void PreviewPanel::animateZoomTo(double zoom,
                                 const QPointF& viewportAnchor) {
    const double target = std::clamp(zoom, 0.01, maximumSafeZoom());
    if (qFuzzyCompare(m_zoom, target)) return;
    if (m_zoomAnimation) {
        m_zoomAnimation->stop();
        m_zoomAnimation->deleteLater();
    }
    auto* animation = new QVariantAnimation(this);
    m_zoomAnimation = animation;
    animation->setStartValue(m_zoom);
    animation->setEndValue(target);
    animation->setDuration(StyleTokens::Motion::kBaseMs);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this,
            [this, viewportAnchor](const QVariant& value) {
                setZoomAnchored(value.toDouble(), viewportAnchor);
            });
    connect(animation, &QVariantAnimation::finished, this,
            [this, animation]() {
                if (m_zoomAnimation == animation) m_zoomAnimation = nullptr;
                animation->deleteLater();
            });
    animation->start();
}

void PreviewPanel::loadImage(const QString& filePath) {
    if (filePath.isEmpty()) {
        clearImage();
        return;
    }

    QFileInfo info(filePath);
    if (!info.exists()) {
        qWarning() << "文件不存在:" << filePath;
        return;
    }
    beginContentSwitch(sourceContentKey(filePath));
    setPointSelectionActive(false);
    clearSelectedPoint();
    clearMaskOverlay();
    const uint64_t generation = ++m_previewGeneration;
    m_previewPool.clear();
    m_currentFilePath = filePath;
    m_imageFileName = info.fileName();
    m_currentImage = QImage();
    m_showingResult = false;
    resetComparison();
    m_imageLabel->setPixmap(QPixmap());
    m_scrollArea->setVisible(false);
    m_emptyState->setVisible(true);
    m_topBar->setVisible(false);
    m_bottomBar->setVisible(false);
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
            filePath, kPreviewLongSide, preview, &metadata);

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
                safePanel->finishContentSwitch();
                emit safePanel->sourcePreviewReady(
                    filePath, image, metadata.iso, metadata.exposureTime,
                    metadata.aperture, metadata.focalLength);
            }, Qt::QueuedConnection);
    });
}

void PreviewPanel::loadImage(const QImage& image) {
    loadImage(image, QStringLiteral("image:%1")
                         .arg(++m_anonymousContentGeneration));
}

void PreviewPanel::loadImage(const QImage& image,
                             const QString& contentKey) {
    ++m_previewGeneration;
    m_previewPool.clear();
    if (image.isNull()) {
        clearImage();
        return;
    }
    const bool sameContent = beginContentSwitch(contentKey);
    if (!sameContent) {
        setPointSelectionActive(false);
        clearSelectedPoint();
        clearMaskOverlay();
    }
    m_currentImage = image;
    m_showingResult = false;
    resetComparison();

    finishContentSwitch();
}

void PreviewPanel::load16BitImage(const std::vector<uint16_t>& data, int w, int h) {
    load16BitImage(data, w, h, legacyResultContentKey());
}

void PreviewPanel::load16BitImage(const std::vector<uint16_t>& data, int w,
                                  int h, const QString& contentKey) {
    ++m_previewGeneration;
    m_previewPool.clear();
    const PreviewImage8 preview = PreviewToneMapper::mapMono16(data, w, h);
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }
    const bool sameContent = beginContentSwitch(contentKey);
    if (!sameContent) {
        setPointSelectionActive(false);
        clearSelectedPoint();
        clearMaskOverlay();
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

    finishContentSwitch();
}

void PreviewPanel::loadRgb16BitImage(const std::vector<uint16_t>& rgb, int w, int h) {
    loadRgb16BitImage(rgb, w, h, legacyResultContentKey());
}

void PreviewPanel::loadRgb16BitImage(const std::vector<uint16_t>& rgb, int w,
                                     int h, const QString& contentKey) {
    ++m_previewGeneration;
    m_previewPool.clear();
    const PreviewImage8 preview = PreviewToneMapper::mapRgb16(rgb, w, h);
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }
    const bool sameContent = beginContentSwitch(contentKey);
    if (!sameContent) {
        setPointSelectionActive(false);
        clearSelectedPoint();
        clearMaskOverlay();
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

    finishContentSwitch();
}

void PreviewPanel::loadRgb16BitComparison(const QImage& before,
                                           const std::vector<uint16_t>& afterRgb,
                                           int w, int h,
                                           uint16_t blackPoint,
                                           uint16_t whitePoint) {
    loadRgb16BitComparison(before, afterRgb, w, h, blackPoint, whitePoint,
                           legacyResultContentKey());
}

void PreviewPanel::loadRgb16BitComparison(
    const QImage& before, const std::vector<uint16_t>& afterRgb,
    int w, int h, uint16_t blackPoint, uint16_t whitePoint,
    const QString& contentKey) {
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
    const bool sameContent = beginContentSwitch(contentKey);
    if (!sameContent) {
        setPointSelectionActive(false);
        clearSelectedPoint();
        clearMaskOverlay();
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

    finishContentSwitch();
}

void PreviewPanel::clearImage() {
    saveCurrentViewState();
    ++m_viewRestoreGeneration;
    m_pendingViewState.reset();
    m_currentContentKey.clear();
    setPointSelectionActive(false);
    m_selectedPointX = -1.0;
    m_selectedPointY = -1.0;
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
    m_fitToView = true;
    m_showingResult = false;
    m_viewMode = 1;
    m_beforeAfterMode = false;
    if (m_compareControl) m_compareControl->setVisible(false);
    if (m_afterBtn) m_afterBtn->setChecked(true);
    emit comparisonAvailabilityChanged(false);

    m_imageLabel->setPixmap(QPixmap());
    m_emptyState->setVisible(true);
    m_emptyText->setText(QString::fromUtf8("今晚的星空还在等你"));
    m_emptyFormat->setText(QString::fromUtf8("拖入一组 RAW，或选择文件"));
    m_emptyImportBtn->setVisible(true);
    m_scrollArea->setVisible(false);
    m_topBar->setVisible(false);
    m_maskEditControls->setVisible(false);
    m_bottomBar->setVisible(false);
    m_bottomInfo->setText(QString::fromUtf8("缩放: 100% | 就绪"));
    m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
}

void PreviewPanel::setZoom(double zoom) {
    setZoomAnchored(zoom, viewportCenter());
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

void PreviewPanel::setPointSelectionActive(bool active) {
    m_pointSelectionActive = active && !m_currentImage.isNull();
    if (!m_imageLabel) return;
    if (m_pointSelectionActive && hasComparison()) {
        m_viewMode = 0;
        m_beforeAfterMode = false;
        if (m_beforeBtn) m_beforeBtn->setChecked(true);
        emit beforeAfterModeChanged(false);
        if (m_fitToView) {
            applyFitZoom();
        } else {
            applyZoom();
        }
    }
    if (m_infoBtn) {
        const QSignalBlocker blocker(m_infoBtn);
        m_infoBtn->setChecked(m_pointSelectionActive);
        setButtonVariant(m_infoBtn, m_pointSelectionActive
            ? StyleTokens::Properties::kSecondaryButton
            : StyleTokens::Properties::kIcon);
    }
    updateImageCursor();
    if (m_pointSelectionActive && m_mouseInfo) {
        m_mouseInfo->setText(QString::fromUtf8("点击应为灰色的天空区域"));
    }
}

void PreviewPanel::setSelectedPoint(double normalizedX,
                                    double normalizedY) {
    if (!std::isfinite(normalizedX) || !std::isfinite(normalizedY)) return;
    m_selectedPointX = std::clamp(normalizedX, 0.0, 1.0);
    m_selectedPointY = std::clamp(normalizedY, 0.0, 1.0);
    applyZoom();
}

void PreviewPanel::clearSelectedPoint() {
    if (m_selectedPointX < 0.0 && m_selectedPointY < 0.0) return;
    m_selectedPointX = -1.0;
    m_selectedPointY = -1.0;
    if (!m_currentImage.isNull()) applyZoom();
}

void PreviewPanel::onFitView() {
    if (m_currentImage.isNull()) return;

    m_fitToView = true;
    applyFitZoom();
}

void PreviewPanel::applyFitZoom() {
    if (m_currentImage.isNull()) return;

    const double fitZoom = fitZoomForViewport();
    if (fitZoom <= 0.0) return;

    m_zoom = std::clamp(fitZoom, 0.01, maximumSafeZoom());

    applyZoom();
    updateZoomDisplay();
    emit zoomChanged(m_zoom);
}

double PreviewPanel::fitZoomForViewport() const {
    if (!m_scrollArea || !m_scrollArea->viewport()) return 0.0;
    const QImage& image = displayedImage();
    if (image.isNull()) return 0.0;

    int viewW = m_scrollArea->viewport()->width() - kPreviewMargin;
    const int viewH = m_scrollArea->viewport()->height() - kPreviewMargin;
    if (m_viewMode == 2 && hasComparison()) {
        viewW = (viewW - kSplitGutter) / 2;
    }
    if (viewW <= 0 || viewH <= 0) return 0.0;

    const double scaleW = static_cast<double>(viewW) / image.width();
    const double scaleH = static_cast<double>(viewH) / image.height();
    return std::min(scaleW, scaleH);
}

void PreviewPanel::onZoom100() {
    setZoom(1.0);
}

void PreviewPanel::onZoomIn() {
    animateZoomTo(m_zoom * 1.25, viewportCenter());
}

void PreviewPanel::onZoomOut() {
    animateZoomTo(m_zoom * 0.8, viewportCenter());
}

void PreviewPanel::onToggleBeforeAfter() {
    if (!hasComparison()) return;
    m_beforeAfterMode = true;
    m_viewMode = 2;
    if (m_splitBtn) m_splitBtn->setChecked(true);
    applyZoom();
    if (m_fitToView) {
        applyFitZoom();
    }
}

void PreviewPanel::onViewModeChanged(int id) {
    if (!hasComparison()) return;
    m_viewMode = std::clamp(id, 0, 2);
    m_beforeAfterMode = m_viewMode == 2;
    updateComparisonButtons();
    emit beforeAfterModeChanged(m_beforeAfterMode);
    applyZoom();
    if (m_fitToView) {
        applyFitZoom();
    }
}

void PreviewPanel::onToggleInfo() {
    m_showInfo = m_infoBtn->isChecked();
    m_bottomBar->setVisible(m_showInfo);
}

void PreviewPanel::onToggleMaskOverlay() {
    setMaskOverlayVisible(m_maskOverlayBtn && m_maskOverlayBtn->isChecked());
}

void PreviewPanel::onTogglePointSelection() {
    setPointSelectionActive(m_infoBtn && m_infoBtn->isChecked());
}

void PreviewPanel::updateImageCursor() {
    if (!m_imageLabel) return;
    if (m_pointSelectionActive || m_maskEditingActive) {
        m_imageLabel->setCursor(Qt::CrossCursor);
    } else if (m_panning) {
        m_imageLabel->setCursor(Qt::ClosedHandCursor);
    } else {
        m_imageLabel->setCursor(Qt::OpenHandCursor);
    }
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
        const int afterX = w + kSplitGutter;
        pixmap = QPixmap(w * 2 + kSplitGutter, h);
        pixmap.fill(QColor("#090D0F"));
        QPainter comparisonPainter(&pixmap);
        comparisonPainter.drawPixmap(0, 0, before);
        comparisonPainter.drawPixmap(afterX, 0, after);
        comparisonPainter.setPen(QPen(QColor("#344548"), 1));
        comparisonPainter.drawLine(w + kSplitGutter / 2, 0,
                                   w + kSplitGutter / 2, h);
        if (w > 180 && h > 48) {
            comparisonPainter.setPen(Qt::NoPen);
            comparisonPainter.setBrush(QColor(12, 14, 16, 190));
            comparisonPainter.drawRoundedRect(QRect(8, 8, 52, 22), 4, 4);
            comparisonPainter.drawRoundedRect(
                QRect(afterX + w - 60, 8, 52, 22), 4, 4);
            comparisonPainter.setPen(QColor("#F5FAF8"));
            comparisonPainter.drawText(QRect(8, 8, 52, 22), Qt::AlignCenter,
                                       QString::fromUtf8("处理前"));
            comparisonPainter.drawText(
                QRect(afterX + w - 60, 8, 52, 22), Qt::AlignCenter,
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
        const QImage scaledMask = m_maskOverlay.scaled(
            w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        painter.drawImage(0, 0, scaledMask);
        if (m_viewMode == 2 && hasComparison()) {
            painter.drawImage(w + kSplitGutter, 0, scaledMask);
        }
        painter.end();
    }
    if (m_maskOverlayVisible && !m_groundHintOverlay.isNull()) {
        QPainter painter(&pixmap);
        const QImage scaledHints = m_groundHintOverlay.scaled(
            w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        painter.drawImage(0, 0, scaledHints);
        if (m_viewMode == 2 && hasComparison()) {
            painter.drawImage(w + kSplitGutter, 0, scaledHints);
        }
    }

    if (m_selectedPointX >= 0.0 && m_selectedPointY >= 0.0) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const auto drawMarker = [&painter](const QPointF& center) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(9, 13, 15, 210), 4.0));
            painter.drawEllipse(center, 8.0, 8.0);
            painter.setPen(QPen(QColor("#4ED7AE"), 2.0));
            painter.drawEllipse(center, 8.0, 8.0);
            painter.drawLine(center + QPointF(-12.0, 0.0),
                             center + QPointF(-5.0, 0.0));
            painter.drawLine(center + QPointF(5.0, 0.0),
                             center + QPointF(12.0, 0.0));
            painter.drawLine(center + QPointF(0.0, -12.0),
                             center + QPointF(0.0, -5.0));
            painter.drawLine(center + QPointF(0.0, 5.0),
                             center + QPointF(0.0, 12.0));
        };
        drawMarker(QPointF(m_selectedPointX * w, m_selectedPointY * h));
        if (m_viewMode == 2 && hasComparison()) {
            drawMarker(QPointF(w + kSplitGutter + m_selectedPointX * w,
                               m_selectedPointY * h));
        }
    }

    m_imageLabel->setPixmap(pixmap);
    m_imageLabel->setFixedSize(pixmap.size());
    m_imageContainer->setMinimumSize(
        pixmap.width() + kPreviewMargin,
        pixmap.height() + kPreviewMargin);
}

bool PreviewPanel::imageSampleAt(const QPoint& labelPosition,
                                 const QImage*& image,
                                 int& x, int& y) const {
    const QImage& displayed = displayedImage();
    if (displayed.isNull()) return false;

    const int paneWidth = std::max(1, qRound(displayed.width() * m_zoom));
    const int paneHeight = std::max(1, qRound(displayed.height() * m_zoom));
    if (labelPosition.y() < 0 || labelPosition.y() >= paneHeight) return false;

    int localX = labelPosition.x();
    image = &displayed;
    if (m_viewMode == 2 && hasComparison()) {
        if (localX >= 0 && localX < paneWidth) {
            image = &m_beforeImage;
        } else {
            localX -= paneWidth + kSplitGutter;
            if (localX < 0 || localX >= paneWidth) return false;
            image = &m_afterImage;
        }
    } else if (localX < 0 || localX >= paneWidth) {
        return false;
    }

    x = static_cast<int>(
        static_cast<int64_t>(localX) * image->width() / paneWidth);
    y = static_cast<int>(
        static_cast<int64_t>(labelPosition.y()) * image->height() / paneHeight);
    return x >= 0 && x < image->width() && y >= 0 && y < image->height();
}

double PreviewPanel::maximumSafeZoom() const {
    const QImage& image = displayedImage();
    if (image.isNull()) return 1.0;

    // A scaled QPixmap may require several temporary copies. Bound it to about
    // 32 megapixels so zoom cannot allocate multiple gigabytes on a large frame.
    constexpr double maxRenderedPixels = 32.0 * 1024.0 * 1024.0;
    const double frameCount = (m_viewMode == 2 && hasComparison()) ? 2.0 : 1.0;
    const double pixels = static_cast<double>(image.width()) * image.height()
        * frameCount;
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
    if (m_zoom100Btn) m_zoom100Btn->setEnabled(!m_currentImage.isNull());
    if (m_zoomInBtn) {
        const bool atLimit = m_zoom >= maximumSafeZoom() - 0.0001;
        m_zoomInBtn->setEnabled(!m_currentImage.isNull() && !atLimit);
        m_zoomInBtn->setToolTip(atLimit
            ? QString::fromUtf8("已达安全缩放上限")
            : QString::fromUtf8("放大 (+)"));
    }
}

void PreviewPanel::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
        const double delta = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const QPointF anchor = m_scrollArea
            ? m_scrollArea->viewport()->mapFrom(this,
                  event->position().toPoint())
            : viewportCenter();
        animateZoomTo(m_zoom * delta, anchor);
        event->accept();
    } else {
        // 普通滚轮传递给滚动区域
        QWidget::wheelEvent(event);
    }
}

bool PreviewPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_canvasHost) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            QTimer::singleShot(0, this,
                               [this]() { positionCanvasOverlays(); });
        }
        return QWidget::eventFilter(watched, event);
    }

    if (m_scrollArea && watched == m_scrollArea->viewport()) {
        if (event->type() == QEvent::Resize && m_fitToView &&
            !m_currentImage.isNull() && !m_fitUpdatePending) {
            m_fitUpdatePending = true;
            QTimer::singleShot(0, this, [this]() {
                m_fitUpdatePending = false;
                if (m_fitToView && !m_currentImage.isNull()) {
                    applyFitZoom();
                }
            });
        }
        return QWidget::eventFilter(watched, event);
    }

    if (watched != m_imageLabel || m_currentImage.isNull()) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::Wheel) {
        auto* wheel = static_cast<QWheelEvent*>(event);
        if (wheel->modifiers() &
            (Qt::ControlModifier | Qt::MetaModifier)) {
            const QPointF anchor = m_scrollArea->viewport()->mapFrom(
                m_imageLabel, wheel->position().toPoint());
            const double factor = wheel->angleDelta().y() > 0
                ? 1.15 : 1.0 / 1.15;
            animateZoomTo(m_zoom * factor, anchor);
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            !m_maskEditingActive && !m_pointSelectionActive) {
            if (m_fitToView) onZoom100();
            else onFitView();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_pointSelectionActive &&
            mouse->button() == Qt::RightButton) {
            setPointSelectionActive(false);
            m_mouseInfo->setText(QString::fromUtf8("手动灰点采样已取消"));
            return true;
        }
        if (mouse->button() == Qt::LeftButton) {
            if (m_maskEditingActive && !m_maskRefinementActive) {
                QPoint maskPoint;
                if (maskPositionAt(mouse->position().toPoint(), maskPoint)) {
                    m_maskPainting = true;
                    m_activeGroundHintStroke = {};
                    m_activeGroundHintStroke.radius = std::max(
                        1, m_maskBrushSize->value() / 2);
                    m_activeGroundHintStroke.points.push_back(maskPoint);
                    paintGroundHintSegment(
                        maskPoint, maskPoint,
                        m_activeGroundHintStroke.radius);
                    scheduleMaskDisplayRefresh();
                }
                return true;
            }
            if (m_pointSelectionActive) {
                const QImage* sampled = nullptr;
                int x = -1;
                int y = -1;
                if (imageSampleAt(mouse->position().toPoint(), sampled, x, y)) {
                    const double normalizedX = std::clamp(
                        (x + 0.5) / std::max(1, sampled->width()), 0.0,
                        std::nextafter(1.0, 0.0));
                    const double normalizedY = std::clamp(
                        (y + 0.5) / std::max(1, sampled->height()), 0.0,
                        std::nextafter(1.0, 0.0));
                    setPointSelectionActive(false);
                    emit imagePointSelected(normalizedX, normalizedY);
                }
                return true;
            }
            m_panning = true;
            m_lastPanPos = mouse->globalPosition().toPoint();
            updateImageCursor();
            return true;
        }
    }

    if (event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (m_maskPainting && m_maskEditingActive) {
            QPoint maskPoint;
            if (maskPositionAt(mouse->position().toPoint(), maskPoint) &&
                (m_activeGroundHintStroke.points.empty() ||
                 m_activeGroundHintStroke.points.back() != maskPoint)) {
                const QPoint previous = m_activeGroundHintStroke.points.back();
                m_activeGroundHintStroke.points.push_back(maskPoint);
                paintGroundHintSegment(
                    previous, maskPoint, m_activeGroundHintStroke.radius);
                scheduleMaskDisplayRefresh();
            }
            return true;
        }
        if (m_panning) {
            const QPoint global = mouse->globalPosition().toPoint();
            const QPoint delta = global - m_lastPanPos;
            m_lastPanPos = global;
            m_scrollArea->horizontalScrollBar()->setValue(
                m_scrollArea->horizontalScrollBar()->value() - delta.x());
            m_scrollArea->verticalScrollBar()->setValue(
                m_scrollArea->verticalScrollBar()->value() - delta.y());
        }

        const QImage* sampled = nullptr;
        int x = -1;
        int y = -1;
        if (imageSampleAt(mouse->position().toPoint(), sampled, x, y)) {
            const QRgb pixel = sampled->pixel(x, y);
            emit mousePixelInfo(x, y, qRed(pixel), qGreen(pixel), qBlue(pixel));
            m_mouseInfo->setText(
                QString::fromUtf8("鼠标: %1,%2 | RGB: (%3, %4, %5)")
                    .arg(x).arg(y)
                    .arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel)));
        } else {
            m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
        }
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (m_maskPainting) {
                m_maskPainting = false;
                if (!m_activeGroundHintStroke.points.empty()) {
                    m_groundHintStrokes.push_back(m_activeGroundHintStroke);
                    m_activeGroundHintStroke = {};
                    startMaskRefinement();
                }
                return true;
            }
            m_panning = false;
            updateImageCursor();
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
    updateComparisonButtons();
    emit beforeAfterModeChanged(enabled);
    applyZoom();
    if (m_fitToView) {
        applyFitZoom();
    }
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
    updateComparisonButtons();
    if (m_compareControl) m_compareControl->setVisible(true);
    emit comparisonAvailabilityChanged(true);
    emit beforeAfterModeChanged(false);
    showImageCanvas();
    // A direct setComparisonImages() call may occur before Qt has laid out the
    // newly visible scroll area. Render once at the current zoom immediately;
    // the queued fit pass then refines it when viewport geometry is available.
    applyZoom();
    if (m_fitToView) {
        QTimer::singleShot(0, this, [this]() {
            if (m_fitToView && hasComparison()) applyFitZoom();
        });
    }
    updateZoomDisplay();
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

void PreviewPanel::showResultSummary(const QString& summary) {
    if (!m_resultSummary || !m_resultSummaryLabel || summary.isEmpty()) return;
    m_resultSummaryLabel->setText(summary);
    m_resultSummary->show();
    positionCanvasOverlays();
    if (m_resultSummaryTimer) m_resultSummaryTimer->start();
}

void PreviewPanel::clearComparison() {
    resetComparison();
}

void PreviewPanel::resetComparison() {
    m_beforeImage = QImage();
    m_afterImage = QImage();
    m_viewMode = 1;
    m_beforeAfterMode = false;
    updateComparisonButtons();
    if (m_compareControl) m_compareControl->setVisible(false);
    emit comparisonAvailabilityChanged(false);
    emit beforeAfterModeChanged(false);
}

QImage PreviewPanel::currentImage() const {
    return m_currentImage;
}

void PreviewPanel::setMaskOverlay(const std::vector<uint8_t>& mask, int w, int h) {
    if (w <= 0 || h <= 0 ||
        mask.size() != static_cast<size_t>(w) * h) return;
    ++m_maskRefinementGeneration;
    m_initialMask.resize(mask.size());
    std::transform(mask.begin(), mask.end(), m_initialMask.begin(),
                   [](uint8_t value) { return value >= 128 ? 255 : 0; });
    m_editedMask = m_initialMask;
    m_editedMaskWidth = w;
    m_editedMaskHeight = h;
    m_groundHints.assign(mask.size(), 0);
    m_groundHintStrokes.clear();
    m_groundHintOverlay = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_groundHintOverlay.fill(Qt::transparent);
    m_maskOverlayVisible = true;
    m_maskEditingActive = true;
    m_maskHasUserEdits = false;
    m_maskRefinementActive = false;
    if (m_maskBrushBtn) m_maskBrushBtn->setChecked(true);
    if (m_maskEditControls) m_maskEditControls->setVisible(true);
    if (m_maskOverlayBtn) m_maskOverlayBtn->setVisible(true);
    setMaskOverlayVisible(true);
    positionCanvasOverlays();
    rebuildMaskOverlay();
    updateImageDisplay();
    emit editedMaskChanged();
}

void PreviewPanel::clearMaskOverlay() {
    ++m_maskRefinementGeneration;
    m_maskOverlay = QImage();
    m_groundHintOverlay = QImage();
    m_maskOverlayVisible = false;
    m_maskEditingActive = false;
    m_maskHasUserEdits = false;
    m_maskPainting = false;
    m_maskRefinementActive = false;
    m_initialMask.clear();
    m_editedMask.clear();
    m_groundHints.clear();
    m_groundHintStrokes.clear();
    m_editedMaskWidth = 0;
    m_editedMaskHeight = 0;
    if (m_maskEditControls) m_maskEditControls->setVisible(false);
    if (m_maskOverlayBtn) m_maskOverlayBtn->setVisible(false);
    if (m_maskOverlayBtn) m_maskOverlayBtn->setChecked(false);
    positionCanvasOverlays();
    updateImageDisplay();
}

bool PreviewPanel::hasEditedMask() const {
    return m_maskHasUserEdits && hasMaskData();
}

bool PreviewPanel::hasMaskData() const {
    return !m_editedMask.empty() &&
        m_editedMaskWidth > 0 && m_editedMaskHeight > 0;
}

bool PreviewPanel::maskPositionAt(const QPoint& labelPosition,
                                  QPoint& maskPoint) const {
    const QImage* sampled = nullptr;
    int imageX = -1;
    int imageY = -1;
    if (!hasMaskData() ||
        !imageSampleAt(labelPosition, sampled, imageX, imageY) ||
        !sampled || sampled->isNull()) {
        return false;
    }
    maskPoint.setX(std::clamp(
        static_cast<int>(static_cast<int64_t>(imageX) * m_editedMaskWidth /
                         sampled->width()), 0, m_editedMaskWidth - 1));
    maskPoint.setY(std::clamp(
        static_cast<int>(static_cast<int64_t>(imageY) * m_editedMaskHeight /
                         sampled->height()), 0, m_editedMaskHeight - 1));
    return true;
}

void PreviewPanel::rebuildMaskOverlay() {
    if (!hasMaskData()) return;
    m_maskOverlay = QImage(m_editedMaskWidth, m_editedMaskHeight,
                           QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < m_editedMaskHeight; ++y) {
        auto* row = reinterpret_cast<QRgb*>(m_maskOverlay.scanLine(y));
        for (int x = 0; x < m_editedMaskWidth; ++x) {
            const float sky = m_editedMask[
                static_cast<size_t>(y) * m_editedMaskWidth + x] / 255.0f;
            const int green = static_cast<int>(255 + sky * (136 - 255));
            const int blue = static_cast<int>(136 + sky * (255 - 136));
            row[x] = qPremultiply(qRgba(68, green, blue, 77));
        }
    }
}

void PreviewPanel::paintGroundHintSegment(const QPoint& from,
                                           const QPoint& to, int radius) {
    if (m_groundHints.empty() || m_groundHintOverlay.isNull()) return;
    const int dx = to.x() - from.x();
    const int dy = to.y() - from.y();
    const int steps = std::max(1, static_cast<int>(
        std::ceil(std::hypot(dx, dy) / std::max(1.0, radius * 0.35))));
    for (int step = 0; step <= steps; ++step) {
        const int cx = qRound(from.x() + dx * (step / static_cast<double>(steps)));
        const int cy = qRound(from.y() + dy * (step / static_cast<double>(steps)));
        const int minX = std::max(0, cx - radius);
        const int maxX = std::min(m_editedMaskWidth - 1, cx + radius);
        const int minY = std::max(0, cy - radius);
        const int maxY = std::min(m_editedMaskHeight - 1, cy + radius);
        for (int y = minY; y <= maxY; ++y) {
            auto* overlay = reinterpret_cast<QRgb*>(
                m_groundHintOverlay.scanLine(y));
            for (int x = minX; x <= maxX; ++x) {
                const int px = x - cx;
                const int py = y - cy;
                if (px * px + py * py > radius * radius) continue;
                m_groundHints[static_cast<size_t>(y) * m_editedMaskWidth + x] = 255;
                overlay[x] = qPremultiply(qRgba(242, 182, 90, 185));
            }
        }
    }
}

void PreviewPanel::rebuildGroundHints() {
    m_groundHints.assign(
        static_cast<size_t>(m_editedMaskWidth) * m_editedMaskHeight, 0);
    m_groundHintOverlay = QImage(
        m_editedMaskWidth, m_editedMaskHeight,
        QImage::Format_ARGB32_Premultiplied);
    m_groundHintOverlay.fill(Qt::transparent);
    for (const GroundHintStroke& stroke : m_groundHintStrokes) {
        if (stroke.points.empty()) continue;
        paintGroundHintSegment(stroke.points.front(), stroke.points.front(),
                               stroke.radius);
        for (int index = 1; index < stroke.points.size(); ++index) {
            paintGroundHintSegment(stroke.points[index - 1],
                                   stroke.points[index], stroke.radius);
        }
    }
}

void PreviewPanel::setMaskRefinementBusy(bool busy) {
    m_maskRefinementActive = busy;
    for (QWidget* control : {static_cast<QWidget*>(m_maskBrushBtn),
                             static_cast<QWidget*>(m_maskBrushSize),
                             static_cast<QWidget*>(m_maskUndoBtn),
                             static_cast<QWidget*>(m_maskResetBtn),
                             static_cast<QWidget*>(m_maskDoneBtn)}) {
        if (control) control->setEnabled(!busy);
    }
    if (m_mouseInfo) {
        m_mouseInfo->setText(busy
            ? QString::fromUtf8("正在根据笔迹贴合地景边缘...")
            : QString::fromUtf8("橙色=地景提示 | 蓝色=天空 | 绿色=地景"));
    }
}

void PreviewPanel::startMaskRefinement() {
    if (!hasMaskData() || m_groundHints.empty() ||
        m_maskRefinementActive) return;
    const uint64_t generation = ++m_maskRefinementGeneration;
    const QImage preview = m_currentImage;
    const std::vector<uint8_t> initial = m_initialMask;
    const std::vector<uint8_t> hints = m_groundHints;
    const int width = m_editedMaskWidth;
    const int height = m_editedMaskHeight;
    setMaskRefinementBusy(true);
    emit maskRefinementStarted();
    QPointer<PreviewPanel> safePanel(this);
    m_previewPool.start([safePanel, generation, preview, initial, hints,
                         width, height]() {
        std::vector<uint8_t> refined;
        const bool success = SkyGroundMask::refineWithGroundHints(
            preview, initial, hints, width, height, refined);
        if (!safePanel) return;
        QMetaObject::invokeMethod(safePanel.data(),
            [safePanel, generation, success, refined = std::move(refined)]() mutable {
                if (!safePanel || generation !=
                        safePanel->m_maskRefinementGeneration) return;
                if (success && !refined.empty()) {
                    safePanel->m_editedMask = std::move(refined);
                    safePanel->m_maskHasUserEdits = true;
                    safePanel->rebuildMaskOverlay();
                    emit safePanel->editedMaskChanged();
                }
                safePanel->setMaskRefinementBusy(false);
                safePanel->updateImageDisplay();
                emit safePanel->maskRefinementFinished(success);
            }, Qt::QueuedConnection);
    });
}

void PreviewPanel::undoGroundHint() {
    if (m_maskRefinementActive || m_groundHintStrokes.empty()) return;
    m_groundHintStrokes.removeLast();
    rebuildGroundHints();
    if (m_groundHintStrokes.empty()) {
        m_editedMask = m_initialMask;
        m_maskHasUserEdits = false;
        rebuildMaskOverlay();
        updateImageDisplay();
        emit editedMaskChanged();
    } else {
        startMaskRefinement();
    }
}

void PreviewPanel::resetGroundHints() {
    if (m_maskRefinementActive || m_initialMask.empty()) return;
    ++m_maskRefinementGeneration;
    m_groundHintStrokes.clear();
    rebuildGroundHints();
    m_editedMask = m_initialMask;
    m_maskHasUserEdits = false;
    rebuildMaskOverlay();
    updateImageDisplay();
    emit editedMaskChanged();
}

void PreviewPanel::scheduleMaskDisplayRefresh() {
    if (m_maskDisplayRefreshPending) return;
    m_maskDisplayRefreshPending = true;
    QTimer::singleShot(32, this, [this]() {
        m_maskDisplayRefreshPending = false;
        if (m_maskOverlayVisible) updateImageDisplay();
    });
}

QString PreviewPanel::formatExposureTime(double seconds) const {
    if (seconds >= 1.0) {
        return QString("%1s").arg(seconds, 0, 'f', 1);
    } else if (seconds > 0) {
        return QString("1/%1s").arg(qRound(1.0 / seconds));
    }
    return QString();
}
