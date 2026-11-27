#include "app/MainWindow.h"

#include "ui/FileDialogUtils.h"
#include "ui/FrameReceiptDialog.h"
#include "ui/ParamsPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/PrintOrderPanel.h"
#include "ui/RidgeComparisonDialog.h"
#include "ui/ProjectPanel.h"
#include "ui/TaskStatusBar.h"
#include "ui/Toolbar.h"
#include "workers/HistoryPreviewWorker.h"
#include "workers/MaskPreviewWorker.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// 大图使用不透明中性背景，缩放只影响显示坐标，纸面效果不会进入印样。
class NeutralProof final : public QWidget {
public:
    NeutralProof(const QImage& image, QWidget* parent)
        : QWidget(parent), m_image(image) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(23, 25, 25));
        const QSize size = m_image.size().scaled(this->size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, m_image);
    }
private:
    QImage m_image;
};
}

void MainWindow::connectPrintOrder() {
    connect(m_printOrder, &PrintOrderPanel::openOrderRequested, this, [this] { requestOpenPrintOrder(); });
    connect(m_printOrder, &PrintOrderPanel::saveOrderRequested, this, [this] { requestSavePrintOrder(); });
    connect(m_printOrder, &PrintOrderPanel::ridgeComparisonRequested, this, [this] {
        QString error;
        if (!m_comparisonManifest.isEmpty() &&
            !RidgeComparisonDialog::openForManifest(m_comparisonManifest, this, error))
            QMessageBox::warning(this, QCoreApplication::translate("Nebulens", "山脊对照"), error);
    });
    connect(m_printOrder, &PrintOrderPanel::productRequested, this, &MainWindow::selectProduct);
    connect(m_printOrder, &PrintOrderPanel::importFilesRequested, this, &MainWindow::onImportClicked);
    connect(m_printOrder, &PrintOrderPanel::importFolderRequested, this, &MainWindow::onImportFolderClicked);
    connect(m_printOrder, &PrintOrderPanel::advancedRequested, this, &MainWindow::showAdvanced);
    connect(m_printOrder, &PrintOrderPanel::formRequested, this, &MainWindow::showPrintOrder);
    connect(m_printOrder, &PrintOrderPanel::exposureRequested, m_paramsPanel, &ParamsPanel::setExposureTenths);
    connect(m_printOrder, &PrintOrderPanel::proofRequested, this, &MainWindow::inspectProof);
    connect(m_printOrder, &PrintOrderPanel::frameReceiptRequested, this, &MainWindow::inspectFrameReceipt);
    connect(m_printOrder, &PrintOrderPanel::formatRequested, m_paramsPanel, &ParamsPanel::setOutputFormat);
    connect(m_printOrder, &PrintOrderPanel::directoryRequested, this, [this]() {
        if (processingActive()) return;
        const QString path = FileDialogUtils::getExistingDirectory(this,
            QCoreApplication::translate("Nebulens", "选择输出目录"), m_paramsPanel->outputPath());
        if (!path.isEmpty()) {
            m_paramsPanel->setOutputPath(path);
            m_printOrder->setOutput(m_paramsPanel->outputFormat(), path);
        }
    });
    connect(m_printOrder, &PrintOrderPanel::processRequested, this, &MainWindow::onProcessClicked);
    connect(m_printOrder, &PrintOrderPanel::newOrderRequested, this, [this] {
        if (m_orderFilePath.isEmpty() || confirmOrderReplacement()) resetProject();
    });
    connect(m_printOrder, &PrintOrderPanel::revealRequested, this, &MainWindow::revealDelivery);
    connect(m_toolbar, &Toolbar::printOrderRequested, this, &MainWindow::showPrintOrder);
    connect(m_paramsPanel, &ParamsPanel::referenceChoiceChanged, m_projectPanel,
            qOverload<const QString&>(&ProjectPanel::setReferenceFrame));
}

void MainWindow::showPrintOrder() {
    if (m_printOrder->showingDetailPage()) m_printOrder->rememberDetailStage(m_workflowRail->currentStage());
    m_printOrder->showDetailPage(false);
    statusBar()->hide();
    updateProjectReadiness();
    refreshPrintOrderResult();
    refreshExportAction();
}

void MainWindow::showAdvanced(WorkflowStage stage) {
    m_printOrder->showDetailPage(true);
    statusBar()->show();
    m_workflowRail->setCurrentStage(stage, true);
    refreshPrintOrderResult();
    refreshExportAction();
}

