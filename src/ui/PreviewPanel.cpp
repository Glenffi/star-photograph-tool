#include "PreviewPanel.h"
#include "../core/PreviewToneMapper.h"
#include "../core/RawImageLoader.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
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
    setupUI();
}

void PreviewPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    setupTopBar();
    layout->addWidget(m_topBar);

    // 图像区域（空状态 + 图像视图）
    auto* contentWidget = new QWidget(this);
    contentWidget->setStyleSheet("background-color: #0D1117;");
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
    m_topBar->setFixedHeight(32);
    m_topBar->setStyleSheet("background-color: #161B22; border-bottom: 1px solid #30363D;");

    auto* layout = new QHBoxLayout(m_topBar);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(4);

    auto createToolBtn = [this](const QString& text, const QString& tooltip) -> QPushButton* {
        auto* btn = new QPushButton(text, this);
        btn->setToolTip(tooltip);
        btn->setFixedHeight(24);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: transparent;"
            "  color: #8B949E;"
            "  border: 1px solid #30363D;"
            "  border-radius: 4px;"
            "  padding: 2px 8px;"
            "  font-size: 11px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #21262D;"
            "  color: #E6EDF3;"
            "  border: 1px solid #484F58;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #30363D;"
            "}"
            "QPushButton:disabled {"
            "  color: #484F58;"
            "  border: 1px solid #21262D;"
            "}"
        );
        return btn;
    };

    m_fitBtn = createToolBtn(QString::fromUtf8("适应"), QString::fromUtf8("适应视图"));
    connect(m_fitBtn, &QPushButton::clicked, this, &PreviewPanel::onFitView);
    layout->addWidget(m_fitBtn);

    m_zoom100Btn = createToolBtn("1:1", QString::fromUtf8("100% 缩放"));
    connect(m_zoom100Btn, &QPushButton::clicked, this, &PreviewPanel::onZoom100);
    layout->addWidget(m_zoom100Btn);

    m_zoomInBtn = createToolBtn(QString::fromUtf8("+"), QString::fromUtf8("放大"));
    connect(m_zoomInBtn, &QPushButton::clicked, this, &PreviewPanel::onZoomIn);
    layout->addWidget(m_zoomInBtn);

    m_zoomOutBtn = createToolBtn(QString::fromUtf8("−"), QString::fromUtf8("缩小"));
    connect(m_zoomOutBtn, &QPushButton::clicked, this, &PreviewPanel::onZoomOut);
    layout->addWidget(m_zoomOutBtn);

    layout->addSpacing(8);

    m_beforeAfterBtn = createToolBtn(QString::fromUtf8("对比"), QString::fromUtf8("Before/After 对比"));
    m_beforeAfterBtn->setCheckable(true);
    // The pipeline does not yet retain a matching pre-processing frame.
    m_beforeAfterBtn->setVisible(false);
    connect(m_beforeAfterBtn, &QPushButton::clicked, this, &PreviewPanel::onToggleBeforeAfter);
    layout->addWidget(m_beforeAfterBtn);

    m_infoBtn = createToolBtn(QString::fromUtf8("信息"), QString::fromUtf8("显示/隐藏信息"));
    m_infoBtn->setCheckable(true);
    m_infoBtn->setChecked(true);
    connect(m_infoBtn, &QPushButton::clicked, this, &PreviewPanel::onToggleInfo);
    layout->addWidget(m_infoBtn);

    layout->addStretch();
}

void PreviewPanel::setupEmptyState() {
    m_emptyState = new QWidget(this);
    m_emptyState->setStyleSheet("background-color: transparent;");
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);

    m_emptyIcon = new QLabel(QString::fromUtf8("🖼️"), m_emptyState);
    m_emptyIcon->setStyleSheet("font-size: 48px; color: #484F58; background-color: transparent;");
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIcon);

    m_emptyText = new QLabel(QString::fromUtf8("拖入 RAW 文件或点击导入"), m_emptyState);
    m_emptyText->setStyleSheet("font-size: 14px; color: #8B949E; background-color: transparent;");
    m_emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyText);

    m_emptyFormat = new QLabel(
        QString::fromUtf8("支持 NEF, CR2, ARW, DNG, RAW, ORF, RAF, PEF, CR3"),
        m_emptyState
    );
    m_emptyFormat->setStyleSheet("font-size: 11px; color: #6E7681; background-color: transparent;");
    m_emptyFormat->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyFormat);

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
    connect(m_emptyImportBtn, &QPushButton::clicked, this, &PreviewPanel::importRequested);
    emptyLayout->addWidget(m_emptyImportBtn, 0, Qt::AlignCenter);

}

void PreviewPanel::setupImageView() {
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background-color: #0D1117; border: none; }"
        "QScrollBar:vertical { background-color: #0D1117; width: 12px; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background-color: #30363D; border-radius: 6px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background-color: #484F58; }"
        "QScrollBar:horizontal { background-color: #0D1117; height: 12px; border-radius: 6px; }"
        "QScrollBar::handle:horizontal { background-color: #30363D; border-radius: 6px; min-width: 20px; }"
        "QScrollBar::handle:horizontal:hover { background-color: #484F58; }"
    );

    m_imageContainer = new QWidget();
    m_imageContainer->setStyleSheet("background-color: #0D1117;");
    auto* containerLayout = new QVBoxLayout(m_imageContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setAlignment(Qt::AlignCenter);

    m_imageLabel = new QLabel(m_imageContainer);
    m_imageLabel->setStyleSheet("background-color: transparent;");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    containerLayout->addWidget(m_imageLabel, 0, Qt::AlignCenter);

    m_scrollArea->setWidget(m_imageContainer);
}

