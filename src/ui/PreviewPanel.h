#pragma once

#include <QWidget>
#include <QImage>
#include <QThreadPool>

#include <atomic>

class QScrollArea;
class QLabel;
class QPushButton;
class QTimer;
class QButtonGroup;

class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget* parent = nullptr);
    ~PreviewPanel() override;

    void loadImage(const QString& filePath);     // 通过 RawImageLoader 加载
    void loadImage(const QImage& image);         // 直接加载 QImage
    void load16BitImage(const std::vector<uint16_t>& data, int w, int h); // 加载 16-bit 单通道并 tone mapping
    void loadRgb16BitImage(const std::vector<uint16_t>& rgb, int w, int h); // 加载 16-bit RGB 并 tone mapping
    void loadRgb16BitComparison(const QImage& before,
                                const std::vector<uint16_t>& afterRgb,
                                int w, int h,
                                uint16_t blackPoint,
                                uint16_t whitePoint);
    void clearImage();

    void setZoom(double zoom);
    double zoom() const;
    void fitToView();
    void resetZoom();

    void setInfo(const QString& info);           // 底部信息栏文字
    void setBeforeAfterMode(bool enabled);
    void setBeforeImage(const QImage& image);
    void setAfterImage(const QImage& image);
    void setComparisonImages(const QImage& before, const QImage& after);
    bool hasComparison() const;
    bool isShowingResult() const;
    void setResultAvailable(bool available, bool viewingResult = false);
    void clearComparison();

    void setMaskOverlay(const std::vector<uint8_t>& mask, int w, int h);
    void clearMaskOverlay();

    QImage currentImage() const;

signals:
    void zoomChanged(double zoom);
    void importRequested();                      // 点击空状态的导入按钮
    void mousePixelInfo(int x, int y, int r, int g, int b);
    void comparisonAvailabilityChanged(bool available);
    void beforeAfterModeChanged(bool enabled);
    void resultRequested();
    void sourcePreviewReady(const QString& filePath, const QImage& image,
                            int iso, double exposureTime, double aperture,
                            int focalLength);
    void sourcePreviewFailed(const QString& filePath);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onFitView();
    void onZoom100();
    void onToggleBeforeAfter();
    void onToggleInfo();
    void onZoomIn();
    void onZoomOut();
    void onViewModeChanged(int id);

private:
    void setupUI();
    void setupEmptyState();
    void setupImageView();
    void setupTopBar();
    void setupBottomBar();
    void updateZoomDisplay();
    void updateImageDisplay();
    void applyZoom();
    double maximumSafeZoom() const;
    const QImage& displayedImage() const;
    void resetComparison();
    QString formatExposureTime(double seconds) const;

    // 空状态
    QWidget* m_emptyState = nullptr;
    QLabel* m_emptyIcon = nullptr;
    QLabel* m_emptyText = nullptr;
    QLabel* m_emptyFormat = nullptr;
    QPushButton* m_emptyImportBtn = nullptr;

    // 图像显示
    QScrollArea* m_scrollArea = nullptr;
    QLabel* m_imageLabel = nullptr;
    QWidget* m_imageContainer = nullptr;

    // 顶部工具栏
    QWidget* m_topBar = nullptr;
    QPushButton* m_fitBtn = nullptr;
    QPushButton* m_zoom100Btn = nullptr;
    QPushButton* m_beforeAfterBtn = nullptr;
    QPushButton* m_infoBtn = nullptr;
    QPushButton* m_zoomInBtn = nullptr;
    QPushButton* m_zoomOutBtn = nullptr;
    QPushButton* m_resultBtn = nullptr;
    QWidget* m_compareControl = nullptr;
    QButtonGroup* m_viewModeGroup = nullptr;
    QPushButton* m_beforeBtn = nullptr;
    QPushButton* m_afterBtn = nullptr;
    QPushButton* m_splitBtn = nullptr;
    QLabel* m_zoomLabel = nullptr;

    // 底部信息栏
    QWidget* m_bottomBar = nullptr;
    QLabel* m_bottomInfo = nullptr;
    QLabel* m_mouseInfo = nullptr;

    // 数据
    QImage m_currentImage;
    QImage m_beforeImage;
    QImage m_afterImage;
    double m_zoom = 1.0;
    bool m_panning = false;
    QPoint m_lastPanPos;
    bool m_beforeAfterMode = false;
    bool m_showInfo = true;
    bool m_showingResult = false;
    int m_viewMode = 1; // 0=处理前，1=处理后，2=分屏
    QString m_currentFilePath;
    int m_imageIso = 0;
    double m_imageExposure = 0.0;
    int m_imageFocalLength = 0;
    QString m_imageFileName;

    // 蒙版叠加
    QImage m_maskOverlay;
    bool m_maskOverlayVisible = false;

    QThreadPool m_previewPool;
    std::atomic<uint64_t> m_previewGeneration{0};
};
