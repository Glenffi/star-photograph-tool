#include "app/MainWindow.h"
#include <QSettings>
#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStackedWidget>
#include <QPushButton>
#include <QMessageBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QComboBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProgressBar>
#include <QDateTime>
#include <QDesktopServices>
#include <QDebug>
#include <QSet>
#include <QTimer>
#include <QSignalBlocker>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include "ui/ProjectPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/PrintOrderPanel.h"
#include "ui/ParamsPanel.h"
#include "ui/Toolbar.h"
#include "ui/UiAssets.h"
#include "ui/ProcessingScene.h"
#include "ui/StyleTokens.h"
#include "ui/TaskStatusBar.h"
#include "ui/WorkflowRail.h"
#include "ui/FileDialogUtils.h"
#include "i18n/AppLocalization.h"
#include "update/UpdateManager.h"

#include "core/RawImageLoader.h"
#include "core/ImageExporter.h"
#include "workers/MaskPreviewWorker.h"
#include "workers/ExportWorker.h"
#include "workers/ProcessingWorker.h"
#include "workers/QuickPreviewWorker.h"
#include "workers/HistoryPreviewWorker.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QCoreApplication::translate("Nebulens", "Nebulens — 星空影像工作台"));
    resize(1400, 900);
    setMinimumSize(920, 540);
    setAcceptDrops(true);

    setupCentralWidget();
    m_updateManager = new UpdateManager(this);
    setupMenuBar();
    setupStatusBar();
    setupConnections();
    connectPrintOrder();
    qApp->installEventFilter(this);

    QSettings settings("StarProcessor", "App");
    const int storedScene = settings.value(
        "ui/processingScene",
        static_cast<int>(ProcessingScene::Nightscape)).toInt();
    const bool hasPersistedProcessingSettings =
        settings.contains("stackMethod");
    const auto previous = static_cast<ProcessingScene>(std::clamp(storedScene, 0, 5));
    const bool previousStill = previous == ProcessingScene::Nightscape ||
                               previous == ProcessingScene::SingleFrame;
    activateScene(previousStill ? previous : ProcessingScene::Nightscape,
                  !previousStill || !hasPersistedProcessingSettings);
    showPrintOrder();

    connect(m_updateManager, &UpdateManager::statusMessage,
            this, [this](const QString& message, int timeoutMs) {
                statusBar()->showMessage(message, timeoutMs);
            });
    m_updateManager->scheduleAutomaticCheck();

    statusBar()->showMessage(QCoreApplication::translate(
        "Nebulens", "就绪 — 拖入 RAW 文件或点击导入开始"));
}

void MainWindow::importFiles(const QStringList& paths) {
    if (!processingActive() && !paths.isEmpty()) {
        m_projectPanel->addFiles(paths);
    }
}

void MainWindow::selectStartupScene(ProcessingScene scene) {
    m_automaticStill = false;
    activateScene(scene);
}

void MainWindow::selectStartupInspectorOutput() {
    showAdvanced(WorkflowStage::Output);
}

void MainWindow::selectStartupWorkflowStage(WorkflowStage stage) {
    showAdvanced(stage);
}

void MainWindow::loadStartupHistory(const QString& path) {
    showAdvanced(WorkflowStage::Output);
    QTimer::singleShot(0, this, [this, path]() {
        loadRecentResultPreview(path);
    });
}