void PreviewPanel::setupBottomBar() {
    m_bottomBar = new QWidget(this);
    m_bottomBar->setFixedHeight(56);
    m_bottomBar->setStyleSheet("background-color: #161B22; border-top: 1px solid #30363D;");

    auto* layout = new QVBoxLayout(m_bottomBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);

    m_bottomInfo = new QLabel(this);
    m_bottomInfo->setStyleSheet("font-size: 12px; color: #8B949E; background-color: transparent;");
    m_bottomInfo->setText(QString::fromUtf8("缩放: 100% | 就绪"));
    layout->addWidget(m_bottomInfo);

    m_mouseInfo = new QLabel(this);
    m_mouseInfo->setStyleSheet("font-size: 11px; color: #6E7681; background-color: transparent;");
    m_mouseInfo->setText(QString::fromUtf8("鼠标: — | RGB: —"));
    layout->addWidget(m_mouseInfo);
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

    QImage image = convertRawToDisplayable(filePath);
    if (image.isNull()) {
        qWarning() << "无法加载图像:" << filePath;
        return;
    }

    m_currentImage = image;
    m_beforeImage = image;
    m_afterImage = image;
    m_currentFilePath = filePath;
    m_imageFileName = info.fileName();

    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);

    updateImageDisplay();
    onFitView();
    updateZoomDisplay();
}

void PreviewPanel::loadImage(const QImage& image) {
    if (image.isNull()) {
        clearImage();
        return;
    }
    m_currentImage = image;
    m_beforeImage = image;
    m_afterImage = image;

    m_emptyState->setVisible(false);
    m_scrollArea->setVisible(true);

    updateImageDisplay();
    onFitView();
    updateZoomDisplay();
}

void PreviewPanel::load16BitImage(const std::vector<uint16_t>& data, int w, int h) {
    const PreviewImage8 preview = PreviewToneMapper::mapMono16(data, w, h);
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }

    const QImage borrowed(preview.rgb.data(), preview.width, preview.height,
                          preview.width * 3, QImage::Format_RGB888);
    const QImage image = borrowed.copy();

    m_currentImage = image;
    m_beforeImage = image;
    m_afterImage = image;
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
    const PreviewImage8 preview = PreviewToneMapper::mapRgb16(rgb, w, h);
    if (preview.rgb.empty()) {
        clearImage();
        return;
    }

    const QImage borrowed(preview.rgb.data(), preview.width, preview.height,
                          preview.width * 3, QImage::Format_RGB888);
    const QImage image = borrowed.copy();

    m_currentImage = image;
    m_beforeImage = image;
    m_afterImage = image;
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

void PreviewPanel::clearImage() {
    m_currentImage = QImage();
    m_beforeImage = QImage();
    m_afterImage = QImage();
    m_currentFilePath.clear();
    m_imageFileName.clear();
    m_imageIso = 0;
    m_imageExposure = 0.0;
    m_imageFocalLength = 0;
    m_zoom = 1.0;

    m_imageLabel->setPixmap(QPixmap());
    m_emptyState->setVisible(true);
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
    m_beforeAfterMode = m_beforeAfterBtn->isChecked();
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

    QPixmap pixmap = QPixmap::fromImage(image);
    if (w != image.width() || h != image.height()) {
        pixmap = pixmap.scaled(w, h, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
    if (m_beforeAfterMode && !m_afterImage.isNull()) return m_afterImage;
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

QImage PreviewPanel::convertRawToDisplayable(const QString& filePath) {
    RawImageLoader loader;
    RawImageLoader::PreviewData preview;
    RawImageLoader::Metadata metadata;
    constexpr int kPreviewLongSide = 2400;
    if (!loader.loadPreview(filePath.toStdString(), kPreviewLongSide,
                            preview, &metadata)) {
        return QImage();
    }

    m_imageIso = metadata.iso;
    m_imageExposure = metadata.exposureTime;
    m_imageFocalLength = metadata.focalLength;

    QImage image;
    if (preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
        image = QImage::fromData(preview.bytes.data(),
                                 static_cast<int>(preview.bytes.size()), "JPEG");
    } else if (preview.width > 0 && preview.height > 0) {
        QImage borrowed(preview.bytes.data(), preview.width, preview.height,
                        preview.width * 3, QImage::Format_RGB888);
        image = borrowed.copy();
    }
    if (image.isNull()) return image;
    if (std::max(image.width(), image.height()) > kPreviewLongSide) {
        image = image.scaled(kPreviewLongSide, kPreviewLongSide,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void PreviewPanel::fitToView() {
    onFitView();
}

void PreviewPanel::resetZoom() {
    onZoom100();
}

void PreviewPanel::setBeforeAfterMode(bool enabled) {
    if (m_beforeAfterBtn) {
        m_beforeAfterBtn->setChecked(enabled);
    }
    onToggleBeforeAfter();
}

void PreviewPanel::setBeforeImage(const QImage& image) {
    m_beforeImage = image;
}

void PreviewPanel::setAfterImage(const QImage& image) {
    m_afterImage = image;
    if (m_beforeAfterMode) {
        onToggleBeforeAfter();
    }
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
