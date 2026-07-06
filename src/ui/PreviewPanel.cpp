#include "PreviewPanel.h"
#include "../core/RawImageLoader.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGraphicsSceneMouseEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QDebug>
#include <QApplication>
#include <algorithm>

PreviewPanel::PreviewPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void PreviewPanel::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 创建场景和视图
    m_scene = new QGraphicsScene(this);
    m_graphicsView = new QGraphicsView(m_scene, this);
    m_graphicsView->setRenderHints(QPainter::SmoothPixmapTransform);
    m_graphicsView->setDragMode(QGraphicsView::NoDrag);
    m_graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_graphicsView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    m_graphicsView->setBackgroundBrush(QBrush(QColor("#0D1117")));
    m_graphicsView->setFrameShape(QFrame::NoFrame);
    m_graphicsView->setStyleSheet("QGraphicsView { border: none; background-color: #0D1117; }");
    m_graphicsView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_pixmapItem = new QGraphicsPixmapItem();
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    m_scene->addItem(m_pixmapItem);

    // Before/After 分栏（初始隐藏）
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setVisible(false);

    m_beforeScene = new QGraphicsScene(this);
    m_afterScene = new QGraphicsScene(this);

    m_beforeView = new QGraphicsView(m_beforeScene, m_splitter);
    m_afterView = new QGraphicsView(m_afterScene, m_splitter);

    for (auto* view : {m_beforeView, m_afterView}) {
        view->setRenderHints(QPainter::SmoothPixmapTransform);
        view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        view->setBackgroundBrush(QBrush(QColor("#0D1117")));
        view->setFrameShape(QFrame::NoFrame);
        view->setStyleSheet("QGraphicsView { border: none; background-color: #0D1117; }");
    }

    m_beforeItem = new QGraphicsPixmapItem();
    m_afterItem = new QGraphicsPixmapItem();
    m_beforeScene->addItem(m_beforeItem);
    m_afterScene->addItem(m_afterItem);

    m_splitter->addWidget(m_beforeView);
    m_splitter->addWidget(m_afterView);
    m_splitter->setSizes({width() / 2, width() / 2});

    layout->addWidget(m_graphicsView, 1);
    layout->addWidget(m_splitter, 1);

    // 状态栏标签
    m_statusLabel = new QLabel("就绪 | 缩放: 100% | 位置: —", this);
    m_statusLabel->setStyleSheet(
        "QLabel { background-color: #161B22; color: #8B949E; padding: 4px 8px; font-size: 12px; border-top: 1px solid #30363D; }"
    );
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_statusLabel);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this]() {
        updateStatusInfo();
    });
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

    updateImageDisplay();
    fitInView();
}

void PreviewPanel::clearImage() {
    m_currentImage = QImage();
    m_beforeImage = QImage();
    m_afterImage = QImage();
    m_pixmapItem->setPixmap(QPixmap());
    m_beforeItem->setPixmap(QPixmap());
    m_afterItem->setPixmap(QPixmap());
    m_scene->setSceneRect(QRectF());
    m_beforeScene->setSceneRect(QRectF());
    m_afterScene->setSceneRect(QRectF());
    m_statusLabel->setText("就绪 | 缩放: 100% | 位置: —");
}

void PreviewPanel::setBeforeAfterMode(bool enabled) {
    m_beforeAfterMode = enabled;
    m_graphicsView->setVisible(!enabled);
    m_splitter->setVisible(enabled);

    if (enabled) {
        updateImageDisplay();
    } else {
        m_pixmapItem->setPixmap(QPixmap::fromImage(m_currentImage));
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
    }
}

void PreviewPanel::setBeforeImage(const QImage& image) {
    m_beforeImage = image;
    if (m_beforeAfterMode) {
        m_beforeItem->setPixmap(QPixmap::fromImage(m_beforeImage));
        m_beforeScene->setSceneRect(m_beforeItem->boundingRect());
    }
}

void PreviewPanel::setAfterImage(const QImage& image) {
    m_afterImage = image;
    if (m_beforeAfterMode) {
        m_afterItem->setPixmap(QPixmap::fromImage(m_afterImage));
        m_afterScene->setSceneRect(m_afterItem->boundingRect());
    }
}

void PreviewPanel::setZoom(double zoom) {
    m_zoom = std::clamp(zoom, 0.01, 50.0);

    if (m_beforeAfterMode) {
        QTransform transform;
        transform.scale(m_zoom, m_zoom);
        m_beforeView->setTransform(transform);
        m_afterView->setTransform(transform);
    } else {
        QTransform transform;
        transform.scale(m_zoom, m_zoom);
        m_graphicsView->setTransform(transform);
    }

    emit zoomChanged(m_zoom);
    updateStatusInfo();
}

double PreviewPanel::zoom() const {
    return m_zoom;
}

QImage PreviewPanel::currentImage() const {
    return m_currentImage;
}

void PreviewPanel::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl + 滚轮 = 缩放
        double delta = event->angleDelta().y() > 0 ? 1.25 : 0.8;
        setZoom(m_zoom * delta);
        event->accept();
    } else {
        // 普通滚轮传递给视图
        if (m_beforeAfterMode) {
            QApplication::sendEvent(m_afterView, event);
        } else {
            QApplication::sendEvent(m_graphicsView, event);
        }
    }
}

void PreviewPanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
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

        if (m_beforeAfterMode) {
            m_beforeView->horizontalScrollBar()->setValue(m_beforeView->horizontalScrollBar()->value() - delta.x());
            m_beforeView->verticalScrollBar()->setValue(m_beforeView->verticalScrollBar()->value() - delta.y());
            m_afterView->horizontalScrollBar()->setValue(m_afterView->horizontalScrollBar()->value() - delta.x());
            m_afterView->verticalScrollBar()->setValue(m_afterView->verticalScrollBar()->value() - delta.y());
        } else {
            m_graphicsView->horizontalScrollBar()->setValue(m_graphicsView->horizontalScrollBar()->value() - delta.x());
            m_graphicsView->verticalScrollBar()->setValue(m_graphicsView->verticalScrollBar()->value() - delta.y());
        }
    }

    // 计算鼠标在图像中的位置
    if (!m_currentImage.isNull()) {
        QGraphicsView* activeView = m_beforeAfterMode ? m_afterView : m_graphicsView;
        QPointF scenePos = activeView->mapToScene(activeView->mapFromParent(event->pos()));
        int x = static_cast<int>(scenePos.x());
        int y = static_cast<int>(scenePos.y());

        if (x >= 0 && x < m_currentImage.width() && y >= 0 && y < m_currentImage.height()) {
            QRgb pixel = m_currentImage.pixel(x, y);
            emit mousePixelInfo(x, y, qRed(pixel), qGreen(pixel), qBlue(pixel));

            m_statusLabel->setText(QString("缩放: %1% | 位置: (%2, %3) | RGB: (%4, %5, %6)")
                .arg(qRound(m_zoom * 100))
                .arg(x).arg(y)
                .arg(qRed(pixel)).arg(qGreen(pixel)).arg(qBlue(pixel)));
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

void PreviewPanel::updateImageDisplay() {
    if (m_currentImage.isNull()) return;

    if (m_beforeAfterMode) {
        m_beforeItem->setPixmap(QPixmap::fromImage(m_beforeImage));
        m_afterItem->setPixmap(QPixmap::fromImage(m_afterImage));
        m_beforeScene->setSceneRect(m_beforeItem->boundingRect());
        m_afterScene->setSceneRect(m_afterItem->boundingRect());
    } else {
        m_pixmapItem->setPixmap(QPixmap::fromImage(m_currentImage));
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
    }
}

void PreviewPanel::updateStatusInfo() {
    if (!m_currentImage.isNull()) {
        m_statusLabel->setText(QString("缩放: %1% | 图像: %2x%3")
            .arg(qRound(m_zoom * 100))
            .arg(m_currentImage.width())
            .arg(m_currentImage.height()));
    }
}

void PreviewPanel::fitInView() {
    if (m_currentImage.isNull()) return;

    if (m_beforeAfterMode) {
        m_beforeView->fitInView(m_beforeItem->boundingRect(), Qt::KeepAspectRatio);
        m_afterView->fitInView(m_afterItem->boundingRect(), Qt::KeepAspectRatio);
        m_zoom = m_beforeView->transform().m11();
    } else {
        m_graphicsView->fitInView(m_pixmapItem->boundingRect(), Qt::KeepAspectRatio);
        m_zoom = m_graphicsView->transform().m11();
    }
    emit zoomChanged(m_zoom);
    updateStatusInfo();
}

QImage PreviewPanel::convertRawToDisplayable(const QString& filePath) {
    RawImageLoader loader;
    RawImageLoader::ImageData imageData;

    if (!loader.loadRaw(filePath.toStdString(), imageData)) {
        return QImage();
    }

    QImage image;

    if (imageData.channels == 3) {
        // 已经是 RGB，直接转换为 8-bit QImage
        image = QImage(imageData.width, imageData.height, QImage::Format_RGB888);

        // 16-bit 到 8-bit 转换，带自动缩放
        uint16_t maxVal = 0;
        for (uint16_t v : imageData.data) {
            if (v > maxVal) maxVal = v;
        }
        if (maxVal < 256) maxVal = 255; // 已经是 8-bit 数据
        if (maxVal == 0) maxVal = 1;

        for (int y = 0; y < imageData.height; ++y) {
            for (int x = 0; x < imageData.width; ++x) {
                int idx = (y * imageData.width + x) * 3;
                int r = (imageData.data[idx + 0] * 255) / maxVal;
                int g = (imageData.data[idx + 1] * 255) / maxVal;
                int b = (imageData.data[idx + 2] * 255) / maxVal;
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                image.setPixelColor(x, y, QColor(r, g, b));
            }
        }
    } else {
        // Bayer 数据，先 demosaic 到 RGB
        std::vector<uint16_t> rgbData;
        if (!loader.decodeToRgb(imageData, rgbData)) {
            return QImage();
        }

        image = QImage(imageData.width, imageData.height, QImage::Format_RGB888);

        uint16_t maxVal = 0;
        for (uint16_t v : rgbData) {
            if (v > maxVal) maxVal = v;
        }
        if (maxVal < 256) maxVal = 255;
        if (maxVal == 0) maxVal = 1;

        for (int y = 0; y < imageData.height; ++y) {
            for (int x = 0; x < imageData.width; ++x) {
                int idx = (y * imageData.width + x) * 3;
                int r = (rgbData[idx + 0] * 255) / maxVal;
                int g = (rgbData[idx + 1] * 255) / maxVal;
                int b = (rgbData[idx + 2] * 255) / maxVal;
                r = std::clamp(r, 0, 255);
                g = std::clamp(g, 0, 255);
                b = std::clamp(b, 0, 255);
                image.setPixelColor(x, y, QColor(r, g, b));
            }
        }
    }

    return image;
}