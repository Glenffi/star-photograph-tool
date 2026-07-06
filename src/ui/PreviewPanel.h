#pragma once

#include <QWidget>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QSplitter>
#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QTimer>

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);

    void loadImage(const QString& filePath);
    void clearImage();
    void setBeforeAfterMode(bool enabled);
    void setBeforeImage(const QImage& image);
    void setAfterImage(const QImage& image);
    void setZoom(double zoom);
    double zoom() const;

    QImage currentImage() const;

signals:
    void zoomChanged(double zoom);
    void mousePixelInfo(int x, int y, int r, int g, int b);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void setupUI();
    void updateImageDisplay();
    void updateStatusInfo();
    void fitInView();
    QImage convertRawToDisplayable(const QString& filePath);

    // 单视图模式
    QGraphicsView* m_graphicsView = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QGraphicsPixmapItem* m_pixmapItem = nullptr;

    // Before/After 模式
    QSplitter* m_splitter = nullptr;
    QGraphicsView* m_beforeView = nullptr;
    QGraphicsView* m_afterView = nullptr;
    QGraphicsScene* m_beforeScene = nullptr;
    QGraphicsScene* m_afterScene = nullptr;
    QGraphicsPixmapItem* m_beforeItem = nullptr;
    QGraphicsPixmapItem* m_afterItem = nullptr;

    QLabel* m_statusLabel = nullptr;

    QImage m_currentImage;
    QImage m_beforeImage;
    QImage m_afterImage;
    double m_zoom = 1.0;
    bool m_beforeAfterMode = false;
    bool m_panning = false;
    QPoint m_lastPanPos;

    QTimer* m_statusTimer = nullptr;
};