MainWindow::~MainWindow() {
    if (qApp) qApp->removeEventFilter(this);
    if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
        m_quickPreviewWorker->requestCancel();
        m_quickPreviewWorker->wait();
    }
    // 等待 ProcessingWorker 真正结束（无超时，避免销毁运行中的线程）
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        m_worker->wait();
    }
    if (m_exportWorker && m_exportWorker->isRunning()) {
        m_exportWorker->requestCancel();
        m_exportWorker->wait();
    }
    if (m_historyPreviewWorker && m_historyPreviewWorker->isRunning()) {
        m_historyPreviewWorker->requestInterruption();
        m_historyPreviewWorker->wait();
    }
    // 等待所有 MaskPreviewWorker 真正结束
    for (MaskPreviewWorker* w : m_activeMaskPreviewWorkers) {
        if (w && w->isRunning()) {
            w->requestInterruption();
            w->wait();
        }
    }
    m_activeMaskPreviewWorkers.clear();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!m_orderFilePath.isEmpty() && !processingActive() && !confirmOrderReplacement()) {
        event->ignore();
        return;
    }
    if (m_paramsPanel) m_paramsPanel->saveCurrentSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!processingActive() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (processingActive()) {
        event->ignore();
        statusBar()->showMessage(QCoreApplication::translate("Nebulens", "处理中不能修改素材"), 3000);
        return;
    }
    const QMimeData* mimeData = event->mimeData();
    if (!mimeData->hasUrls()) return;

    QStringList filePaths;
    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile()) {
            filePaths.append(url.toLocalFile());
        }
    }

    if (!filePaths.isEmpty()) {
        if (filePaths.size() == 1 && filePaths.first().endsWith(QStringLiteral(".nebprint"), Qt::CaseInsensitive)) {
            requestOpenPrintOrder(filePaths.first());
            event->acceptProposedAction();
            return;
        }
        importFiles(filePaths);
        statusBar()->showMessage(
            QCoreApplication::translate("Nebulens", "拖放导入 %1 个文件").arg(filePaths.size()),
            5000
        );
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    Q_UNUSED(watched)
    if (event->type() == QEvent::KeyPress &&
        handleSingleKeyShortcut(static_cast<QKeyEvent*>(event))) {
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (handleSingleKeyShortcut(event)) {
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool MainWindow::handleSingleKeyShortcut(QKeyEvent* event) {
    if (m_workspacePages->currentWidget() != m_darkroom ||
        QApplication::activeModalWidget() ||
        QApplication::activePopupWidget()) {
        return false;
    }
    QWidget* focused = QApplication::focusWidget();
    const bool interactiveControl = qobject_cast<QLineEdit*>(focused) ||
        qobject_cast<QPlainTextEdit*>(focused) ||
        qobject_cast<QTextEdit*>(focused) ||
        qobject_cast<QAbstractSpinBox*>(focused) ||
        qobject_cast<QAbstractButton*>(focused) ||
        qobject_cast<QAbstractItemView*>(focused) ||
        qobject_cast<QAbstractSlider*>(focused) ||
        qobject_cast<QComboBox*>(focused);
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool commandModifier = modifiers.testFlag(Qt::ControlModifier) ||
        modifiers.testFlag(Qt::MetaModifier) ||
        modifiers.testFlag(Qt::AltModifier);
    if (interactiveControl || commandModifier) {
        return false;
    }

    switch (event->key()) {
    case Qt::Key_F:
        m_previewPanel->fitToView();
        return true;
    case Qt::Key_1:
        m_previewPanel->resetZoom();
        return true;
    case Qt::Key_B:
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            m_previewPanel->showSplitComparison();
        } else {
            m_previewPanel->cycleComparisonMode();
        }
        return true;
    case Qt::Key_M:
        m_previewPanel->toggleMaskOverlay();
        return true;
    case Qt::Key_R:
        showBestAvailableResult();
        return true;
    case Qt::Key_G:
        m_previewPanel->beginPointSelection();
        return true;
    case Qt::Key_BracketLeft:
        m_previewPanel->adjustMaskBrushSize(-4);
        return true;
    case Qt::Key_BracketRight:
        m_previewPanel->adjustMaskBrushSize(4);
        return true;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        m_previewPanel->zoomIn();
        return true;
    case Qt::Key_Minus:
        m_previewPanel->zoomOut();
        return true;
    case Qt::Key_Space:
        if (!processingActive()) m_projectPanel->toggleSelectedExclusion();
        return true;
    case Qt::Key_Escape:
        m_previewPanel->cancelInteractiveMode();
        return true;
    default:
        return false;
    }
}

void MainWindow::setupCentralWidget() {
    m_workspacePages = new QStackedWidget(this);
    setCentralWidget(m_workspacePages);
    m_printOrder = new PrintOrderPanel(m_workspacePages);
    m_workspacePages->addWidget(m_printOrder);
    m_darkroom = new QWidget(m_workspacePages);
    m_darkroom->setObjectName(QStringLiteral("advancedDarkroom"));
    m_workspacePages->addWidget(m_darkroom);

    auto* mainLayout = new QVBoxLayout(m_darkroom);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 顶部工具栏
    m_toolbar = new Toolbar(m_darkroom);
    mainLayout->addWidget(m_toolbar);

    auto* workspace = new QWidget(m_darkroom);
    workspace->setObjectName(QStringLiteral("mainWorkspace"));
    auto* workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    m_workflowRail = new WorkflowRail(workspace);
    workspaceLayout->addWidget(m_workflowRail);

    // The image canvas keeps the largest share; the filmstrip is part of
    // the editor column instead of competing with it as a left panel.
    m_contentSplitter = new QSplitter(Qt::Horizontal, workspace);
    m_contentSplitter->setHandleWidth(1);

    auto* editorColumn = new QWidget(m_contentSplitter);
    editorColumn->setObjectName(QStringLiteral("editorColumn"));
    auto* editorLayout = new QVBoxLayout(editorColumn);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    m_previewPanel = new PreviewPanel(editorColumn);
    editorLayout->addWidget(m_previewPanel, 1);

    m_projectPanel = new ProjectPanel(editorColumn);
    editorLayout->addWidget(m_projectPanel);
    m_contentSplitter->addWidget(editorColumn);

    m_paramsPanel = new ParamsPanel(m_contentSplitter);
    m_paramsPanel->setMinimumWidth(StyleTokens::Layout::kRightPanelMinimumWidth);
    m_paramsPanel->setMaximumWidth(StyleTokens::Layout::kRightPanelMaximumWidth);
    m_contentSplitter->addWidget(m_paramsPanel);

    QSettings settings("StarProcessor", "App");
    const int rightWidth = std::clamp(
        settings.value("ui/rightPanelWidth",
                       StyleTokens::Layout::kRightPanelWidth).toInt(),
        StyleTokens::Layout::kRightPanelMinimumWidth,
        StyleTokens::Layout::kRightPanelMaximumWidth);
    m_contentSplitter->setSizes({960, rightWidth});
    m_contentSplitter->setStretchFactor(0, 1);
    m_contentSplitter->setStretchFactor(1, 0);
    connect(m_contentSplitter, &QSplitter::splitterMoved,
            this, [this](int, int) {
                const QList<int> sizes = m_contentSplitter->sizes();
                if (sizes.size() != 2) return;
                QSettings settings("StarProcessor", "App");
                settings.setValue("ui/rightPanelWidth", sizes[1]);
            });
    workspaceLayout->addWidget(m_contentSplitter, 1);
    mainLayout->addWidget(workspace, 1);
}

QString MainWindow::sceneName() const {
    switch (m_scene) {
    case ProcessingScene::SingleFrame:
        return QCoreApplication::translate("Nebulens", "单张精修");
    case ProcessingScene::Nightscape:
        return QCoreApplication::translate("Nebulens", "银河星景");
    case ProcessingScene::DeepSky:
        return QCoreApplication::translate("Nebulens", "深空天体");
    case ProcessingScene::SkyGround:
        return QCoreApplication::translate("Nebulens", "天地分离");
    case ProcessingScene::StarTrail:
        return QCoreApplication::translate("Nebulens", "星轨合成");
    case ProcessingScene::Timelapse:
        return QCoreApplication::translate("Nebulens", "延时序列");
    }
    return QString();
}

int MainWindow::requiredFrameCount() const {
    if (m_scene == ProcessingScene::SingleFrame) return 1;
    if (m_scene == ProcessingScene::Timelapse ||
        m_scene == ProcessingScene::StarTrail) return 3;
    return 2;
}

void MainWindow::activateScene(ProcessingScene scene, bool resetParameters) {
    const bool changed = !m_sceneActive || m_scene != scene;
    m_scene = scene;
    m_sceneActive = true;

    if (changed) {
        m_paramsPanel->clearModifiedCameraGrayPoint();
        markCachedResultStale(
            QCoreApplication::translate("Nebulens", "场景已切换 · 这是上次处理的成片"));
        m_paramsPanel->applySceneProfile(scene, resetParameters);
    }
    m_projectPanel->setScene(scene);
    m_toolbar->setScene(scene);
    m_printOrder->setScene(scene);
    m_workflowRail->setScene(scene);
    for (WorkflowStage stage : {
             WorkflowStage::Alignment, WorkflowStage::Stacking,
             WorkflowStage::Adjustment, WorkflowStage::Output}) {
        m_workflowRail->setStageComplete(stage, false);
        m_workflowRail->setStageWarning(stage, false);
    }
    QSettings("StarProcessor", "App").setValue(
        "ui/processingScene", static_cast<int>(scene));
    updateProjectReadiness();
    statusBar()->showMessage(
        QCoreApplication::translate("Nebulens", "已进入%1流程").arg(sceneName()), 3000);
}

void MainWindow::updateProjectReadiness() {
    if (!m_sceneActive || m_resettingProject) return;
    const int count = m_projectPanel->includedFilePaths().size();
    const int required = requiredFrameCount();
    const bool calibrationReady = m_scene != ProcessingScene::DeepSky ||
        m_paramsPanel->deepSkyCalibrationInputsComplete();
    const auto calibrationSummary = [](const QStringList& paths,
                                       const QString& master) {
        if (!master.isEmpty()) return QStringLiteral("Master");
        if (!paths.isEmpty()) {
            return QCoreApplication::translate("Nebulens", "%1 张").arg(paths.size());
        }
        return QCoreApplication::translate("Nebulens", "未设置");
    };
    m_projectPanel->setCalibrationSummary(
        calibrationSummary(m_paramsPanel->darkFramePaths(),
                           m_paramsPanel->masterDarkPath()),
        calibrationSummary(m_paramsPanel->flatFramePaths(),
                           m_paramsPanel->masterFlatPath()),
        calibrationSummary(m_paramsPanel->biasFramePaths(),
                           m_paramsPanel->masterBiasPath()),
        calibrationSummary(m_paramsPanel->darkFlatFramePaths(),
                           m_paramsPanel->masterDarkFlatPath()),
        m_paramsPanel->deepSkyCalibrationInputsComplete());
    m_toolbar->enableProcess(count >= required && calibrationReady);
    m_startAction->setEnabled(processingActive() || (count >= required && calibrationReady));
    m_printOrder->setProject(count, m_projectPanel->totalFileCount(),
                             QFileInfo(m_projectPanel->currentFilePath()).fileName(),
                             calibrationReady, m_paramsPanel->calibrationHint());
    m_printOrder->setOutput(m_paramsPanel->outputFormat(), m_paramsPanel->outputPath());
    m_workflowRail->setStageComplete(
        WorkflowStage::Assets, count >= required);
    m_workflowRail->setStageComplete(
        WorkflowStage::Calibration,
        count >= required && calibrationReady);
    m_workflowRail->setStageWarning(
        WorkflowStage::Assets, count > 0 && count < required);
    m_workflowRail->setStageWarning(
        WorkflowStage::Calibration, count >= required && !calibrationReady);
    m_toolbar->setProjectSummary(
        count > 0
            ? QCoreApplication::translate("Nebulens", "%1 · %2 张素材").arg(sceneName()).arg(count)
            : QCoreApplication::translate("Nebulens", "%1 · 等待素材").arg(sceneName()));
    m_paramsPanel->updateRefFrameList(m_projectPanel->includedFilePaths());
    m_paramsPanel->setAssetSummary(
        m_projectPanel->totalFileCount(), count,
        QFileInfo(m_projectPanel->referenceFramePath()).fileName(),
        count >= required);

    if (count >= required && calibrationReady) {
        if ((m_scene == ProcessingScene::Nightscape ||
             m_scene == ProcessingScene::SkyGround) && count >= 2) {
            m_paramsPanel->recommendStackMethod(count);
        }
        QString status = QCoreApplication::translate("Nebulens", "%1 张照片已就绪").arg(count);
        if (m_scene == ProcessingScene::DeepSky && count < 6) {
            status += QCoreApplication::translate("Nebulens", " · 建议 6 张以上获得更稳健结果");
        }
        setWorkflowStage(
            m_scene == ProcessingScene::DeepSky ? 0 : 1,
            m_scene == ProcessingScene::DeepSky
                ? QCoreApplication::translate("Nebulens", "%1 张 Light 与校准帧已齐，可以开始处理")
                      .arg(count)
                : status);
    } else if (count < required) {
        const int remaining = required - count;
        setWorkflowStage(0,
            QCoreApplication::translate("Nebulens", "还需要 %1 张照片").arg(remaining));
    } else {
        setWorkflowStage(0, QCoreApplication::translate("Nebulens",
            "请补齐 Dark、Flat，以及 Bias 或同曝光 Dark Flat；也可导入 Master"));
    }
}

void MainWindow::setupMenuBar() {
    // 文件菜单
    auto* fileMenu = menuBar()->addMenu(
        QCoreApplication::translate("Nebulens", "文件"));
    addDocumentActions(fileMenu);
    fileMenu->addSeparator();

    auto* importAction = new QAction(
        QCoreApplication::translate("Nebulens", "导入 RAW..."), this);
    importAction->setShortcut(QKeySequence::Open);
    connect(importAction, &QAction::triggered, this, &MainWindow::onImportClicked);
    fileMenu->addAction(importAction);

    auto* importFolderAction = new QAction(
        QCoreApplication::translate("Nebulens", "导入文件夹..."), this);
    importFolderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(importFolderAction, &QAction::triggered, this, &MainWindow::onImportFolderClicked);
    fileMenu->addAction(importFolderAction);

    fileMenu->addSeparator();

    auto* clearAction = new QAction(
        QCoreApplication::translate("Nebulens", "清空项目"), this);
    connect(clearAction, &QAction::triggered, this, [this] {
        if (m_orderFilePath.isEmpty() || confirmOrderReplacement()) resetProject();
    });
    fileMenu->addAction(clearAction);

    fileMenu->addSeparator();
    auto* preferences = fileMenu->addAction(QCoreApplication::translate("Nebulens", "设置"));
    preferences->setMenuRole(QAction::PreferencesRole);
    connect(preferences, &QAction::triggered, this, &MainWindow::onSettingsClicked);

    auto* exitAction = new QAction(
        QCoreApplication::translate("Nebulens", "退出"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    fileMenu->addAction(exitAction);

    // 编辑菜单
    auto* editMenu = menuBar()->addMenu(
        QCoreApplication::translate("Nebulens", "编辑"));

    auto* removeAction = new QAction(
        QCoreApplication::translate("Nebulens", "移除所选"), this);
    removeAction->setShortcut(QKeySequence::Delete);
    connect(removeAction, &QAction::triggered, this, [this]() {
        if (!processingActive()) m_projectPanel->removeSelected();
    });
    editMenu->addAction(removeAction);

    // 视图菜单
    auto* viewMenu = menuBar()->addMenu(
        QCoreApplication::translate("Nebulens", "视图"));
    viewMenu->addAction(QCoreApplication::translate("Nebulens", "冲印单"), this,
                        &MainWindow::showPrintOrder);
    viewMenu->addAction(QCoreApplication::translate("Nebulens", "高级暗房"), this,
                        [this]() { showAdvanced(m_workflowRail->currentStage()); });
    viewMenu->addSeparator();

    auto* cycleCompareAction = new QAction(
        QCoreApplication::translate("Nebulens", "循环对比"), this);
    cycleCompareAction->setShortcut(QKeySequence(QStringLiteral("B")));
    connect(cycleCompareAction, &QAction::triggered,
            m_previewPanel, &PreviewPanel::cycleComparisonMode);
    viewMenu->addAction(cycleCompareAction);

    m_beforeAfterAction = new QAction(
        QCoreApplication::translate("Nebulens", "处理前后分屏"), this);
    m_beforeAfterAction->setShortcut(
        QKeySequence(QStringLiteral("Shift+B")));
    m_beforeAfterAction->setCheckable(true);
    m_beforeAfterAction->setEnabled(false);
    connect(m_beforeAfterAction, &QAction::toggled,
            m_previewPanel, &PreviewPanel::setBeforeAfterMode);
    viewMenu->addAction(m_beforeAfterAction);

    viewMenu->addSeparator();

    auto* fitViewAction = new QAction(
        QCoreApplication::translate("Nebulens", "适应视图"), this);
    fitViewAction->setShortcut(QKeySequence(QStringLiteral("F")));
    connect(fitViewAction, &QAction::triggered, m_previewPanel, &PreviewPanel::fitToView);
    viewMenu->addAction(fitViewAction);

    auto* actualPixelsAction = new QAction(
        QCoreApplication::translate("Nebulens", "实际像素 (1:1)"), this);
    actualPixelsAction->setShortcut(QKeySequence(QStringLiteral("1")));
    connect(actualPixelsAction, &QAction::triggered, m_previewPanel, &PreviewPanel::resetZoom);
    viewMenu->addAction(actualPixelsAction);

    auto* maskAction = new QAction(
        QCoreApplication::translate("Nebulens", "蒙版叠加"), this);
    maskAction->setShortcut(QKeySequence(QStringLiteral("M")));
    connect(maskAction, &QAction::triggered,
            m_previewPanel, &PreviewPanel::toggleMaskOverlay);
    viewMenu->addAction(maskAction);
    auto* resultAction = new QAction(
        QCoreApplication::translate("Nebulens", "返回当前结果"), this);
    resultAction->setShortcut(QKeySequence(QStringLiteral("R")));
    connect(resultAction, &QAction::triggered,
            this, &MainWindow::showBestAvailableResult);
    viewMenu->addAction(resultAction);

    // 处理菜单
    auto* processMenu = menuBar()->addMenu(
        QCoreApplication::translate("Nebulens", "处理"));

    m_startAction = new QAction(
        QCoreApplication::translate("Nebulens", "开始处理"), this);
    m_startAction->setShortcut(QKeySequence("Ctrl+Return"));
    connect(m_startAction, &QAction::triggered, this, &MainWindow::onProcessClicked);
    processMenu->addAction(m_startAction);

    m_exportAction = new QAction(
        QCoreApplication::translate("Nebulens", "导出结果"), this);
    m_exportAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::onExportClicked);
    m_exportAction->setEnabled(false);
    processMenu->addAction(m_exportAction);

    // 帮助菜单
    auto* helpMenu = menuBar()->addMenu(
        QCoreApplication::translate("Nebulens", "帮助"));

    auto* updateAction = new QAction(
        QCoreApplication::translate("Nebulens", "检查更新..."), this);
    connect(updateAction, &QAction::triggered, this, [this]() {
        m_updateManager->checkForUpdates(true);
    });
    helpMenu->addAction(updateAction);
    auto* shortcutsAction = new QAction(QCoreApplication::translate("Nebulens", "快捷键..."), this);
    connect(shortcutsAction, &QAction::triggered,
            this, &MainWindow::showShortcutsDialog);
    helpMenu->addAction(shortcutsAction);
    helpMenu->addSeparator();

    auto* aboutAction = new QAction(
        QCoreApplication::translate("Nebulens", "关于"), this);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutClicked);
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupStatusBar() {
    m_taskStatusBar = new TaskStatusBar(this);
    setStatusBar(m_taskStatusBar);
    m_undoRemoveBtn = new QPushButton(statusBar());
    m_undoRemoveBtn->setProperty(
        StyleTokens::Properties::kVariant,
        StyleTokens::Properties::kGhost);
    m_undoRemoveBtn->setAccessibleName(QCoreApplication::translate("Nebulens", "撤销移除素材"));
    m_undoRemoveBtn->hide();
    statusBar()->addWidget(m_undoRemoveBtn);
    connect(m_undoRemoveBtn, &QPushButton::clicked, this, [this]() {
        m_projectPanel->undoLastRemoval();
    });
    m_pixelInfoLabel = new QLabel(QCoreApplication::translate("Nebulens", "RGB —"), statusBar());
    m_pixelInfoLabel->setProperty(
        StyleTokens::Properties::kTextRole,
        QString::fromLatin1(StyleTokens::Properties::kMono));
    m_pixelInfoLabel->setMinimumWidth(190);
    m_pixelInfoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(m_pixelInfoLabel);

    m_inlineProgress = m_taskStatusBar->progressBar();
}

void MainWindow::setWorkflowStage(int stage, const QString& status, bool complete) {
    const int normalizedStage = std::clamp(stage, 0, 3);
    const bool activeFormalTask =
        m_worker && m_worker->isRunning();
    if ((activeFormalTask || complete) && normalizedStage >= 1) {
        m_workflowRail->setStageComplete(
            WorkflowStage::Calibration, true);
    }
    if ((activeFormalTask || complete) && normalizedStage >= 2) {
        m_workflowRail->setStageComplete(
            WorkflowStage::Alignment, true);
    }
    if ((activeFormalTask || complete) && normalizedStage >= 3) {
        m_workflowRail->setStageComplete(
            WorkflowStage::Stacking, true);
    }
    if (complete) {
        m_workflowRail->setStageComplete(
            WorkflowStage::Adjustment, true);
        m_workflowRail->setStageComplete(
            WorkflowStage::Output, true);
    }
    statusBar()->showMessage(status, complete ? 5000 : 0);
}

void MainWindow::setupConnections() {
    m_quickPreviewTimer = new QTimer(this);
    m_quickPreviewTimer->setSingleShot(true);
    m_quickPreviewTimer->setInterval(StyleTokens::Motion::kPreviewDebounceMs);
    connect(m_quickPreviewTimer, &QTimer::timeout,
            this, &MainWindow::startQuickPreview);

    // Toolbar 信号
    connect(m_toolbar, &Toolbar::importFilesClicked, this, &MainWindow::onImportClicked);
    connect(m_toolbar, &Toolbar::importFolderClicked, this, &MainWindow::onImportFolderClicked);
    connect(m_toolbar, &Toolbar::clearProjectClicked, this, [this] {
        if (m_orderFilePath.isEmpty() || confirmOrderReplacement()) resetProject();
    });
    connect(m_toolbar, &Toolbar::startProcessClicked, this, &MainWindow::onProcessClicked);
    connect(m_toolbar, &Toolbar::exportResultClicked, this, &MainWindow::onExportClicked);
    connect(m_toolbar, &Toolbar::settingsClicked, this, [this]() {
        onSettingsClicked();
    });
    connect(m_toolbar, &Toolbar::checkUpdatesClicked, this, [this]() {
        m_updateManager->checkForUpdates(true);
    });
    connect(m_toolbar, &Toolbar::shortcutsClicked,
            this, &MainWindow::showShortcutsDialog);
    connect(m_toolbar, &Toolbar::aboutClicked, this, &MainWindow::onAboutClicked);
    connect(m_toolbar, &Toolbar::sceneChanged,
            this, [this](ProcessingScene scene) {
                m_automaticStill = false;
                activateScene(scene, true);
            });
    connect(m_workflowRail, &WorkflowRail::stageChanged,
            m_paramsPanel, &ParamsPanel::showWorkflowStage);
    connect(m_projectPanel, &ProjectPanel::calibrationSettingsRequested,
            m_paramsPanel, &ParamsPanel::showCalibrationSettings);
    connect(m_projectPanel, &ProjectPanel::undoAvailabilityChanged,
            this, [this](bool available, const QString& fileName) {
                if (!m_undoRemoveBtn) return;
                m_undoRemoveBtn->setText(
                    available
                        ? QCoreApplication::translate("Nebulens", "撤销移除 · %1").arg(fileName)
                        : QString());
                m_undoRemoveBtn->setVisible(available);
            });
    connect(m_paramsPanel, &ParamsPanel::recentResultRequested,
            this, &MainWindow::loadRecentResultPreview);
    connect(m_paramsPanel, &ParamsPanel::revealResultRequested,
            this, [](const QString& path) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(
                    QFileInfo(path).absolutePath()));
            });

    // 文件选择 -> 预览加载
    connect(m_projectPanel, &ProjectPanel::fileSelected, this, [this](const QString& filePath) {
        m_sourceProof = {};
        m_previewPanel->loadImage(filePath);
        m_previewPanel->setResultAvailable(
            !m_cachedStackedData.empty() ||
                !m_lastValidResultPreview.isNull(),
            false);
        if (m_scene == ProcessingScene::SingleFrame) {
            handleProcessingParametersChanged();
        } else {
            refreshPrintOrderResult();
        }
        statusBar()->showMessage(
            QCoreApplication::translate("Nebulens", "正在加载：%1").arg(QFileInfo(filePath).fileName()));
    });
    connect(m_previewPanel, &PreviewPanel::sourcePreviewReady, this,
            [this](const QString& filePath, const QImage& image,
                   int iso, double exposureTime, double aperture,
                   int focalLength) {
                m_projectPanel->applyPreviewData(
                    filePath, image, iso, exposureTime, aperture,
                    focalLength);
                if (m_projectPanel->currentFilePath() == filePath) {
                    m_sourceProof = image;
                    refreshPrintOrderResult();
                    if (!m_editedSkyGroundMask.empty() &&
                        m_editedMaskSourceFile == filePath &&
                        m_editedMaskProjectFiles ==
                            m_projectPanel->filePaths()) {
                        m_maskOverlaySourceFile = filePath;
                        m_previewPanel->restoreEditedMask(
                            m_editedSkyGroundMask,
                            m_editedSkyGroundMaskWidth,
                            m_editedSkyGroundMaskHeight);
                    }
                    m_pixelInfoLabel->setText(
                        QCoreApplication::translate("Nebulens", "%1×%2 · RGB —")
                            .arg(image.width()).arg(image.height()));
                    statusBar()->showMessage(
                        QCoreApplication::translate("Nebulens", "已加载：%1")
                            .arg(QFileInfo(filePath).fileName()),
                        3000);
                }
            });
    connect(m_previewPanel, &PreviewPanel::mousePixelInfo,
            this, [this](int x, int y, int r, int g, int b) {
                m_pixelInfoLabel->setText(
                    QCoreApplication::translate("Nebulens", "%1,%2 · RGB %3,%4,%5")
                        .arg(x).arg(y).arg(r).arg(g).arg(b));
            });
    connect(m_previewPanel, &PreviewPanel::sourcePreviewFailed, this,
            [this](const QString& filePath) {
                m_projectPanel->requestThumbnail(filePath);
                if (m_projectPanel->currentFilePath() == filePath) {
                    statusBar()->showMessage(
                        QCoreApplication::translate("Nebulens", "预览加载失败：%1")
                            .arg(QFileInfo(filePath).fileName()),
                        5000);
                }
            });

    // 元数据请求
    connect(m_projectPanel, &ProjectPanel::requestMetadata, this, [this](const QString& filePath) {
        onViewMetadata(filePath);
    });

    // 预览区空状态导入按钮
    connect(m_previewPanel, &PreviewPanel::importRequested, this, &MainWindow::onImportClicked);
    connect(m_previewPanel, &PreviewPanel::resultRequested, this, [this]() {
        showBestAvailableResult();
    });
    connect(m_paramsPanel, &ParamsPanel::modifiedCameraGrayPointRequested,
            this, [this]() {
                if (!m_quickPreviewSource || m_quickPreviewSource->empty()) {
                    m_paramsPanel->clearModifiedCameraGrayPoint();
                    QMessageBox::information(
                        this, QCoreApplication::translate("Nebulens", "手动灰点"),
                        QCoreApplication::translate("Nebulens",
                            "请先完成一次正式处理。之后可在结果预览中采样，"
                            "并立即通过快速预览检查色彩。"));
                    return;
                }
                cancelQuickPreview(false);
                showBestAvailableResult();
                m_previewPanel->setPointSelectionActive(true);
                statusBar()->showMessage(
                    QCoreApplication::translate("Nebulens", "请在结果中点击应为灰色的天空区域"));
            });
    connect(m_previewPanel, &PreviewPanel::imagePointSelected,
            this, [this](double normalizedX, double normalizedY) {
                m_paramsPanel->setModifiedCameraGrayPoint(
                    normalizedX, normalizedY);
                m_previewPanel->setSelectedPoint(
                    normalizedX, normalizedY);
                statusBar()->showMessage(
                    QCoreApplication::translate("Nebulens", "灰点已采样，正在更新快速预览"), 3000);
            });

    // 项目面板拖放导入
    connect(m_projectPanel, &ProjectPanel::filesDropped, this, [this](const QStringList& paths) {
        if (!paths.isEmpty()) {
            statusBar()->showMessage(
                QCoreApplication::translate("Nebulens", "拖放导入 %1 个文件").arg(paths.size()),
                5000
            );
        } else {
            onImportClicked();
        }
    });

    // 文件变化 -> 更新按钮状态 & 参考帧列表 & 智能推荐堆栈算法
    connect(m_projectPanel, &ProjectPanel::filesChanged, this, [this]() {
        if (m_resettingProject) return;
        adaptStillScene();
        if (m_projectPanel->currentFilePath().isEmpty()) m_sourceProof = {};
        if (!m_editedSkyGroundMask.empty() &&
            m_editedMaskProjectFiles != m_projectPanel->filePaths()) {
            m_editedSkyGroundMask.clear();
            m_editedSkyGroundMaskWidth = 0;
            m_editedSkyGroundMaskHeight = 0;
            m_editedMaskProjectFiles.clear();
            m_editedMaskSourceFile.clear();
            ++m_editedMaskRevision;
            m_previewPanel->clearMaskOverlay();
        }
        m_paramsPanel->clearModifiedCameraGrayPoint();
        markCachedResultStale(
            QCoreApplication::translate("Nebulens", "素材已修改 · 这是上次处理的成片"));
        m_workflowRail->setStageComplete(WorkflowStage::Alignment, false);
        m_workflowRail->setStageComplete(WorkflowStage::Stacking, false);
        m_workflowRail->setStageComplete(WorkflowStage::Adjustment, false);
        m_workflowRail->setStageComplete(WorkflowStage::Output, false);
        updateProjectReadiness();
        refreshExportAction();
    });

    // 参考帧变化
    connect(m_projectPanel, &ProjectPanel::referenceFrameChanged, this, [this]() {
        const QString projectReference =
            m_projectPanel->referenceFramePath();
        const bool referenceLeavesEditedMask =
            !m_editedSkyGroundMask.empty() &&
            !projectReference.isEmpty() &&
            projectReference != m_editedMaskSourceFile;
        // Keep the right-panel selector and context-menu reference as one
        // user-visible setting. ParamsPanel emits its normal change signal,
        // which also clears an edited mask tied to a different frame.
        m_paramsPanel->setSelectedReferenceFrame(projectReference);
        const bool discarded = referenceLeavesEditedMask ||
            discardEditedMaskForDifferentReference();
        statusBar()->showMessage(
            discarded
                ? QCoreApplication::translate("Nebulens",
                      "参考帧已更新，请为新参考帧重新检测和修补地景")
                : QCoreApplication::translate("Nebulens", "参考帧已更新"),
            discarded ? 5000 : 2000);
        handleProcessingParametersChanged();
    });

    connect(m_previewPanel, &PreviewPanel::comparisonAvailabilityChanged,
            this, [this](bool available) {
                if (!m_beforeAfterAction) return;
                m_beforeAfterAction->setEnabled(available);
                if (!available) m_beforeAfterAction->setChecked(false);
            });
    connect(m_previewPanel, &PreviewPanel::beforeAfterModeChanged,
            this, [this](bool enabled) {
                if (!m_beforeAfterAction) return;
                const QSignalBlocker blocker(m_beforeAfterAction);
                m_beforeAfterAction->setChecked(enabled);
            });
    connect(m_previewPanel, &PreviewPanel::editedMaskChanged,
            this, [this]() {
                if (m_previewPanel->hasEditedMask()) {
                    m_editedSkyGroundMask = m_previewPanel->editedMask();
                    m_editedSkyGroundMaskWidth =
                        m_previewPanel->editedMaskWidth();
                    m_editedSkyGroundMaskHeight =
                        m_previewPanel->editedMaskHeight();
                    m_editedMaskProjectFiles =
                        m_projectPanel->filePaths();
                    m_editedMaskSourceFile = m_maskOverlaySourceFile;
                    if (!m_editedMaskSourceFile.isEmpty()) {
                        m_paramsPanel->setSelectedReferenceFrame(
                            m_editedMaskSourceFile);
                        if (m_projectPanel->referenceFramePath() !=
                            m_editedMaskSourceFile) {
                            m_projectPanel->setReferenceFrame(
                                m_editedMaskSourceFile);
                        }
                        statusBar()->showMessage(
                            QCoreApplication::translate("Nebulens",
                                "地景蒙版已绑定参考帧：%1")
                                .arg(QFileInfo(m_editedMaskSourceFile)
                                         .fileName()),
                            4000);
                    }
                } else {
                    m_editedSkyGroundMask.clear();
                    m_editedSkyGroundMaskWidth = 0;
                    m_editedSkyGroundMaskHeight = 0;
                    m_editedMaskProjectFiles.clear();
                    m_editedMaskSourceFile.clear();
                }
                ++m_editedMaskRevision;
                handleProcessingParametersChanged();
            });
    connect(m_previewPanel, &PreviewPanel::maskRefinementStarted,
            this, [this]() {
                statusBar()->showMessage(
                    QCoreApplication::translate("Nebulens", "正在根据笔迹贴合地景边缘..."));
            });
    connect(m_previewPanel, &PreviewPanel::maskRefinementFinished,
            this, [this](bool success) {
                statusBar()->showMessage(
                    success
                        ? QCoreApplication::translate("Nebulens", "蒙版已贴合边缘，可继续修补或开始处理")
                        : QCoreApplication::translate("Nebulens", "蒙版精修失败，已保留上一次结果"),
                    4000);
            });

    // 参数变化
    connect(m_paramsPanel, &ParamsPanel::paramsChanged, this, [this]() {
        discardEditedMaskForDifferentReference();
        handleProcessingParametersChanged();
    });

    // 天地分离蒙版预览请求 -> 使用 MaskPreviewWorker
    connect(m_paramsPanel, &ParamsPanel::maskPreviewRequested, this, [this]() {
        QString currentFile = m_projectPanel->currentFilePath();
        if (currentFile.isEmpty()) {
            QMessageBox::information(
                this, QCoreApplication::translate("Nebulens", "提示"),
                QCoreApplication::translate(
                    "Nebulens", "请先选择一张图像用于检测"));
            return;
        }

        // 取消之前的预览任务，等待其真正结束
        if (m_maskPreviewWorker && m_maskPreviewWorker->isRunning()) {
            m_maskPreviewWorker->requestInterruption();
            if (!m_maskPreviewWorker->wait(3000)) {
                // 超时：拒绝启动新任务，避免旧 worker 失去清理路径
                statusBar()->showMessage(QCoreApplication::translate(
                    "Nebulens", "上一个检测任务仍在运行，请稍后再试"),
                    3000);
                return;
            }
        }

        int feather = m_paramsPanel->featherRadius();
        const QString maskSourceFile = currentFile;
        MaskPreviewWorker* worker = new MaskPreviewWorker(currentFile, feather, this);
        m_maskPreviewWorker = worker;
        m_activeMaskPreviewWorkers.insert(worker);
        connect(worker, &MaskPreviewWorker::finished, this,
                [this, worker, maskSourceFile]() {
            m_activeMaskPreviewWorkers.remove(worker);
            if (worker->errorString().isEmpty()) {
                // 只处理最新 worker 的结果
                if (worker == m_maskPreviewWorker &&
                    m_projectPanel->currentFilePath() == maskSourceFile) {
                    m_maskOverlaySourceFile = maskSourceFile;
                    m_previewPanel->setMaskOverlay(worker->takeMask(),
                                                    worker->width(),
                                                    worker->height());
                    statusBar()->showMessage(QCoreApplication::translate(
                        "Nebulens", "地景检测完成，蓝色=天空，绿色=地景"),
                        5000);
                }
            } else if (worker == m_maskPreviewWorker) {
                QMessageBox::warning(
                    this, QCoreApplication::translate("Nebulens", "错误"),
                    worker->errorString());
            }
            worker->deleteLater();
            if (m_maskPreviewWorker == worker) {
                m_maskPreviewWorker = nullptr;
            }
        });
        worker->start();
    });
}

QString MainWindow::formatExposureTime(double seconds) {
    if (seconds >= 1.0) {
        return QString("%1s").arg(seconds, 0, 'f', seconds >= 10.0 ? 0 : 1);
    }
    if (seconds > 0.0) {
        return QString("1/%1s").arg(qRound(1.0 / seconds));
    }
    return QCoreApplication::translate("Nebulens", "—");
}

bool MainWindow::processingActive() const {
    // Qt 线程可能已结束，但 UI 的 finished 回调仍在队列中，此时仍须锁住项目。
    return m_worker || m_exportWorker;
}

QString MainWindow::effectiveReferenceFrame() const {
    const QStringList files = m_projectPanel->includedFilePaths();
    const QString reference = m_projectPanel->referenceFramePath();
    return files.contains(reference) ? reference : QString();
}

QString MainWindow::effectiveSingleFrameSource() const {
    const QStringList files = m_projectPanel->includedFilePaths();
    if (files.isEmpty()) return QString();
    QString source = m_projectPanel->currentFilePath();
    if (source.isEmpty() || !files.contains(source)) source = files.first();
    const QFileInfo info(source);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool MainWindow::discardEditedMaskForDifferentReference() {
    if (m_editedSkyGroundMask.empty()) return false;
    const QString reference = effectiveReferenceFrame();
    if (reference.isEmpty() || reference == m_editedMaskSourceFile) {
        return false;
    }
    m_editedSkyGroundMask.clear();
    m_editedSkyGroundMaskWidth = 0;
    m_editedSkyGroundMaskHeight = 0;
    m_editedMaskProjectFiles.clear();
    m_editedMaskSourceFile.clear();
    ++m_editedMaskRevision;
    m_previewPanel->clearMaskOverlay();
    return true;
}

QString MainWindow::currentUpstreamSignature() const {
    // ParamsPanel owns the algorithm settings, while ProjectPanel owns the
    // context-menu reference choice. Sign the effective value used by the
    // worker so cache/export validity follows both controls.
    return m_paramsPanel->upstreamSignature() +
        QStringLiteral("|scene=") +
        QString::number(static_cast<int>(m_scene)) +
        QStringLiteral("|processing-sources=") +
        m_projectPanel->processingSourceSignature() +
        QStringLiteral("|single-source=") +
        (m_scene == ProcessingScene::SingleFrame
             ? effectiveSingleFrameSource() : QString()) +
        QStringLiteral("|effective-reference=") + effectiveReferenceFrame() +
        QStringLiteral("|edited-mask-source=") + m_editedMaskSourceFile +
        QStringLiteral("|edited-mask-revision=") +
        QString::number(m_editedMaskRevision);
}

QString MainWindow::currentProcessingSignature() const {
    return currentUpstreamSignature() + QStringLiteral("||") +
        m_paramsPanel->finishingSignature();
}

void MainWindow::handleProcessingParametersChanged() {
    if (m_resettingProject) return;
    updateProjectReadiness();
    refreshPrintOrderResult();
    refreshExportAction();
    if (m_cachedStackedData.empty() || processingActive()) return;
    if (m_paramsPanel->hasModifiedCameraGrayPoint() &&
        !m_lastProcessedUpstreamSignature.isEmpty() &&
        currentUpstreamSignature() !=
            m_lastProcessedUpstreamSignature) {
        // A manual point is tied to the crop and geometry of the cached
        // pre-finishing image. Any upstream change requires a new sample.
        m_paramsPanel->clearModifiedCameraGrayPoint();
        m_previewPanel->clearSelectedPoint();
    }
    const bool current =
        currentProcessingSignature() == m_lastProcessedSignature;
    const bool upstreamCurrent =
        currentUpstreamSignature() == m_lastProcessedUpstreamSignature;
    if (!upstreamCurrent) {
        m_workflowRail->setStageComplete(WorkflowStage::Alignment, false);
        m_workflowRail->setStageComplete(WorkflowStage::Stacking, false);
        m_workflowRail->setStageComplete(WorkflowStage::Adjustment, false);
    } else if (!current) {
        m_workflowRail->setStageComplete(WorkflowStage::Adjustment, false);
    }
    m_workflowRail->setStageComplete(WorkflowStage::Output, current);
    if (current) {
        cancelQuickPreview(false);
        setWorkflowStage(3,
            QCoreApplication::translate("Nebulens", "当前结果 · %1 帧 · %2×%3")
                .arg(m_cachedFrameCount).arg(m_cachedWidth).arg(m_cachedHeight),
            true);
        if (m_previewPanel->isShowingResult()) {
            showBestAvailableResult();
        }
    } else if (quickPreviewEligible()) {
        if (currentQuickPreviewAvailable()) {
            if (m_previewPanel->isShowingResult()) showBestAvailableResult();
            setWorkflowStage(3, QCoreApplication::translate("Nebulens",
                "快速预览已更新 · 完整导出需重新处理"));
        } else {
            if (m_previewPanel->isShowingResult()) {
                m_previewPanel->setResultLabel(
                    QCoreApplication::translate("Nebulens", "参数已修改 · 这是上次处理的成片"));
            }
            scheduleQuickPreview();
        }
    } else {
        cancelQuickPreview(false);
        if (m_previewPanel->isShowingResult()) {
            m_previewPanel->setResultLabel(
                QCoreApplication::translate("Nebulens", "参数已修改 · 这是上次处理的成片"));
        }
        setWorkflowStage(1,
            QCoreApplication::translate("Nebulens", "参数已修改，重新处理后生效"));
    }
}

FinishingOptions MainWindow::currentFinishingOptions() const {
    FinishingOptions options;
    options.noiseReductionEnabled =
        m_paramsPanel->noiseReductionEnabled();
    options.noiseReductionStrength =
        m_paramsPanel->noiseReductionStrength();
    options.modifiedCameraColorEnabled =
        m_paramsPanel->modifiedCameraColorEnabled();
    options.modifiedCameraColor.strength =
        m_paramsPanel->modifiedCameraColorStrength();
    options.modifiedCameraColor.neutralMode =
        m_paramsPanel->modifiedCameraColorMode();
    options.modifiedCameraColor.manualPointX =
        m_paramsPanel->modifiedCameraGrayPointX();
    options.modifiedCameraColor.manualPointY =
        m_paramsPanel->modifiedCameraGrayPointY();
    options.dehazeEnabled = m_paramsPanel->dewarpEnabled();
    options.dehazeStrength = m_paramsPanel->dewarpStrength();
    options.stretchEnabled = m_paramsPanel->stretchEnabled();
    options.basicAdjustments =
        m_paramsPanel->basicAdjustmentOptions();
    const bool separateGround = (m_scene == ProcessingScene::SkyGround ||
                                 m_scene == ProcessingScene::Nightscape) &&
                                m_paramsPanel->skyGroundSeparationEnabled();
    options.skyGroundSeparation = separateGround ||
        (m_scene == ProcessingScene::StarTrail &&
         m_paramsPanel->starTrailProtectGround());
    options.groundDetailStrength = separateGround ? m_paramsPanel->groundDetailStrength() : 0;
    options.starDefringeEnabled =
        m_scene != ProcessingScene::StarTrail && m_paramsPanel->starDefringeEnabled();
    options.starDefringeStrength =
        m_paramsPanel->starDefringeStrength();
    options.starReductionEnabled =
        m_scene != ProcessingScene::StarTrail && m_paramsPanel->starReduceEnabled();
    options.starReductionStrength =
        m_paramsPanel->starReduceStrength();
    return options;
}

bool MainWindow::quickPreviewEligible() const {
    return m_scene != ProcessingScene::Timelapse &&
        m_quickPreviewSource && !m_quickPreviewSource->empty() &&
        currentUpstreamSignature() == m_lastProcessedUpstreamSignature &&
        m_lastProcessedUpstreamSignature + QStringLiteral("||") +
            m_paramsPanel->finishingSignature() != m_lastProcessedSignature;
}

bool MainWindow::currentQuickPreviewAvailable() const {
    return quickPreviewEligible() &&
        !m_cachedQuickPreviewResult.empty() &&
        m_quickPreviewSignature == m_paramsPanel->finishingSignature();
}

void MainWindow::showBestAvailableResult() {
    // 像素有效性跟随调整参数；同一项目、同一几何的观察位置应跨调整保留。
    const QString contentKey = QStringLiteral("result:%1:").arg(m_orderGeneration) +
                               m_lastProcessedUpstreamSignature;
    if (currentQuickPreviewAvailable()) {
        if (!m_cachedBeforePreview.isNull()) {
            m_previewPanel->loadRgb16BitComparison(
                m_cachedBeforePreview, m_cachedQuickPreviewResult,
                m_quickPreviewWidth, m_quickPreviewHeight,
                m_cachedBeforeBlackPoint, m_cachedBeforeWhitePoint,
                contentKey);
        } else {
            m_previewPanel->loadRgb16BitImage(
                m_cachedQuickPreviewResult,
                m_quickPreviewWidth, m_quickPreviewHeight,
                contentKey);
        }
        m_previewPanel->setResultLabel(QCoreApplication::translate("Nebulens",
            "快速预览 %1×%2 · 完整导出需重新处理")
            .arg(m_quickPreviewWidth).arg(m_quickPreviewHeight));
        if (m_paramsPanel->modifiedCameraColorEnabled() &&
            m_paramsPanel->modifiedCameraColorMode() ==
                ModifiedCameraNeutralMode::ManualPoint &&
            m_paramsPanel->hasModifiedCameraGrayPoint()) {
            m_previewPanel->setSelectedPoint(
                m_paramsPanel->modifiedCameraGrayPointX(),
                m_paramsPanel->modifiedCameraGrayPointY());
        } else {
            m_previewPanel->clearSelectedPoint();
        }
        m_previewPanel->setResultAvailable(true, true);
        return;
    }
    if (m_cachedStackedData.empty()) {
        if (!m_lastValidResultPreview.isNull()) {
            m_previewPanel->loadReadOnlyResultPreview(
                m_lastValidResultPreview,
                m_lastValidResultPreviewKey,
                QCoreApplication::translate("Nebulens", "上次有效结果 · 只读"));
            m_previewPanel->setResultAvailable(true, true);
            m_toolbar->enableExport(false);
        }
        return;
    }
    if (!m_cachedBeforePreview.isNull()) {
        m_previewPanel->loadRgb16BitComparison(
            m_cachedBeforePreview, m_cachedStackedData,
            m_cachedWidth, m_cachedHeight,
            m_cachedBeforeBlackPoint, m_cachedBeforeWhitePoint,
            contentKey);
    } else {
        m_previewPanel->loadRgb16BitImage(
            m_cachedStackedData, m_cachedWidth, m_cachedHeight,
            contentKey);
    }
    if (currentProcessingSignature() == m_lastProcessedSignature) {
        m_previewPanel->setResultLabel(QString());
    } else {
        m_previewPanel->setResultLabel(
            QCoreApplication::translate("Nebulens", "参数已修改 · 这是上次处理的成片"));
    }
    if (m_paramsPanel->modifiedCameraColorEnabled() &&
        m_paramsPanel->modifiedCameraColorMode() ==
            ModifiedCameraNeutralMode::ManualPoint &&
        m_paramsPanel->hasModifiedCameraGrayPoint()) {
        m_previewPanel->setSelectedPoint(
            m_paramsPanel->modifiedCameraGrayPointX(),
            m_paramsPanel->modifiedCameraGrayPointY());
    } else {
        m_previewPanel->clearSelectedPoint();
    }
    m_previewPanel->setResultAvailable(true, true);
    refreshExportAction();
}

void MainWindow::cancelQuickPreview(bool waitForDone) {
    if (m_quickPreviewTimer) m_quickPreviewTimer->stop();
    m_quickPreviewPending = false;
    ++m_quickPreviewGeneration;
    if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
        m_quickPreviewWorker->requestCancel();
        if (waitForDone) m_quickPreviewWorker->wait();
    }
}

void MainWindow::scheduleQuickPreview() {
    if (!quickPreviewEligible() || processingActive()) return;
    ++m_quickPreviewGeneration;
    if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
        m_quickPreviewWorker->requestCancel();
    }
    if (m_quickPreviewTimer) m_quickPreviewTimer->start();
    setWorkflowStage(3, QCoreApplication::translate("Nebulens", "参数已修改 · 准备快速预览"));
}