void MainWindow::inspectProof() {
    refreshPrintOrderResult();
    const QString caption = m_printOrder->proofCaption();
    const QImage image = m_printOrder->proofImage();
    if (image.isNull()) return;
    auto* dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("neutralProofDialog"));
    const QString title = QCoreApplication::translate("Nebulens", "查看大图");
    dialog->setWindowTitle(caption.isEmpty() ? title : QStringLiteral("%1 · %2").arg(title, caption));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(size() * 0.85);
    auto* layout = new QVBoxLayout(dialog);
    layout->addWidget(new NeutralProof(image, dialog), 1);
    auto* buttons = new QDialogButtonBox(dialog);
    buttons->addButton(QCoreApplication::translate("Nebulens", "返回冲印单"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    m_printOrder->setProofInspectionActive(true);
    connect(dialog, &QDialog::finished, m_printOrder,
            [panel=m_printOrder]() { panel->setProofInspectionActive(false); });
    dialog->open();
}

void MainWindow::selectProduct(ProcessingScene scene) {
    if (processingActive()) return;
    m_automaticStill = scene == ProcessingScene::Nightscape;
    if (m_automaticStill && m_projectPanel->includedFilePaths().size() == 1) {
        scene = ProcessingScene::SingleFrame;
    }
    activateScene(scene);
}

void MainWindow::inspectFrameReceipt() {
    if (processingActive() || m_resettingProject || m_deliveryPath.isEmpty() ||
        m_formalFrameReceipt.rows.isEmpty()) return;
    if (m_receiptDialog) {
        m_receiptDialog->raise();
        m_receiptDialog->activateWindow();
        return;
    }
    auto* dialog = new FrameReceiptDialog(m_formalFrameReceipt, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_receiptDialog = dialog;
    m_printOrder->setProofInspectionActive(true);
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (m_receiptDialog == dialog) {
            m_receiptDialog.clear();
            m_printOrder->setProofInspectionActive(false);
        }
    });
    dialog->open();
}

void MainWindow::clearDeliveryReceipt() {
    if (m_receiptDialog) m_receiptDialog->reject();
    m_deliveryPath.clear();
    m_printOrder->clearReceipt();
}

void MainWindow::publishDeliveryReceipt(const QString& path) {
    m_deliveryPath = path;
    m_printOrder->setReceipt(path, m_formalFrameReceipt.deliverables, m_formalFrameReceipt);
}

void MainWindow::adaptStillScene() {
    if (!m_automaticStill || m_resettingProject || processingActive()) return;
    const auto count = m_projectPanel->includedFilePaths().size();
    if (count == 0) return;
    const auto wanted = count == 1 ? ProcessingScene::SingleFrame : ProcessingScene::Nightscape;
    if (m_scene != wanted) activateScene(wanted);
}

void MainWindow::revealDelivery(const QString& path) {
    const QFileInfo info(path);
    if (!path.isEmpty() && info.exists() &&
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()))) return;
    QMessageBox::warning(this, QCoreApplication::translate("Nebulens", "输出目录"),
                         QCoreApplication::translate("Nebulens", "无法打开成品位置：%1").arg(path));
}

void MainWindow::resetProject() {
    if (processingActive()) return;
    m_resettingProject = true;
    m_orderFilePath.clear();
    m_orderFileBaseline.clear();
    m_comparisonManifest.clear();
    m_printOrder->setRidgeComparisonAvailable(false);
    setWindowFilePath({});
    ++m_orderGeneration;
    for (auto* worker : m_activeMaskPreviewWorkers) worker->requestInterruption();
    m_maskPreviewWorker = nullptr; // 活动任务仍由集合持有，过期回调不得恢复蒙版。
    if (m_historyPreviewWorker) m_historyPreviewWorker->requestInterruption();
    invalidateCachedResult();
    m_sourceProof = {};
    m_sourcePreviewFailed = false;
    m_lastValidResultPreview = {};
    m_lastValidResultPreviewKey.clear();
    m_resultWasVisibleBeforeRun = false;
    m_editedSkyGroundMask.clear();
    m_editedSkyGroundMaskWidth = m_editedSkyGroundMaskHeight = 0;
    m_editedMaskProjectFiles.clear();
    m_editedMaskSourceFile.clear();
    m_maskOverlaySourceFile.clear();
    ++m_editedMaskRevision;
    m_projectPanel->clearFiles();
    m_previewPanel->clearMaskOverlay();
    m_previewPanel->clearImage();
    m_paramsPanel->clearProjectSources();
    m_printOrder->resetOrder();
    m_taskProgress = 0;
    m_taskMessage.clear();
    m_automaticStill = true;
    activateScene(ProcessingScene::Nightscape);
    m_resettingProject = false;
    showPrintOrder();
}

