#pragma once

#include "core/FinishingPipeline.h"
#include "ui/WorkflowRail.h"
#include "workers/FrameReceipt.h"

#include <QImage>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <functional>
#include <memory>

class QAction;
class QDialog;
class ExportWorker;
class HistoryPreviewWorker;
class QLabel;
class QMenu;
class MaskPreviewWorker;
class ParamsPanel;
class PreviewPanel;
class PrintOrderPanel;
struct PrintOrderDocument;
class QStackedWidget;
class ProcessingWorker;
class ProjectPanel;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTimer;
class QuickPreviewWorker;
class TaskStatusBar;
class Toolbar;
class UpdateManager;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void importFiles(const QStringList& paths);
    void selectStartupScene(ProcessingScene scene);
    void selectStartupInspectorOutput();
    void selectStartupWorkflowStage(WorkflowStage stage);
    void loadStartupHistory(const QString& path);
    using LocateOrderDirectory = std::function<QString(const QString& missingFile)>;
    bool savePrintOrderTo(const QString& path, QString& error);
    bool openPrintOrderFrom(const QString& path, QString& error,
                            const LocateOrderDirectory& locate = {});
    void requestOpenPrintOrder(const QString& path = {});
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct DeliverySnapshot {
        uint64_t orderGeneration;
        QString processingSignature;
    };

    bool handleSingleKeyShortcut(QKeyEvent* event);
    void setupCentralWidget();
    void connectPrintOrder();
    void addDocumentActions(QMenu* menu);
    bool requestSavePrintOrder(bool saveAs = false);
    bool confirmOrderReplacement();
    void capturePrintOrder(PrintOrderDocument& document) const;
    QString orderEditSignature() const;
    void showPrintOrder();
    void showAdvanced(WorkflowStage stage);
    void inspectProof();
    void inspectFrameReceipt();
    void clearDeliveryReceipt();
    void publishDeliveryReceipt(const QString& path);
    void revealDelivery(const QString& path);
    void selectProduct(ProcessingScene scene);
    void adaptStillScene();
    void resetProject();
    void refreshPrintOrderResult();
    void refreshExportAction();
    void setTaskFeedback(bool busy, int progress, const QString& message);
    bool matchesDeliverySnapshot(const DeliverySnapshot& snapshot) const;
    void finishStaleDelivery(const QString& savedPath);
    void unlockDeliveryTask();
    QString sceneName() const;
    int requiredFrameCount() const;
    void activateScene(ProcessingScene scene, bool resetParameters = true);
    void updateProjectReadiness();
    void setupMenuBar();
    void setupStatusBar();
    void setWorkflowStage(int stage, const QString& status, bool complete = false);
    void setupConnections();
    static QString formatExposureTime(double seconds);
    bool processingActive() const;
    QString effectiveReferenceFrame() const;
    QString effectiveSingleFrameSource() const;
    bool discardEditedMaskForDifferentReference();
    QString currentUpstreamSignature() const;
    QString currentProcessingSignature() const;
    void handleProcessingParametersChanged();
    FinishingOptions currentFinishingOptions() const;
    bool quickPreviewEligible() const;
    bool currentQuickPreviewAvailable() const;
    void showBestAvailableResult();
    void cancelQuickPreview(bool waitForDone);
    void scheduleQuickPreview();
    void startQuickPreview();
    void invalidateCachedResult();
    void markCachedResultStale(const QString& reason);
    void restoreLastValidResultAfterFailedRun();

private slots:
    void onImportClicked();
    void onImportFolderClicked();
    void onViewMetadata(const QString& filePath);
    void onAboutClicked();
    void onProcessClicked();
    void onExportClicked();
    void onSettingsClicked();
    void showShortcutsDialog();
    void loadRecentResultPreview(const QString& path);

private:
    PrintOrderPanel* m_printOrder = nullptr;
    QAction* m_startAction = nullptr;
    QAction* m_exportAction = nullptr;
    QAction* m_openOrderAction = nullptr;
    QAction* m_saveOrderAction = nullptr;
    QAction* m_saveOrderAsAction = nullptr;
    QString m_orderFilePath;
    QString m_orderFileBaseline;
    QString m_comparisonManifest;
    QString m_deliveryPath;
    FrameReceipt m_formalFrameReceipt;
    QPointer<QDialog> m_receiptDialog;
    QString m_taskMessage;
    int m_taskProgress = 0;
    bool m_automaticStill = true;
    bool m_resettingProject = false;
    uint64_t m_orderGeneration = 0;
    Toolbar* m_toolbar = nullptr;
    WorkflowRail* m_workflowRail = nullptr;
    QSplitter* m_contentSplitter = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    PreviewPanel* m_previewPanel = nullptr;
    ParamsPanel* m_paramsPanel = nullptr;
    QProgressBar* m_inlineProgress = nullptr;
    TaskStatusBar* m_taskStatusBar = nullptr;
    QLabel* m_pixelInfoLabel = nullptr;
    QPushButton* m_undoRemoveBtn = nullptr;
    ProcessingScene m_scene = ProcessingScene::Nightscape;
    bool m_sceneActive = false;
    QAction* m_beforeAfterAction = nullptr;
    UpdateManager* m_updateManager = nullptr;
    ProcessingWorker* m_worker = nullptr;
    MaskPreviewWorker* m_maskPreviewWorker = nullptr;
    QSet<MaskPreviewWorker*> m_activeMaskPreviewWorkers;
    QuickPreviewWorker* m_quickPreviewWorker = nullptr;
    ExportWorker* m_exportWorker = nullptr;
    HistoryPreviewWorker* m_historyPreviewWorker = nullptr;
    QTimer* m_quickPreviewTimer = nullptr;
    uint64_t m_quickPreviewGeneration = 0;
    uint64_t m_editedMaskRevision = 0;
    std::vector<uint8_t> m_editedSkyGroundMask;
    int m_editedSkyGroundMaskWidth = 0;
    int m_editedSkyGroundMaskHeight = 0;
    QStringList m_editedMaskProjectFiles;
    QString m_editedMaskSourceFile;
    QString m_maskOverlaySourceFile;
    bool m_quickPreviewPending = false;

    // 缓存最后一次堆栈结果（用于导出）
    std::vector<uint16_t> m_cachedStackedData;
    QImage m_cachedBeforePreview;
    QImage m_sourceProof;
    bool m_sourcePreviewFailed = false;
    QImage m_formalProof;
    QImage m_quickProof;
    uint16_t m_cachedBeforeBlackPoint = 0;
    uint16_t m_cachedBeforeWhitePoint = 65535;
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    int m_cachedFrameCount = 0;
    std::shared_ptr<const std::vector<uint16_t>> m_quickPreviewSource;
    std::shared_ptr<const std::vector<uint8_t>> m_quickPreviewMask;
    std::vector<uint16_t> m_cachedQuickPreviewResult;
    QImage m_lastValidResultPreview;
    QString m_lastValidResultPreviewKey;
    bool m_resultWasVisibleBeforeRun = false;
    int m_quickPreviewWidth = 0;
    int m_quickPreviewHeight = 0;
    QString m_quickPreviewSignature;
    QString m_lastProcessedSignature;
    QString m_lastProcessedUpstreamSignature;
};