void MainWindow::startQuickPreview() {
    if (!quickPreviewEligible() || processingActive()) return;
    if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
        m_quickPreviewPending = true;
        m_quickPreviewWorker->requestCancel();
        return;
    }

    m_quickPreviewPending = false;
    const uint64_t generation = m_quickPreviewGeneration;
    const QString finishingSignature =
        m_paramsPanel->finishingSignature();
    auto* worker = new QuickPreviewWorker(
        m_quickPreviewSource,
        m_quickPreviewWidth, m_quickPreviewHeight,
        m_quickPreviewMask,
        currentFinishingOptions(), generation, this);
    m_quickPreviewWorker = worker;
    connect(worker, &QuickPreviewWorker::stageMessage, this,
            [this, generation](const QString& message) {
                if (generation != m_quickPreviewGeneration) return;
                setWorkflowStage(3, message);
            });
    connect(worker, &QuickPreviewWorker::finished, this,
            [this, worker, generation, finishingSignature]() {
                const bool isLatest =
                    !processingActive() && generation == m_quickPreviewGeneration &&
                    finishingSignature ==
                        m_paramsPanel->finishingSignature() &&
                    quickPreviewEligible();
                if (isLatest && !worker->wasCancelled() &&
                    worker->errorString().isEmpty()) {
                    std::vector<uint16_t> result = worker->takeResult();
                    if (!result.empty()) {
                        m_quickProof = ImageExporter::makeRgb16Proof(
                            result, m_quickPreviewWidth, m_quickPreviewHeight);
                        m_cachedQuickPreviewResult = std::move(result);
                        m_quickPreviewSignature = finishingSignature;
                        refreshPrintOrderResult();
                        showBestAvailableResult();
                        m_toolbar->enableExport(false);
                        setWorkflowStage(3, QCoreApplication::translate("Nebulens",
                            "快速预览已更新 · 完整导出需重新处理"));
                        const StarReductionStats& starStats =
                            worker->starReductionStats();
                        if (m_paramsPanel->starReduceEnabled()) {
                            statusBar()->showMessage(
                                QCoreApplication::translate("Nebulens",
                                    "快速预览已更新 · 缩星处理 %1 颗，"
                                    "请在 100% 下检查")
                                    .arg(starStats.processedStars),
                                4000);
                        } else if (m_paramsPanel->starDefringeEnabled()) {
                            statusBar()->showMessage(
                                QCoreApplication::translate("Nebulens",
                                    "快速预览已更新 · 去紫边修正 %1 个星缘像素")
                                    .arg(worker->starDefringeStats()
                                             .defringedPixels),
                                4000);
                        } else {
                            statusBar()->showMessage(
                                QCoreApplication::translate("Nebulens", "快速预览已更新"), 2500);
                        }
                    }
                } else if (isLatest && !worker->wasCancelled()) {
                    statusBar()->showMessage(
                        QCoreApplication::translate("Nebulens", "快速预览失败：%1")
                            .arg(worker->errorString()),
                        4000);
                }
                if (m_quickPreviewWorker == worker) {
                    m_quickPreviewWorker = nullptr;
                }
                worker->deleteLater();
                if (m_quickPreviewPending && quickPreviewEligible()) {
                    m_quickPreviewPending = false;
                    m_quickPreviewTimer->start(0);
                }
            });
    worker->start();
}