void MainWindow::refreshPrintOrderResult() {
    if (m_resettingProject) return;
    const QString source = m_projectPanel->currentFilePath();
    QImage proof = source.isEmpty() ? QImage() : m_sourceProof;
    bool isSource = true;
    QString caption;
    if (!source.isEmpty()) {
        if (proof.isNull())
            caption = m_sourcePreviewFailed ? QCoreApplication::translate("Nebulens", "原片预览不可用")
                                            : QCoreApplication::translate("Nebulens", "正在读取原片…");
        else
            caption = QCoreApplication::translate("Nebulens", "原片 · %1").arg(QFileInfo(source).fileName());
    }
    if (!m_lastProcessedSignature.isEmpty()) {
        const QString upstream = currentUpstreamSignature();
        const QString finishing = m_paramsPanel->finishingSignature();
        const bool formalCurrent = upstream + QStringLiteral("||") + finishing ==
            m_lastProcessedSignature;
        if (!formalCurrent && !m_deliveryPath.isEmpty()) {
            clearDeliveryReceipt();
        }
        if (formalCurrent) {
            isSource = false;
            proof = m_formalProof;
            caption = m_scene == ProcessingScene::Timelapse
                ? QCoreApplication::translate("Nebulens", "本次延时序列 · 中间帧")
                : QCoreApplication::translate("Nebulens", "处理结果");
        } else if (upstream == m_lastProcessedUpstreamSignature) {
            if (!m_quickProof.isNull() && m_quickPreviewSignature == finishing) {
                isSource = false;
                proof = m_quickProof;
                caption = QCoreApplication::translate("Nebulens", "快速预览已更新 · 完整导出需重新处理");
            } else if (!m_formalProof.isNull()) {
                isSource = false;
                proof = m_formalProof;
                caption = QCoreApplication::translate("Nebulens", "参数已修改 · 这是上次处理的成片");
            }
        }
    }
    m_printOrder->setProof(proof, caption, isSource);
    // 只有在看原片的调整章节才以试样代替画布显示；正式像素和取样来源不变。
    const bool inspectExposure = isSource && !processingActive() && m_printOrder->showingDetailPage() &&
        m_workflowRail->currentStage() == WorkflowStage::Adjustment && !m_previewPanel->isShowingResult();
    const QImage draft = inspectExposure ? m_printOrder->sourceExposureProof() : QImage{};
    m_previewPanel->setSourceExposureProof(m_sourceProof, draft,
        draft.isNull() ? QString() : m_printOrder->exposureDraftCaption());
}

void MainWindow::refreshExportAction() {
    const bool current = m_scene != ProcessingScene::Timelapse &&
        !m_resettingProject && !processingActive() &&
        !m_cachedStackedData.empty() && !m_lastProcessedSignature.isEmpty() &&
        !m_previewPanel->isShowingHistoryResult() &&
        currentProcessingSignature() == m_lastProcessedSignature;
    m_exportAction->setEnabled(current);
    m_toolbar->enableExport(current);
}

void MainWindow::setTaskFeedback(bool busy, int progress, const QString& message) {
    m_taskMessage = message;
    m_taskProgress = progress;
    m_printOrder->setTask(busy, progress, message);
    for (auto* action : {m_openOrderAction, m_saveOrderAction, m_saveOrderAsAction})
        if (action) action->setEnabled(!busy);
}

bool MainWindow::matchesDeliverySnapshot(const DeliverySnapshot& snapshot) const {
    return snapshot.orderGeneration == m_orderGeneration &&
        snapshot.processingSignature == currentProcessingSignature();
}

void MainWindow::finishStaleDelivery(const QString& savedPath) {
    invalidateCachedResult();
    m_paramsPanel->setProcessingReport({});
    if (m_previewPanel->isShowingResult()) {
        const QString source = m_projectPanel->currentFilePath();
        m_sourceProof = {};
        m_sourcePreviewFailed = false;
        m_previewPanel->clearImage();
        if (!source.isEmpty()) m_previewPanel->loadImage(source);
    }
    m_previewPanel->setResultAvailable(!m_lastValidResultPreview.isNull());
    for (const auto stage : {WorkflowStage::Alignment, WorkflowStage::Stacking,
                            WorkflowStage::Adjustment, WorkflowStage::Output}) {
        m_workflowRail->setStageComplete(stage, false);
    }

    const QString update = QCoreApplication::translate("Nebulens", "结果需要更新");
    const QString message = savedPath.isEmpty() ? update :
        QStringLiteral("%1 · %2").arg(update,
            QCoreApplication::translate("Nebulens", "已导出到：%1").arg(savedPath));
    if (!savedPath.isEmpty()) m_paramsPanel->refreshRecentResults();
    setTaskFeedback(false, -1, message);
    statusBar()->showMessage(message, 5000);
}

void MainWindow::unlockDeliveryTask() {
    m_toolbar->setProcessing(false);
    m_projectPanel->setEditingEnabled(true);
    m_paramsPanel->setEnabled(true);
    m_inlineProgress->setRange(0, 100);
    m_taskStatusBar->setProgressVisible(false);
    refreshPrintOrderResult();
    refreshExportAction();
}