void MainWindow::invalidateCachedResult() {
    cancelQuickPreview(false);
    m_cachedStackedData.clear();
    m_cachedStackedData.shrink_to_fit();
    m_cachedBeforePreview = QImage();
    m_formalProof = {};
    m_quickProof = {};
    m_cachedBeforeBlackPoint = 0;
    m_cachedBeforeWhitePoint = 65535;
    m_cachedWidth = 0;
    m_cachedHeight = 0;
    m_cachedFrameCount = 0;
    m_quickPreviewSource.reset();
    m_quickPreviewMask.reset();
    m_cachedQuickPreviewResult.clear();
    m_cachedQuickPreviewResult.shrink_to_fit();
    m_quickPreviewWidth = 0;
    m_quickPreviewHeight = 0;
    m_quickPreviewSignature.clear();
    m_lastProcessedSignature.clear();
    m_lastProcessedUpstreamSignature.clear();
    if (m_toolbar) m_toolbar->enableExport(false);
    if (m_previewPanel) {
        m_previewPanel->setResultAvailable(false);
        m_previewPanel->clearComparison();
    }
}

void MainWindow::markCachedResultStale(const QString& reason) {
    m_deliveryPath.clear();
    m_printOrder->clearReceipt();
    cancelQuickPreview(false);
    m_quickPreviewSource.reset();
    m_quickPreviewMask.reset();
    m_quickProof = {};
    m_cachedQuickPreviewResult.clear();
    m_cachedQuickPreviewResult.shrink_to_fit();
    m_quickPreviewWidth = 0;
    m_quickPreviewHeight = 0;
    m_quickPreviewSignature.clear();
    refreshPrintOrderResult();
    refreshExportAction();
    if (!m_previewPanel || m_cachedStackedData.empty()) return;
    const bool viewingResult = m_previewPanel->isShowingResult();
    m_previewPanel->setResultAvailable(true, viewingResult);
    if (viewingResult) m_previewPanel->setResultLabel(reason);
}

void MainWindow::restoreLastValidResultAfterFailedRun() {
    if (m_lastValidResultPreview.isNull()) return;
    if (m_resultWasVisibleBeforeRun) {
        m_previewPanel->loadReadOnlyResultPreview(
            m_lastValidResultPreview,
            m_lastValidResultPreviewKey,
            QCoreApplication::translate("Nebulens", "上次有效结果 · 本次处理未完成"));
        m_previewPanel->setResultAvailable(true, true);
    } else {
        m_previewPanel->setResultAvailable(true, false);
    }
    m_toolbar->enableExport(false);
}

void MainWindow::onImportClicked() {
    if (processingActive()) {
        statusBar()->showMessage(QCoreApplication::translate("Nebulens", "处理完成后再添加照片"), 3000);
        return;
    }
    QStringList fileNames = FileDialogUtils::getOpenFileNames(
        this,
        QCoreApplication::translate("Nebulens", "选择 RAW 文件"),
        QString(),
        QCoreApplication::translate(
            "Nebulens",
            "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef *.cr3);;所有文件 (*)"));

    if (!fileNames.isEmpty()) {
        importFiles(fileNames);
        statusBar()->showMessage(
            QCoreApplication::translate("Nebulens", "已导入 %1 个文件").arg(fileNames.size()),
            5000
        );
    }
}

void MainWindow::onImportFolderClicked() {
    if (processingActive()) {
        statusBar()->showMessage(QCoreApplication::translate("Nebulens", "处理完成后再添加文件夹"), 3000);
        return;
    }
    QString dir = FileDialogUtils::getExistingDirectory(
        this,
        QCoreApplication::translate(
            "Nebulens", "选择包含 RAW 文件的文件夹"),
        QString());
    if (!dir.isEmpty()) {
        QDir directory(dir);
        QStringList filters;
        filters << "*.nef" << "*.cr2" << "*.arw" << "*.dng" << "*.raw" << "*.orf" << "*.raf" << "*.pef" << "*.cr3";
        filters << "*.NEF" << "*.CR2" << "*.ARW" << "*.DNG" << "*.RAW" << "*.ORF" << "*.RAF" << "*.PEF" << "*.CR3";
        directory.setNameFilters(filters);
        QStringList files = directory.entryList(QDir::Files);
        QStringList fullPaths;
        for (const QString& f : files) {
            fullPaths.append(directory.absoluteFilePath(f));
        }
        if (!fullPaths.isEmpty()) {
            importFiles(fullPaths);
            statusBar()->showMessage(
                QCoreApplication::translate("Nebulens", "从文件夹导入 %1 个文件").arg(fullPaths.size()),
                5000
            );
        }
    }
}

void MainWindow::onViewMetadata(const QString& filePath) {
    RawImageLoader loader;
    RawImageLoader::Metadata metadata;
    if (!loader.loadMetadata(filePath, metadata)) {
        QMessageBox::warning(
            this, QCoreApplication::translate("Nebulens", "元数据"),
            QCoreApplication::translate("Nebulens", "无法加载文件元数据"));
        return;
    }
    QString info = QCoreApplication::translate("Nebulens", "<b>文件路径：</b>%1<br>"
        "<b>相机型号：</b>%2<br>"
        "<b>ISO：</b>%3<br>"
        "<b>曝光时间：</b>%4<br>"
        "<b>光圈：</b>%5<br>"
        "<b>焦距：</b>%6 mm<br>"
        "<b>尺寸：</b>%7×%8<br>"
        "<b>时间戳：</b>%9"
    ).arg(filePath)
     .arg(QString::fromStdString(metadata.cameraModel))
     .arg(metadata.iso)
     .arg(formatExposureTime(metadata.exposureTime))
     .arg(metadata.aperture > 0 ? QString("f/%1").arg(metadata.aperture, 0, 'f', 1) : "—")
     .arg(metadata.focalLength)
     .arg(metadata.width)
     .arg(metadata.height)
     .arg(QString::fromStdString(metadata.timestamp));
    QMessageBox::information(
        this, QCoreApplication::translate("Nebulens", "图像元数据"), info);
}

void MainWindow::onAboutClicked() {
    QMessageBox::about(
        this,
        QCoreApplication::translate("Nebulens", "关于 Nebulens"),
        QStringLiteral("Nebulens %1\n%2\nMIT License")
            .arg(QCoreApplication::applicationVersion(),
                 QCoreApplication::translate("Nebulens", "星空影像工作台"))
    );
}

void MainWindow::onProcessClicked() {
    if (m_worker && m_worker->isRunning()) {
        m_worker->requestCancel();
        setWorkflowStage(1, QCoreApplication::translate("Nebulens", "正在安全停止..."));
        return;
    }
    if (m_exportWorker && m_exportWorker->isRunning()) {
        m_exportWorker->requestCancel();
        setWorkflowStage(3, QCoreApplication::translate("Nebulens", "正在取消导出..."));
        return;
    }
    if (processingActive()) return;
    if (m_previewPanel->maskRefinementActive()) {
        statusBar()->showMessage(
            QCoreApplication::translate("Nebulens", "请等待地景蒙版精修完成"), 3000);
        return;
    }

    // 1. 收集文件
    auto files = m_projectPanel->includedFilePaths();
    if (files.size() < requiredFrameCount()) {
        QMessageBox::warning(
            this, QCoreApplication::translate("Nebulens", "处理"),
            requiredFrameCount() == 1
                ? QCoreApplication::translate("Nebulens", "请先导入 1 张 RAW 图像")
                : requiredFrameCount() == 3
                    ? (m_scene == ProcessingScene::StarTrail
                           ? QCoreApplication::translate("Nebulens", "星轨合成需要至少 3 张未排除的固定机位 RAW 图像")
                           : QCoreApplication::translate("Nebulens", "延时降噪需要至少 3 张未排除的 RAW 图像"))
                    : QCoreApplication::translate("Nebulens", "需要至少 2 张未排除的图像才能开始处理"));
        return;
    }
    if (m_scene == ProcessingScene::SingleFrame) {
        const QString source = effectiveSingleFrameSource();
        files = {source};
    }
    if (m_scene == ProcessingScene::DeepSky &&
        !m_paramsPanel->deepSkyCalibrationInputsComplete()) {
        QMessageBox::warning(
            this, QCoreApplication::translate("Nebulens", "深空校准来源不完整"),
            QCoreApplication::translate("Nebulens",
                "Dark 与 Flat 各需要至少 3 张 RAW 或一个 Master；"
                "生成 Master Flat 时还需 Bias 或同曝光 Dark Flat 二选一。"));
        return;
    }
    if (m_scene == ProcessingScene::DeepSky) {
        QSet<QString> usedPaths;
        QString duplicatePath;
        auto addUniquePaths = [&](const QStringList& paths) {
            for (const QString& path : paths) {
                if (path.isEmpty()) continue;
                QString identity = QFileInfo(path).canonicalFilePath();
                if (identity.isEmpty()) {
                    identity = QFileInfo(path).absoluteFilePath();
                }
                if (usedPaths.contains(identity)) {
                    duplicatePath = path;
                    return false;
                }
                usedPaths.insert(identity);
            }
            return true;
        };
        if (!addUniquePaths(files) ||
            !addUniquePaths(m_paramsPanel->darkFramePaths()) ||
            !addUniquePaths(m_paramsPanel->flatFramePaths()) ||
            !addUniquePaths(m_paramsPanel->biasFramePaths()) ||
            !addUniquePaths(m_paramsPanel->darkFlatFramePaths()) ||
            !addUniquePaths({m_paramsPanel->masterDarkPath()}) ||
            !addUniquePaths({m_paramsPanel->masterFlatPath()}) ||
            !addUniquePaths({m_paramsPanel->masterBiasPath()}) ||
            !addUniquePaths({m_paramsPanel->masterDarkFlatPath()})) {
            QMessageBox::warning(
                this, QCoreApplication::translate("Nebulens", "校准素材重复"),
                QCoreApplication::translate("Nebulens",
                    "同一个文件不能同时作为 Light 或多个校准角色：\n%1")
                    .arg(QFileInfo(duplicatePath).fileName()));
            return;
        }
    }

    // Do not let a bounded preview and a full RAW task compete for memory.
    // Wait only after validation, so a rejected run leaves the useful
    // preview on screen.
    cancelQuickPreview(true);

    // 保存当前参数设置
    m_paramsPanel->saveCurrentSettings();

    QString refFrame = effectiveReferenceFrame();
    if (!m_editedSkyGroundMask.empty()) {
        if (refFrame.isEmpty()) {
            // The edited mask is expressed in one source frame's
            // coordinates. Pin automatic selection to that frame so the
            // previewed horizon and formal blend use the same geometry.
            refFrame = m_editedMaskSourceFile;
        } else if (refFrame != m_editedMaskSourceFile) {
            QMessageBox::warning(
                this, QCoreApplication::translate("Nebulens", "地景蒙版参考帧已变化"),
                QCoreApplication::translate("Nebulens",
                    "当前修补蒙版来自 %1，但处理参考帧为 %2。"
                    "请重新检测并修补地景。")
                    .arg(QFileInfo(m_editedMaskSourceFile).fileName(),
                         QFileInfo(refFrame).fileName()));
            return;
        }
    }

    // 2. 构建参数
    ProcessingWorker::Params params;
    params.singleFrameMode = m_scene == ProcessingScene::SingleFrame;
    params.timelapseMode = m_scene == ProcessingScene::Timelapse;
    params.starTrailMode = m_scene == ProcessingScene::StarTrail;
    params.deepSkyMode = m_scene == ProcessingScene::DeepSky;
    params.darkFramePaths = m_paramsPanel->darkFramePaths();
    params.flatFramePaths = m_paramsPanel->flatFramePaths();
    params.biasFramePaths = m_paramsPanel->biasFramePaths();
    params.darkFlatFramePaths = m_paramsPanel->darkFlatFramePaths();
    params.masterDarkPath = m_paramsPanel->masterDarkPath();
    params.masterFlatPath = m_paramsPanel->masterFlatPath();
    params.masterBiasPath = m_paramsPanel->masterBiasPath();
    params.masterDarkFlatPath = m_paramsPanel->masterDarkFlatPath();
    params.saveGeneratedMasters = m_paramsPanel->saveGeneratedMasters();
    params.timelapseWindowSize = m_paramsPanel->timelapseWindowSize();
    params.timelapseStrength = m_paramsPanel->timelapseStrength();
    params.timelapseMotionProtection =
        m_paramsPanel->timelapseMotionProtection();
    params.timelapseProtectGround =
        m_paramsPanel->timelapseProtectGround();
    params.starTrailCometStrength =
        m_paramsPanel->starTrailCometStrength();
    params.starTrailReverse = m_paramsPanel->starTrailReverse();
    params.starTrailProtectGround =
        m_paramsPanel->starTrailProtectGround();
    params.stackMethod = m_paramsPanel->stackMethod();
    params.kappaValue = m_paramsPanel->kappaValue();
    params.autoRejectLowQualityFrames =
        m_paramsPanel->autoRejectLowQualityFrames();
    params.photometricNormalizationEnabled =
        m_paramsPanel->photometricNormalizationEnabled();
    const FinishingOptions finish = currentFinishingOptions();
    params.noiseReductionEnabled = finish.noiseReductionEnabled;
    params.noiseReductionStrength = finish.noiseReductionStrength;
    params.modifiedCameraColorEnabled = finish.modifiedCameraColorEnabled;
    params.modifiedCameraColor = finish.modifiedCameraColor;
    params.dewarpEnabled = finish.dehazeEnabled;
    params.dewarpStrength = finish.dehazeStrength;
    params.stretchEnabled = finish.stretchEnabled;
    params.basicAdjustments = finish.basicAdjustments;
    params.starDefringeEnabled = finish.starDefringeEnabled;
    params.starDefringeStrength = finish.starDefringeStrength;
    params.starReduceEnabled = finish.starReductionEnabled;
    params.starReduceStrength = finish.starReductionStrength;
    params.outputFormat = m_paramsPanel->outputFormat();
    params.outputPath = m_paramsPanel->outputPath();
    params.skyGroundSepEnabled = finish.skyGroundSeparation && !params.starTrailMode;
    params.skyGroundMode = m_paramsPanel->skyGroundMode();
    params.userMaskPath = m_paramsPanel->userMaskPath();
    params.featherRadius = m_paramsPanel->featherRadius();
    if (params.skyGroundSepEnabled &&
        params.skyGroundMode == SkyGroundMask::AutoDetect &&
        !m_editedSkyGroundMask.empty() &&
        m_editedMaskProjectFiles == m_projectPanel->filePaths()) {
        params.editedSkyGroundMask = m_editedSkyGroundMask;
        params.editedSkyGroundMaskWidth = m_editedSkyGroundMaskWidth;
        params.editedSkyGroundMaskHeight = m_editedSkyGroundMaskHeight;
        params.editedSkyGroundMaskSourcePath = m_editedMaskSourceFile;
    }
    params.groundStackMethod = m_paramsPanel->groundStackMethod();
    params.groundDetailStrength = finish.groundDetailStrength;

    // A new run invalidates the previous export immediately. A cancelled or
    // failed run must never leave an old result looking current.
    m_resultWasVisibleBeforeRun = m_previewPanel->isShowingResult();
    invalidateCachedResult();
    const QString upstreamSignature = currentUpstreamSignature();
    const DeliverySnapshot snapshot{m_orderGeneration,
        upstreamSignature + QStringLiteral("||") + m_paramsPanel->finishingSignature()};
    m_toolbar->setProcessing(true);
    // Worker parameters and source paths are immutable once captured.
    // Disable their editors so the visible project always describes the
    // task currently running; preview navigation remains available.
    m_projectPanel->setEditingEnabled(false);
    m_paramsPanel->setEnabled(false);
    m_inlineProgress->setValue(0);
    m_taskStatusBar->setProgressVisible(true);
    setWorkflowStage(
        m_scene == ProcessingScene::DeepSky ? 0 : 1,
        m_scene == ProcessingScene::DeepSky
            ? QCoreApplication::translate("Nebulens", "正在准备 Bayer 校准")
            : QCoreApplication::translate("Nebulens", "正在准备处理"));

    // 创建后台线程。进度与取消都留在主窗口，不再弹出第二个窗口。
    auto* worker = new ProcessingWorker(files, refFrame, params, this);
    m_worker = worker;
    m_deliveryPath.clear();
    setTaskFeedback(true, 0, QCoreApplication::translate("Nebulens", "正在准备处理"));
    refreshPrintOrderResult();
    refreshExportAction();
    connect(worker, &ProcessingWorker::phaseChanged, this, [this, worker](ProcessingWorker::Phase phase) {
        if (m_worker != worker) return;
        using Phase = ProcessingWorker::Phase;
        WorkflowStage stage = WorkflowStage::Output;
        switch (phase) {
        case Phase::Preparation: stage = m_scene == ProcessingScene::DeepSky ? WorkflowStage::Calibration : WorkflowStage::Assets; break;
        case Phase::Alignment: stage = WorkflowStage::Alignment; break;
        case Phase::Stacking: stage = WorkflowStage::Stacking; break;
        case Phase::Adjustment: stage = WorkflowStage::Adjustment; break;
        case Phase::Output: break;
        }
        // 阶段由任务直接报告。翻译只影响文案，不决定导航和输出状态。
        m_workflowRail->setCurrentStage(stage, true);
    });
    connect(worker, &ProcessingWorker::stageMessage, this, [this, worker](const QString& message) {
        if (m_worker != worker) return;
        setTaskFeedback(true, m_taskProgress, message);
        statusBar()->showMessage(message);
    });
    connect(worker, &ProcessingWorker::progress, this, [this, worker](int value) {
        if (m_worker != worker) return;
        m_inlineProgress->setValue(value);
        m_toolbar->setProgress(value);
        setTaskFeedback(true, value, m_taskMessage);
    });
    connect(worker, &ProcessingWorker::finished, this, [this, worker, snapshot, upstreamSignature]() {
        if (m_worker != worker) {
            worker->deleteLater();
            return;
        }
        QString failureDialog;
        if (!matchesDeliverySnapshot(snapshot)) {
            finishStaleDelivery(!worker->wasCancelled() && worker->errorString().isEmpty()
                ? worker->outputFile() : QString());
        } else if (worker->wasCancelled()) {
            restoreLastValidResultAfterFailedRun();
            setTaskFeedback(false, -1,
                QCoreApplication::translate("Nebulens", "处理已取消，可调整参数后重试"));
            setWorkflowStage(1, QCoreApplication::translate("Nebulens", "处理已取消，可调整参数后重试"));
            statusBar()->showMessage(QCoreApplication::translate(
                "Nebulens", "处理已取消"), 3000);
        } else if (worker->errorString().isEmpty()) {
            // 保留结果用于预览和再次导出。ProcessingWorker 已经把正式
            // 成片写到输出目录，这里不再额外写无上限的重复 TIFF 缓存。
            m_cachedStackedData = worker->takeStackedData();
            m_cachedBeforePreview = worker->takeBeforePreview();
            m_cachedBeforeBlackPoint = worker->beforePreviewBlackPoint();
            m_cachedBeforeWhitePoint = worker->beforePreviewWhitePoint();
            m_cachedWidth = worker->stackedWidth();
            m_cachedHeight = worker->stackedHeight();
            m_cachedFrameCount = worker->outputFrameCount();
            std::vector<uint16_t> quickSource =
                worker->takeQuickPreviewSource();
            std::vector<uint8_t> quickMask =
                worker->takeQuickPreviewMask();
            m_quickPreviewWidth = worker->quickPreviewWidth();
            m_quickPreviewHeight = worker->quickPreviewHeight();
            if (!quickSource.empty()) {
                m_quickPreviewSource =
                    std::make_shared<const std::vector<uint16_t>>(
                        std::move(quickSource));
            }
            if (!quickMask.empty()) {
                m_quickPreviewMask =
                    std::make_shared<const std::vector<uint8_t>>(
                        std::move(quickMask));
            }
            m_lastProcessedSignature = snapshot.processingSignature;
            m_lastProcessedUpstreamSignature = upstreamSignature;
            m_formalProof = ImageExporter::makeRgb16Proof(
                m_cachedStackedData, m_cachedWidth, m_cachedHeight);

            showBestAvailableResult();
            m_workflowRail->setStageComplete(
                WorkflowStage::Alignment, true);
            m_workflowRail->setStageComplete(
                WorkflowStage::Stacking, true);
            m_workflowRail->setStageComplete(
                WorkflowStage::Adjustment, true);
            m_workflowRail->setStageComplete(
                WorkflowStage::Output, true);
            m_lastValidResultPreview = m_previewPanel->currentImage();
            m_lastValidResultPreviewKey =
                QStringLiteral("last-valid:") + m_lastProcessedSignature;
            m_deliveryPath = worker->outputFile();
            m_printOrder->setReceipt(m_deliveryPath,
                m_scene == ProcessingScene::Timelapse ? m_cachedFrameCount : 1);
            setTaskFeedback(false, 100, {});
            int frameCount = m_cachedFrameCount;
            const QString referenceName = QFileInfo(
                worker->selectedReferenceFrame()).fileName();
            const int rejectedCount = worker->qualityRejectedFiles().size();
            const int skippedCount = static_cast<int>(
                worker->skippedFrames().size());
            QStringList reportLines;
            reportLines << QCoreApplication::translate("Nebulens", "输出：%1×%2，%3 帧")
                               .arg(m_cachedWidth).arg(m_cachedHeight)
                               .arg(frameCount);
            if (m_scene != ProcessingScene::SingleFrame &&
                m_scene != ProcessingScene::Timelapse &&
                m_scene != ProcessingScene::StarTrail) {
                reportLines << QCoreApplication::translate("Nebulens",
                    "对齐：RMS %1 px，P95 %2 px，最低网格覆盖 %3%")
                    .arg(worker->averageAlignmentRms(), 0, 'f', 2)
                    .arg(worker->worstAlignmentP95(), 0, 'f', 2)
                    .arg(worker->minimumAlignmentGridCoverage() * 100.0,
                         0, 'f', 0);
                reportLines << QCoreApplication::translate("Nebulens",
                    "模型：Affine %1 帧，Homography %2 帧")
                    .arg(worker->affineFrameCount())
                    .arg(worker->homographyFrameCount());
            }
            reportLines << QCoreApplication::translate("Nebulens",
                "帧筛选：质量排除 %1，处理跳过 %2")
                .arg(rejectedCount).arg(skippedCount);
            if (worker->photometricNormalizedFrameCount() > 0) {
                reportLines << QCoreApplication::translate("Nebulens",
                    "光度归一化：%1 帧，平均增益 %2，范围 %3–%4")
                    .arg(worker->photometricNormalizedFrameCount())
                    .arg(worker->averagePhotometricGain(), 0, 'f', 3)
                    .arg(worker->minimumPhotometricGain(), 0, 'f', 3)
                    .arg(worker->maximumPhotometricGain(), 0, 'f', 3);
            }
            if (m_scene == ProcessingScene::SkyGround) {
                reportLines << QCoreApplication::translate("Nebulens",
                    "天地蒙版：天空占比 %1%，来源 %2")
                    .arg(worker->skyGroundSkyFraction() * 100.0, 0, 'f', 1)
                    .arg(worker->skyGroundMaskSource());
            }
            reportLines << QCoreApplication::translate("Nebulens", "堆栈耗时：%1 s")
                               .arg(worker->stackingElapsedMs() / 1000.0,
                                    0, 'f', 2);
            if (!worker->outputFile().isEmpty()) {
                reportLines << QCoreApplication::translate("Nebulens", "文件：%1")
                                   .arg(worker->outputFile());
            }
            m_paramsPanel->setProcessingReport(reportLines.join('\n'));
            m_paramsPanel->refreshRecentResults();
            const double medianEllipticity =
                FrameQualityEvaluator::medianValidEllipticity(
                    worker->frameQualityMetrics());
            const bool elongatedStars =
                m_scene != ProcessingScene::SingleFrame &&
                m_scene != ProcessingScene::Timelapse &&
                m_scene != ProcessingScene::StarTrail &&
                medianEllipticity >= 0.22;
            setWorkflowStage(3,
                (QCoreApplication::translate("Nebulens", "处理完成 · %1 帧 · %2×%3")
                    .arg(frameCount).arg(m_cachedWidth).arg(m_cachedHeight) +
                 (elongatedStars
                      ? QCoreApplication::translate("Nebulens", " · 星点偏长，请检查曝光拖线/像差")
                      : QString())),
                true);
            QString methodName = m_paramsPanel->stackMethod();
            if (methodName == QStringLiteral("median")) {
                methodName = QStringLiteral("Median");
            } else if (methodName == QStringLiteral("average")) {
                methodName = QStringLiteral("Average");
            } else if (methodName == QStringLiteral("kappa-sigma")) {
                methodName = QStringLiteral("Kappa-Sigma");
            } else if (methodName == QStringLiteral("winsorized")) {
                methodName = QStringLiteral("Winsorized");
            }
            m_previewPanel->showResultSummary(
                QCoreApplication::translate("Nebulens", "%1×%2 · %3 帧 · %4")
                    .arg(m_cachedWidth).arg(m_cachedHeight)
                    .arg(frameCount).arg(methodName));
            statusBar()->showMessage(
                m_scene == ProcessingScene::SingleFrame
                    ? QCoreApplication::translate("Nebulens", "单张精修完成 — %1×%2 | %3")
                          .arg(m_cachedWidth).arg(m_cachedHeight)
                          .arg(referenceName)
                    : m_scene == ProcessingScene::Timelapse
                        ? QCoreApplication::translate("Nebulens",
                              "延时降噪完成 — 已输出 %1 张 · %2×%3 | %4")
                              .arg(frameCount).arg(m_cachedWidth).arg(m_cachedHeight)
                              .arg(worker->outputFile())
                        : m_scene == ProcessingScene::StarTrail
                            ? QCoreApplication::translate("Nebulens",
                                  "星轨合成完成 — %1×%2 · %3 帧 | %4")
                                  .arg(m_cachedWidth).arg(m_cachedHeight)
                                  .arg(frameCount).arg(worker->outputFile())
                    : (QCoreApplication::translate("Nebulens",
                          "处理完成 — %1×%2 已堆栈 %3 帧 | 参考 %4 | 质量排除 %5 帧 | 跳过 %6 帧")
                          .arg(m_cachedWidth).arg(m_cachedHeight)
                          .arg(frameCount).arg(referenceName)
                          .arg(rejectedCount).arg(skippedCount) +
                       (elongatedStars
                            ? QCoreApplication::translate("Nebulens",
                                  " | 星点偏长，检查曝光拖线/像差")
                            : QString())),
                5000);
            const QStringList calibrationWarnings =
                worker->calibrationPreflightWarnings();
            const QStringList generatedMasters =
                worker->generatedMasterFiles();
            if (!generatedMasters.isEmpty()) {
                statusBar()->showMessage(
                    QCoreApplication::translate("Nebulens",
                        "处理完成，已保存 %1 个 Master 到 %2")
                        .arg(generatedMasters.size())
                        .arg(QFileInfo(generatedMasters.first())
                                 .absolutePath()),
                    7000);
            }
            if (m_workspacePages->currentWidget() == m_darkroom &&
                m_scene == ProcessingScene::DeepSky &&
                !calibrationWarnings.isEmpty()) {
                auto* advice = new QMessageBox(
                    QMessageBox::Information,
                    QCoreApplication::translate("Nebulens", "校准素材建议"),
                    QCoreApplication::translate("Nebulens",
                        "处理已经完成，但校准素材还有可改进之处。"),
                    QMessageBox::Ok, this);
                advice->setInformativeText(
                    QCoreApplication::translate("Nebulens", "• ") +
                    calibrationWarnings.join(QCoreApplication::translate("Nebulens", "\n• ")));
                advice->setAttribute(Qt::WA_DeleteOnClose);
                advice->open();
            }
        } else {
            restoreLastValidResultAfterFailedRun();
            setTaskFeedback(false, -1, worker->errorString());
            if (m_workspacePages->currentWidget() == m_darkroom)
                failureDialog = worker->errorString();
            setWorkflowStage(1, QCoreApplication::translate("Nebulens", "处理失败，请检查素材或参数"));
            statusBar()->showMessage(QCoreApplication::translate(
                "Nebulens", "处理失败"), 3000);
        }
        worker->deleteLater();
        m_worker = nullptr;
        unlockDeliveryTask();
        if (!failureDialog.isEmpty()) {
            QMessageBox::warning(this, QCoreApplication::translate("Nebulens", "处理失败"), failureDialog);
        }
    });
    worker->start();
}

void MainWindow::onExportClicked() {
    if (processingActive()) {
        statusBar()->showMessage(QCoreApplication::translate("Nebulens", "处理完成后再导出"), 3000);
        return;
    }
    if (m_previewPanel->isShowingHistoryResult()) {
        QMessageBox::information(
            this, QCoreApplication::translate("Nebulens", "历史结果只读"),
            QCoreApplication::translate("Nebulens",
                "历史结果仅用于查看。按 R 返回当前会话结果后再导出。"));
        return;
    }
    if (m_cachedStackedData.empty()) {
        QMessageBox::warning(
            this, QCoreApplication::translate("Nebulens", "导出"),
            QCoreApplication::translate(
                "Nebulens", "没有可用的堆栈结果，请先完成处理"));
        return;
    }
    if (currentProcessingSignature() != m_lastProcessedSignature) {
        QMessageBox::information(
            this, QCoreApplication::translate("Nebulens", "结果需要更新"),
            QCoreApplication::translate("Nebulens", "处理参数已经改变。请重新处理后再导出，避免把旧结果误认为当前参数的结果。"));
        return;
    }
    if (m_scene == ProcessingScene::Timelapse) {
        if (!m_deliveryPath.isEmpty()) revealDelivery(m_deliveryPath);
        return;
    }

    QString outPath = m_paramsPanel->outputPath();
    if (outPath.isEmpty()) outPath = QDir::homePath() + "/Nebulens/Output";
    QDir().mkpath(outPath);

    ImageExporter::Format fmt = ImageExporter::Tiff16;
    QString ext = ".tiff";
    if (m_paramsPanel->outputFormat() == "png8") {
        fmt = ImageExporter::Png8;
        ext = ".png";
    }

    QString fileName = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") +
        (m_scene == ProcessingScene::SingleFrame
            ? "_single_export"
            : m_scene == ProcessingScene::StarTrail
                ? "_star_trail_export" : "_stacked_export") + ext;
    QString fullPath = outPath + "/" + fileName;

    const DeliverySnapshot snapshot{m_orderGeneration, m_lastProcessedSignature};
    auto image = std::make_shared<std::vector<uint16_t>>(
        std::move(m_cachedStackedData));
    auto* worker = new ExportWorker(
        image, m_cachedWidth, m_cachedHeight, fullPath, fmt, this);
    m_exportWorker = worker;
    m_deliveryPath.clear();
    setTaskFeedback(true, -1, QCoreApplication::translate("Nebulens", "正在导出 %1...").arg(fileName));
    refreshExportAction();
    m_projectPanel->setEditingEnabled(false);
    m_paramsPanel->setEnabled(false);
    m_toolbar->setProcessing(true);
    m_inlineProgress->setRange(0, 100);
    m_taskStatusBar->setProgressVisible(false);
    statusBar()->showMessage(QCoreApplication::translate("Nebulens", "正在导出 %1...").arg(fileName));

    connect(worker, &ExportWorker::finished, this,
            [this, worker, image, snapshot]() {
                if (m_exportWorker != worker) {
                    worker->deleteLater();
                    return;
                }
                QString failureDialog;
                if (!matchesDeliverySnapshot(snapshot)) {
                    finishStaleDelivery(worker->succeeded() ? worker->outputPath() : QString());
                } else {
                    m_cachedStackedData = std::move(*image);
                    if (worker->succeeded()) {
                        m_deliveryPath = worker->outputPath();
                        m_printOrder->setReceipt(m_deliveryPath, 1);
                        setTaskFeedback(false, 100, {});
                        m_paramsPanel->refreshRecentResults();
                        statusBar()->showMessage(QCoreApplication::translate("Nebulens",
                            "已导出到：%1").arg(m_deliveryPath), 5000);
                    } else if (worker->wasCancelled()) {
                        const QString cancelled = QCoreApplication::translate("Nebulens", "导出已取消");
                        setTaskFeedback(false, -1, cancelled);
                        statusBar()->showMessage(cancelled, 3000);
                    } else {
                        failureDialog = QCoreApplication::translate("Nebulens", "无法写入文件，请检查输出目录权限");
                        setTaskFeedback(false, -1, failureDialog);
                    }
                }
                m_exportWorker = nullptr;
                worker->deleteLater();
                unlockDeliveryTask();
                if (!failureDialog.isEmpty()) {
                    QMessageBox::warning(
                        this, QCoreApplication::translate("Nebulens", "导出失败"),
                        failureDialog);
                }
            });
    worker->start();
}

void MainWindow::onSettingsClicked() {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(
        QCoreApplication::translate("Nebulens", "设置"));
    dialog->setFixedSize(520, 252);

    auto* layout = new QVBoxLayout(dialog);
    layout->setSpacing(16);
    layout->setContentsMargins(20, 20, 20, 20);

    auto* title = new QLabel(QCoreApplication::translate("Nebulens", "应用设置"), dialog);
    title->setProperty(
        StyleTokens::Properties::kTextRole,
        StyleTokens::Properties::kTitle);
    layout->addWidget(title);

    QSettings settings("StarProcessor", "App");
    const QString currentLanguage = AppLocalization::preferredLanguage();
    auto* languageRow = new QHBoxLayout();
    auto* languageLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "界面语言:"), dialog);
    languageLabel->setFixedWidth(82);
    auto* languageCombo = new QComboBox(dialog);
    languageCombo->addItem(
        QCoreApplication::translate("Nebulens", "简体中文"),
        QString::fromLatin1(AppLocalization::kSimplifiedChinese));
    languageCombo->addItem(
        QStringLiteral("English"),
        QString::fromLatin1(AppLocalization::kEnglish));
    const int languageIndex = languageCombo->findData(currentLanguage);
    languageCombo->setCurrentIndex(std::max(0, languageIndex));
    languageRow->addWidget(languageLabel);
    languageRow->addWidget(languageCombo, 1);
    layout->addLayout(languageRow);

    auto* languageNote = new QLabel(
        QCoreApplication::translate(
            "Nebulens", "语言切换将在重启应用后完整生效。"), dialog);
    languageNote->setProperty(
        StyleTokens::Properties::kTextRole,
        StyleTokens::Properties::kCaption);
    layout->addWidget(languageNote);

    // 输出目录
    auto* outDirRow = new QHBoxLayout();
    auto* outDirLabel = new QLabel(
        QCoreApplication::translate("Nebulens", "输出目录:"), dialog);
    outDirLabel->setFixedWidth(82);
    QString defaultOutDir = settings.value(
        "outputPath", QDir::homePath() + "/Nebulens/Output").toString();
    auto* outDirEdit = new QLineEdit(defaultOutDir, dialog);
    outDirEdit->setReadOnly(true);
    auto* outDirBtn = new QPushButton(dialog);
    outDirBtn->setIcon(
        UiAssets::icon(
            UiAssets::Glyph::Folder,
            StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kTextSecondary)));
    outDirBtn->setToolTip(QCoreApplication::translate("Nebulens", "选择输出目录"));
    outDirBtn->setFixedSize(28, 28);
    outDirBtn->setProperty(
        StyleTokens::Properties::kVariant,
        StyleTokens::Properties::kIcon);
    connect(outDirBtn, &QPushButton::clicked, this, [this, outDirEdit]() {
        QString dir = FileDialogUtils::getExistingDirectory(
            this,
            QCoreApplication::translate("Nebulens", "选择输出目录"),
            outDirEdit->text());
        if (!dir.isEmpty()) outDirEdit->setText(dir);
    });
    outDirRow->addWidget(outDirLabel);
    outDirRow->addWidget(outDirEdit, 1);
    outDirRow->addWidget(outDirBtn);
    layout->addLayout(outDirRow);

    layout->addStretch();

    // 底部按钮
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(
        QCoreApplication::translate("Nebulens", "取消"), dialog);
    connect(cancelBtn, &QPushButton::clicked,
            dialog, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    auto* okBtn = new QPushButton(
        QCoreApplication::translate("Nebulens", "确定"), dialog);
    okBtn->setProperty(
        StyleTokens::Properties::kVariant,
        StyleTokens::Properties::kPrimary);
    connect(okBtn, &QPushButton::clicked, dialog,
            [this, dialog, outDirEdit, languageCombo,
             currentLanguage]() {
        QSettings s("StarProcessor", "App");
        s.setValue("outputPath", outDirEdit->text());
        // 同步更新参数面板的输出路径（无需重启即生效）
        if (m_paramsPanel) {
            m_paramsPanel->setOutputPath(outDirEdit->text());
        }
        const QString selectedLanguage =
            languageCombo->currentData().toString();
        const bool languageChanged = selectedLanguage != currentLanguage;
        if (languageChanged) {
            AppLocalization::setPreferredLanguage(selectedLanguage);
        }
        dialog->accept();

        if (!languageChanged) return;
        QMessageBox restartPrompt(this);
        restartPrompt.setWindowTitle(QCoreApplication::translate(
            "Nebulens", "切换界面语言"));
        restartPrompt.setText(QCoreApplication::translate(
            "Nebulens",
            "需要重启 Nebulens 才能统一更新全部界面和文件对话框。"
            "当前项目不会自动保存。"));
        auto* restartButton = restartPrompt.addButton(
            QCoreApplication::translate("Nebulens", "立即重启"),
            QMessageBox::AcceptRole);
        restartPrompt.addButton(
            QCoreApplication::translate("Nebulens", "稍后"),
            QMessageBox::RejectRole);
        restartPrompt.exec();
        if (restartPrompt.clickedButton() == restartButton) {
            if (AppLocalization::restartApplication()) {
                qApp->quit();
            } else {
                QMessageBox::warning(
                    this,
                    QCoreApplication::translate(
                        "Nebulens", "无法重启应用"),
                    QCoreApplication::translate(
                        "Nebulens", "请手动退出并重新打开 Nebulens。"));
            }
        }
    });
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::showShortcutsDialog() {
    QMessageBox::information(
        this, QCoreApplication::translate("Nebulens", "快捷键"),
        QCoreApplication::translate("Nebulens",
            "导入文件    Cmd/Ctrl+O\n"
            "导入目录    Shift+Cmd/Ctrl+O\n"
            "处理/取消   Cmd/Ctrl+Return\n"
            "导出结果    Cmd/Ctrl+E\n\n"
            "适应视图    F\n"
            "实际像素    1\n"
            "缩放        + / -\n"
            "循环对比    B\n"
            "直接分屏    Shift+B\n"
            "蒙版叠加    M\n"
            "返回结果    R\n"
            "灰点吸管    G\n"
            "排除/恢复   Space\n"
            "笔刷大小    [ / ]\n"
            "退出工具    Esc"));
}

void MainWindow::loadRecentResultPreview(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) return;
    if (m_historyPreviewWorker && m_historyPreviewWorker->isRunning()) {
        statusBar()->showMessage(
            QCoreApplication::translate("Nebulens", "正在载入另一张历史结果，请稍候"), 3000);
        return;
    }
    auto* worker = new HistoryPreviewWorker(path, this);
    const auto generation = m_orderGeneration;
    m_historyPreviewWorker = worker;
    statusBar()->showMessage(
        QCoreApplication::translate("Nebulens", "正在载入历史结果：%1")
            .arg(QFileInfo(path).fileName()));
    connect(worker, &HistoryPreviewWorker::loaded, this,
            [this, worker, generation](const QString& loadedPath,
                           const QImage& preview) {
                if (m_historyPreviewWorker != worker || generation != m_orderGeneration) return;
                m_previewPanel->loadImage(
                    preview, QStringLiteral("history:") + loadedPath);
                m_previewPanel->setResultAvailable(
                    !m_cachedStackedData.empty() ||
                        !m_lastValidResultPreview.isNull(),
                    false);
                refreshExportAction();
                statusBar()->showMessage(
                    QCoreApplication::translate("Nebulens", "历史结果只读预览 · %1")
                        .arg(QFileInfo(loadedPath).fileName()), 5000);
            });
    connect(worker, &HistoryPreviewWorker::failed, this,
            [this, worker, generation](const QString& failedPath,
                           const QString& reason) {
                if (m_historyPreviewWorker != worker || generation != m_orderGeneration) return;
                statusBar()->showMessage(
                    QCoreApplication::translate("Nebulens", "历史结果载入失败：%1 · %2")
                        .arg(QFileInfo(failedPath).fileName(), reason),
                    5000);
            });
    connect(worker, &QThread::finished, this, [this, worker]() {
        if (m_historyPreviewWorker == worker) {
            m_historyPreviewWorker = nullptr;
        }
        worker->deleteLater();
    });
    worker->start();
}
