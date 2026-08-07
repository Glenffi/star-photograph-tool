#include <QSettings>
#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QDir>
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QProgressDialog>
#include <QProgressBar>
#include <QDateTime>
#include <QDebug>
#include <QSet>
#include <QTimer>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <algorithm>
#include <memory>
#include "ui/ProjectPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/ParamsPanel.h"
#include "ui/Toolbar.h"
#include "ui/UiAssets.h"
#include "ui/SceneLauncher.h"

#include "core/RawImageLoader.h"
#include "core/ImageExporter.h"
#include "workers/MaskPreviewWorker.h"
#include "workers/ProcessingWorker.h"
#include "workers/QuickPreviewWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("StarProcessor — 星空摄影师 RAW 处理工具");
        resize(1400, 900);
        setMinimumSize(1100, 720);
        setAcceptDrops(true);

        setupCentralWidget();
        setupMenuBar();
        setupStatusBar();
        setupStepBar();
        setupConnections();

        statusBar()->showMessage("就绪 — 拖入 RAW 文件或点击导入开始");
    }

    void importFiles(const QStringList& paths) {
        if (m_projectPanel && !paths.isEmpty()) {
            if (!m_sceneActive) {
                activateScene(paths.size() == 1
                                  ? ProcessingScene::SingleFrame
                                  : ProcessingScene::Nightscape);
            } else if (m_contentStack->currentWidget() == m_sceneLauncher) {
                activateScene(m_scene);
            }
            m_projectPanel->addFiles(paths);
        }
    }

    void selectStartupScene(ProcessingScene scene) {
        activateScene(scene);
    }

    ~MainWindow() {
        if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
            m_quickPreviewWorker->requestCancel();
            m_quickPreviewWorker->wait();
        }
        // 等待 ProcessingWorker 真正结束（无超时，避免销毁运行中的线程）
        if (m_worker && m_worker->isRunning()) {
            m_worker->requestCancel();
            m_worker->wait();
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

protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!processingActive() && event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        if (processingActive()) {
            event->ignore();
            statusBar()->showMessage(QString::fromUtf8("处理中不能修改素材"), 3000);
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
            importFiles(filePaths);
            statusBar()->showMessage(
                QString("拖放导入 %1 个文件").arg(filePaths.size()),
                5000
            );
        }
    }

private:
    void setupCentralWidget() {
        auto* centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);

        auto* mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // 顶部工具栏
        m_toolbar = new Toolbar(this);
        mainLayout->addWidget(m_toolbar);

        m_contentStack = new QStackedWidget(centralWidget);
        m_sceneLauncher = new SceneLauncher(m_contentStack);
        m_contentStack->addWidget(m_sceneLauncher);

        m_workspacePage = new QWidget(m_contentStack);
        auto* workspaceLayout = new QVBoxLayout(m_workspacePage);
        workspaceLayout->setContentsMargins(0, 0, 0, 0);
        workspaceLayout->setSpacing(0);

        // 工作流状态只反映真实处理阶段，不作为伪导航使用。
        m_stepBar = new QWidget(m_workspacePage);
        m_stepBar->setFixedHeight(40);
        m_stepBar->setStyleSheet(
            "QWidget { background-color: #111719; border-bottom: 1px solid #263234; }"
        );
        auto* stepLayout = new QHBoxLayout(m_stepBar);
        stepLayout->setContentsMargins(16, 0, 12, 0);
        stepLayout->setSpacing(6);
        const QStringList steps = {
            QString::fromUtf8("素材"), QString::fromUtf8("对齐"),
            QString::fromUtf8("堆栈"), QString::fromUtf8("结果")
        };
        for (int i = 0; i < steps.size(); ++i) {
            auto* label = new QLabel(steps[i], m_stepBar);
            label->setAlignment(Qt::AlignCenter);
            label->setMinimumWidth(64);
            m_stepLabels.append(label);
            stepLayout->addWidget(label);
            if (i + 1 < steps.size()) {
                auto* connector = new QLabel(QString::fromUtf8("›"), m_stepBar);
                connector->setStyleSheet("color: #536763; border: none; font-size: 15px;");
                stepLayout->addWidget(connector);
            }
        }
        stepLayout->addStretch();
        m_inlineProgress = new QProgressBar(m_stepBar);
        m_inlineProgress->setRange(0, 100);
        m_inlineProgress->setTextVisible(false);
        m_inlineProgress->setFixedSize(110, 5);
        m_inlineProgress->setVisible(false);
        m_inlineProgress->setStyleSheet(
            "QProgressBar { background-color: #2B393B; border: none; border-radius: 2px; }"
            "QProgressBar::chunk { background-color: #4ED7AE; border-radius: 2px; }"
        );
        stepLayout->addWidget(m_inlineProgress);
        m_workflowStatus = new QLabel(m_stepBar);
        m_workflowStatus->setMinimumWidth(210);
        m_workflowStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_workflowStatus->setStyleSheet("color: #81938F; border: none; font-size: 10px;");
        stepLayout->addWidget(m_workflowStatus);
        workspaceLayout->addWidget(m_stepBar);

        // 主内容区：素材、预览、参数三栏。
        auto* contentSplitter = new QSplitter(Qt::Horizontal, m_workspacePage);
        contentSplitter->setHandleWidth(2);
        contentSplitter->setStyleSheet(
            "QSplitter::handle { background-color: #263234; }"
        );

        // 左侧面板：ProjectPanel（卡片式文件列表）
        m_projectPanel = new ProjectPanel(contentSplitter);
        m_projectPanel->setMinimumWidth(250);
        m_projectPanel->setMaximumWidth(430);
        contentSplitter->addWidget(m_projectPanel);

        // 中央面板：PreviewPanel（QLabel + QScrollArea）
        m_previewPanel = new PreviewPanel(contentSplitter);
        contentSplitter->addWidget(m_previewPanel);

        // 右侧面板：ParamsPanel（实际参数面板）
        m_paramsPanel = new ParamsPanel(contentSplitter);
        m_paramsPanel->setMinimumWidth(280);
        m_paramsPanel->setMaximumWidth(460);
        contentSplitter->addWidget(m_paramsPanel);

        // 设置默认比例：22% / 56% / 22%
        contentSplitter->setSizes({300, 780, 320});

        workspaceLayout->addWidget(contentSplitter, 1);
        m_contentStack->addWidget(m_workspacePage);
        m_contentStack->setCurrentWidget(m_sceneLauncher);
        mainLayout->addWidget(m_contentStack, 1);
    }

    QString sceneName() const {
        switch (m_scene) {
        case ProcessingScene::SingleFrame:
            return QString::fromUtf8("单张精修");
        case ProcessingScene::Nightscape:
            return QString::fromUtf8("银河星景");
        case ProcessingScene::DeepSky:
            return QString::fromUtf8("深空天体");
        case ProcessingScene::SkyGround:
            return QString::fromUtf8("天地分离");
        case ProcessingScene::StarTrail:
            return QString::fromUtf8("星轨合成");
        case ProcessingScene::Timelapse:
            return QString::fromUtf8("延时序列");
        }
        return QString();
    }

    int requiredFrameCount() const {
        if (m_scene == ProcessingScene::SingleFrame) return 1;
        if (m_scene == ProcessingScene::Timelapse ||
            m_scene == ProcessingScene::StarTrail) return 3;
        return 2;
    }

    void activateScene(ProcessingScene scene) {
        const bool changed = !m_sceneActive || m_scene != scene;
        m_scene = scene;
        m_sceneActive = true;

        switch (scene) {
        case ProcessingScene::SingleFrame:
            m_sceneStepNames = {QString::fromUtf8("素材"), QString::fromUtf8("调整"),
                                QString::fromUtf8("预览"), QString::fromUtf8("导出")};
            break;
        case ProcessingScene::SkyGround:
            m_sceneStepNames = {QString::fromUtf8("素材"), QString::fromUtf8("蒙版"),
                                QString::fromUtf8("双路堆栈"), QString::fromUtf8("结果")};
            break;
        case ProcessingScene::Nightscape:
            m_sceneStepNames = {QString::fromUtf8("素材"), QString::fromUtf8("对齐"),
                                QString::fromUtf8("堆栈"), QString::fromUtf8("结果")};
            break;
        case ProcessingScene::DeepSky:
            m_sceneStepNames = {QString::fromUtf8("校准"), QString::fromUtf8("对齐"),
                                QString::fromUtf8("堆栈"), QString::fromUtf8("结果")};
            break;
        case ProcessingScene::StarTrail:
            m_sceneStepNames = {QString::fromUtf8("素材"), QString::fromUtf8("星轨累积"),
                                QString::fromUtf8("地景融合"), QString::fromUtf8("结果")};
            break;
        case ProcessingScene::Timelapse:
            m_sceneStepNames = {QString::fromUtf8("素材"), QString::fromUtf8("预分析"),
                                QString::fromUtf8("逐帧降噪"), QString::fromUtf8("输出序列")};
            break;
        }

        if (changed) {
            m_paramsPanel->clearModifiedCameraGrayPoint();
            invalidateCachedResult();
            m_paramsPanel->applySceneProfile(scene);
        }
        m_contentStack->setCurrentWidget(m_workspacePage);
        updateProjectReadiness();
        statusBar()->showMessage(
            QString::fromUtf8("已进入%1流程").arg(sceneName()), 3000);
    }

    void showSceneLauncher() {
        if (processingActive()) return;
        m_contentStack->setCurrentWidget(m_sceneLauncher);
        m_toolbar->enableProcess(false);
        m_toolbar->enableExport(false);
        m_toolbar->setProjectSummary(QString::fromUtf8("选择处理场景"));
        statusBar()->showMessage(QString::fromUtf8("选择与拍摄内容匹配的处理场景"));
    }

    void updateProjectReadiness() {
        if (!m_sceneActive) return;
        const int count = m_projectPanel->includedFilePaths().size();
        const int required = requiredFrameCount();
        const bool calibrationReady = m_scene != ProcessingScene::DeepSky ||
            (m_paramsPanel->darkFramePaths().size() >= 3 &&
             m_paramsPanel->flatFramePaths().size() >= 3 &&
             m_paramsPanel->biasFramePaths().size() >= 3);
        m_toolbar->enableProcess(count >= required && calibrationReady);
        m_toolbar->setProjectSummary(
            count > 0
                ? QString::fromUtf8("%1 · %2 张素材").arg(sceneName()).arg(count)
                : QString::fromUtf8("%1 · 等待素材").arg(sceneName()));
        m_paramsPanel->updateRefFrameList(m_projectPanel->includedFilePaths());

        if (count >= required && calibrationReady) {
            if ((m_scene == ProcessingScene::Nightscape ||
                 m_scene == ProcessingScene::SkyGround) && count >= 2) {
                m_paramsPanel->recommendStackMethod(count);
            }
            QString status = QString::fromUtf8("%1 张照片已就绪").arg(count);
            if (m_scene == ProcessingScene::DeepSky && count < 6) {
                status += QString::fromUtf8(" · 建议 6 张以上获得更稳健结果");
            }
            setWorkflowStage(
                m_scene == ProcessingScene::DeepSky ? 0 : 1,
                m_scene == ProcessingScene::DeepSky
                    ? QString::fromUtf8("%1 张 Light 与校准帧已齐，可以开始处理")
                          .arg(count)
                    : status);
        } else if (count < required) {
            const int remaining = required - count;
            setWorkflowStage(0,
                QString::fromUtf8("还需要 %1 张照片").arg(remaining));
        } else {
            setWorkflowStage(0, QString::fromUtf8(
                "请分别导入至少 3 张 Dark、Flat 和 Bias 校准帧"));
        }
    }

    void setupMenuBar() {
        // 文件菜单
        auto* fileMenu = menuBar()->addMenu("文件");
        fileMenu->setStyleSheet(menuStyleSheet());

        auto* importAction = new QAction("导入 RAW...", this);
        importAction->setShortcut(QKeySequence::Open);
        connect(importAction, &QAction::triggered, this, &MainWindow::onImportClicked);
        fileMenu->addAction(importAction);

        auto* importFolderAction = new QAction("导入文件夹...", this);
        importFolderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
        connect(importFolderAction, &QAction::triggered, this, &MainWindow::onImportFolderClicked);
        fileMenu->addAction(importFolderAction);

        fileMenu->addSeparator();

        auto* clearAction = new QAction("清空项目", this);
        connect(clearAction, &QAction::triggered, this, [this]() {
            if (processingActive()) return;
            m_projectPanel->clearFiles();
            m_previewPanel->clearImage();
            m_toolbar->enableProcess(false);
            m_toolbar->enableExport(false);
            updateProjectReadiness();
            statusBar()->showMessage("项目已清空", 3000);
        });
        fileMenu->addAction(clearAction);

        fileMenu->addSeparator();

        auto* exitAction = new QAction("退出", this);
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);
        fileMenu->addAction(exitAction);

        // 编辑菜单
        auto* editMenu = menuBar()->addMenu("编辑");
        editMenu->setStyleSheet(menuStyleSheet());

        auto* removeAction = new QAction("移除所选", this);
        removeAction->setShortcut(QKeySequence::Delete);
        connect(removeAction, &QAction::triggered, this, [this]() {
            if (!processingActive()) m_projectPanel->removeSelected();
        });
        editMenu->addAction(removeAction);

        // 视图菜单
        auto* viewMenu = menuBar()->addMenu("视图");
        viewMenu->setStyleSheet(menuStyleSheet());

        m_beforeAfterAction = new QAction(QString::fromUtf8("处理前后分屏"), this);
        m_beforeAfterAction->setCheckable(true);
        m_beforeAfterAction->setEnabled(false);
        connect(m_beforeAfterAction, &QAction::toggled,
                m_previewPanel, &PreviewPanel::setBeforeAfterMode);
        viewMenu->addAction(m_beforeAfterAction);

        viewMenu->addSeparator();

        auto* fitViewAction = new QAction("适应视图", this);
        fitViewAction->setShortcut(QKeySequence("Ctrl+0"));
        connect(fitViewAction, &QAction::triggered, m_previewPanel, &PreviewPanel::fitToView);
        viewMenu->addAction(fitViewAction);

        auto* actualPixelsAction = new QAction("实际像素 (1:1)", this);
        actualPixelsAction->setShortcut(QKeySequence("Ctrl+1"));
        connect(actualPixelsAction, &QAction::triggered, m_previewPanel, &PreviewPanel::resetZoom);
        viewMenu->addAction(actualPixelsAction);

        // 处理菜单
        auto* processMenu = menuBar()->addMenu("处理");
        processMenu->setStyleSheet(menuStyleSheet());

        auto* startAction = new QAction("开始处理", this);
        startAction->setShortcut(QKeySequence("Ctrl+Return"));
        connect(startAction, &QAction::triggered, this, &MainWindow::onProcessClicked);
        processMenu->addAction(startAction);

        auto* exportAction = new QAction("导出结果", this);
        exportAction->setShortcut(QKeySequence("Ctrl+Shift+E"));
        connect(exportAction, &QAction::triggered, this, &MainWindow::onExportClicked);
        processMenu->addAction(exportAction);

        // 帮助菜单
        auto* helpMenu = menuBar()->addMenu("帮助");
        helpMenu->setStyleSheet(menuStyleSheet());

        auto* aboutAction = new QAction("关于", this);
        connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutClicked);
        helpMenu->addAction(aboutAction);
    }

    void setupStatusBar() {
        statusBar()->setStyleSheet(
            "QStatusBar { background-color: #171F21; color: #81938F; border-top: 1px solid #2B393B; }"
        );
    }

    void setupStepBar() {
        setWorkflowStage(0, QString::fromUtf8("等待选择场景"));
    }

    void setWorkflowStage(int stage, const QString& status, bool complete = false) {
        const int current = std::clamp(
            stage, 0, static_cast<int>(m_stepLabels.size()) - 1);
        for (int i = 0; i < m_stepLabels.size(); ++i) {
            QLabel* label = m_stepLabels[i];
            if (!label) continue;
            if (i < current || (complete && i <= current)) {
                label->setText(QString::fromUtf8("✓ ") + m_sceneStepNames.value(i));
                label->setStyleSheet(
                    "color: #4ED7AE; border: none; font-size: 10px; font-weight: 600; padding: 4px 7px;"
                );
            } else if (i == current) {
                label->setText(m_sceneStepNames.value(i));
                label->setStyleSheet(
                    "color: #F3F7F6; background-color: #1B2527; border: 1px solid #465B5E; "
                    "border-radius: 5px; font-size: 10px; font-weight: 700; padding: 4px 7px;"
                );
            } else {
                label->setText(m_sceneStepNames.value(i));
                label->setStyleSheet(
                    "color: #667B77; border: none; font-size: 10px; padding: 4px 7px;"
                );
            }
        }
        if (m_workflowStatus) m_workflowStatus->setText(status);
    }

    void setupConnections() {
        m_quickPreviewTimer = new QTimer(this);
        m_quickPreviewTimer->setSingleShot(true);
        m_quickPreviewTimer->setInterval(400);
        connect(m_quickPreviewTimer, &QTimer::timeout,
                this, &MainWindow::startQuickPreview);

        connect(m_sceneLauncher, &SceneLauncher::sceneSelected,
                this, &MainWindow::activateScene);

        // Toolbar 信号
        connect(m_toolbar, &Toolbar::sceneSelectorClicked,
                this, &MainWindow::showSceneLauncher);
        connect(m_toolbar, &Toolbar::importFilesClicked, this, &MainWindow::onImportClicked);
        connect(m_toolbar, &Toolbar::importFolderClicked, this, &MainWindow::onImportFolderClicked);
        connect(m_toolbar, &Toolbar::clearProjectClicked, this, [this]() {
            if (processingActive()) return;
            m_projectPanel->clearFiles();
            m_previewPanel->clearImage();
            m_toolbar->enableProcess(false);
            m_toolbar->enableExport(false);
            updateProjectReadiness();
            statusBar()->showMessage("项目已清空", 3000);
        });
        connect(m_toolbar, &Toolbar::startProcessClicked, this, &MainWindow::onProcessClicked);
        connect(m_toolbar, &Toolbar::exportResultClicked, this, &MainWindow::onExportClicked);
        connect(m_toolbar, &Toolbar::settingsClicked, this, [this]() {
            onSettingsClicked();
        });
        connect(m_toolbar, &Toolbar::aboutClicked, this, &MainWindow::onAboutClicked);

        // 文件选择 -> 预览加载
        connect(m_projectPanel, &ProjectPanel::fileSelected, this, [this](const QString& filePath) {
            m_previewPanel->loadImage(filePath);
            m_previewPanel->setResultAvailable(!m_cachedStackedData.empty(), false);
            statusBar()->showMessage(
                QString("正在加载：%1").arg(QFileInfo(filePath).fileName()));
        });
        connect(m_previewPanel, &PreviewPanel::sourcePreviewReady, this,
                [this](const QString& filePath, const QImage& image,
                       int iso, double exposureTime, double aperture,
                       int focalLength) {
                    m_projectPanel->applyPreviewData(
                        filePath, image, iso, exposureTime, aperture,
                        focalLength);
                    if (m_projectPanel->currentFilePath() == filePath) {
                        statusBar()->showMessage(
                            QString("已加载：%1")
                                .arg(QFileInfo(filePath).fileName()),
                            3000);
                    }
                });
        connect(m_previewPanel, &PreviewPanel::sourcePreviewFailed, this,
                [this](const QString& filePath) {
                    m_projectPanel->requestThumbnail(filePath);
                    if (m_projectPanel->currentFilePath() == filePath) {
                        statusBar()->showMessage(
                            QString("预览加载失败：%1")
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
                            this, QString::fromUtf8("手动灰点"),
                            QString::fromUtf8(
                                "请先完成一次正式处理。之后可在结果预览中采样，"
                                "并立即通过快速预览检查色彩。"));
                        return;
                    }
                    cancelQuickPreview(false);
                    showBestAvailableResult();
                    m_previewPanel->setPointSelectionActive(true);
                    statusBar()->showMessage(
                        QString::fromUtf8("请在结果中点击应为灰色的天空区域"));
                });
        connect(m_previewPanel, &PreviewPanel::imagePointSelected,
                this, [this](double normalizedX, double normalizedY) {
                    m_paramsPanel->setModifiedCameraGrayPoint(
                        normalizedX, normalizedY);
                    m_previewPanel->setSelectedPoint(
                        normalizedX, normalizedY);
                    statusBar()->showMessage(
                        QString::fromUtf8("灰点已采样，正在更新快速预览"), 3000);
                });

        // 项目面板拖放导入
        connect(m_projectPanel, &ProjectPanel::filesDropped, this, [this](const QStringList& paths) {
            if (!paths.isEmpty()) {
                statusBar()->showMessage(
                    QString("拖放导入 %1 个文件").arg(paths.size()),
                    5000
                );
            } else {
                onImportClicked();
            }
        });

        // 文件变化 -> 更新按钮状态 & 参考帧列表 & 智能推荐堆栈算法
        connect(m_projectPanel, &ProjectPanel::filesChanged, this, [this]() {
            const bool wasShowingResult = m_previewPanel->isShowingResult();
            m_paramsPanel->clearModifiedCameraGrayPoint();
            invalidateCachedResult();
            updateProjectReadiness();
            if (wasShowingResult) {
                const QString currentFile = m_projectPanel->currentFilePath();
                if (currentFile.isEmpty()) m_previewPanel->clearImage();
                else m_previewPanel->loadImage(currentFile);
            }
        });

        // 参考帧变化
        connect(m_projectPanel, &ProjectPanel::referenceFrameChanged, this, [this]() {
            statusBar()->showMessage("参考帧已更新", 2000);
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

        // 参数变化
        connect(m_paramsPanel, &ParamsPanel::paramsChanged, this, [this]() {
            handleProcessingParametersChanged();
        });

        // 天地分离蒙版预览请求 -> 使用 MaskPreviewWorker
        connect(m_paramsPanel, &ParamsPanel::maskPreviewRequested, this, [this]() {
            QString currentFile = m_projectPanel->currentFilePath();
            if (currentFile.isEmpty()) {
                QMessageBox::information(this, "提示", "请先选择一张图像用于检测");
                return;
            }

            // 取消之前的预览任务，等待其真正结束
            if (m_maskPreviewWorker && m_maskPreviewWorker->isRunning()) {
                m_maskPreviewWorker->requestInterruption();
                if (!m_maskPreviewWorker->wait(3000)) {
                    // 超时：拒绝启动新任务，避免旧 worker 失去清理路径
                    statusBar()->showMessage("上一个检测任务仍在运行，请稍后再试", 3000);
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
                        m_previewPanel->setMaskOverlay(worker->takeMask(),
                                                        worker->width(),
                                                        worker->height());
                        statusBar()->showMessage("地景检测完成，蓝色=天空，绿色=地景", 5000);
                    }
                } else if (worker == m_maskPreviewWorker) {
                    QMessageBox::warning(this, "错误", worker->errorString());
                }
                worker->deleteLater();
                if (m_maskPreviewWorker == worker) {
                    m_maskPreviewWorker = nullptr;
                }
            });
            worker->start();
        });
    }

    QString menuStyleSheet() const {
        return "QMenu { background-color: #171F21; color: #D2DDDA; border: 1px solid #344548; padding: 5px; }"
               "QMenu::item { padding: 7px 22px 7px 10px; border-radius: 4px; }"
               "QMenu::item:selected { background-color: #273336; color: #F3F7F6; }"
               "QMenu::separator { height: 1px; background-color: #2B393B; margin: 5px 8px; }";
    }

    static QString formatExposureTime(double seconds) {
        if (seconds >= 1.0) {
            return QString("%1s").arg(seconds, 0, 'f', seconds >= 10.0 ? 0 : 1);
        }
        if (seconds > 0.0) {
            return QString("1/%1s").arg(qRound(1.0 / seconds));
        }
        return QString::fromUtf8("—");
    }

    bool processingActive() const {
        return m_worker && m_worker->isRunning();
    }

    QString effectiveReferenceFrame() const {
        const QStringList files = m_projectPanel->includedFilePaths();
        QString reference = m_paramsPanel->selectedReferenceFrame();
        if (reference.isEmpty() || !files.contains(reference)) {
            reference = m_projectPanel->referenceFramePath();
        }
        return files.contains(reference) ? reference : QString();
    }

    QString currentUpstreamSignature() const {
        // ParamsPanel owns the algorithm settings, while ProjectPanel owns the
        // context-menu reference choice. Sign the effective value used by the
        // worker so cache/export validity follows both controls.
        return m_paramsPanel->upstreamSignature() +
            QStringLiteral("|effective-reference=") + effectiveReferenceFrame();
    }

    QString currentProcessingSignature() const {
        return currentUpstreamSignature() + QStringLiteral("||") +
            m_paramsPanel->finishingSignature();
    }

    void handleProcessingParametersChanged() {
        updateProjectReadiness();
        if (m_cachedStackedData.empty()) return;
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
        m_toolbar->enableExport(current);
        if (current) {
            cancelQuickPreview(false);
            setWorkflowStage(3,
                QString::fromUtf8("当前结果 · %1 帧 · %2×%3")
                    .arg(m_cachedFrameCount).arg(m_cachedWidth).arg(m_cachedHeight),
                true);
            if (m_previewPanel->isShowingResult()) {
                showBestAvailableResult();
            }
        } else if (quickPreviewEligible()) {
            if (currentQuickPreviewAvailable()) {
                setWorkflowStage(3, QString::fromUtf8(
                    "快速预览已更新 · 完整导出需重新处理"));
            } else {
                scheduleQuickPreview();
            }
        } else {
            cancelQuickPreview(false);
            setWorkflowStage(1,
                QString::fromUtf8("参数已修改，重新处理后生效"));
        }
    }

    FinishingOptions currentFinishingOptions() const {
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
        options.skyGroundSeparation =
            (m_scene == ProcessingScene::SkyGround &&
             m_paramsPanel->skyGroundSeparationEnabled()) ||
            (m_scene == ProcessingScene::StarTrail &&
             m_paramsPanel->starTrailProtectGround());
        options.groundDetailStrength =
            m_scene == ProcessingScene::SkyGround &&
                options.skyGroundSeparation
                ? m_paramsPanel->groundDetailStrength() : 0;
        options.starReductionEnabled =
            m_paramsPanel->starReduceEnabled();
        options.starReductionStrength =
            m_paramsPanel->starReduceStrength();
        return options;
    }

    bool quickPreviewEligible() const {
        return m_scene != ProcessingScene::Timelapse &&
            m_quickPreviewSource && !m_quickPreviewSource->empty() &&
            currentUpstreamSignature() ==
                m_lastProcessedUpstreamSignature;
    }

    bool currentQuickPreviewAvailable() const {
        return quickPreviewEligible() &&
            !m_cachedQuickPreviewResult.empty() &&
            m_quickPreviewSignature == m_paramsPanel->finishingSignature();
    }

    void showBestAvailableResult() {
        if (currentQuickPreviewAvailable()) {
            if (!m_cachedBeforePreview.isNull()) {
                m_previewPanel->loadRgb16BitComparison(
                    m_cachedBeforePreview, m_cachedQuickPreviewResult,
                    m_quickPreviewWidth, m_quickPreviewHeight,
                    m_cachedBeforeBlackPoint, m_cachedBeforeWhitePoint);
            } else {
                m_previewPanel->loadRgb16BitImage(
                    m_cachedQuickPreviewResult,
                    m_quickPreviewWidth, m_quickPreviewHeight);
            }
            m_previewPanel->setResultLabel(QString::fromUtf8(
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
        if (m_cachedStackedData.empty()) return;
        if (!m_cachedBeforePreview.isNull()) {
            m_previewPanel->loadRgb16BitComparison(
                m_cachedBeforePreview, m_cachedStackedData,
                m_cachedWidth, m_cachedHeight,
                m_cachedBeforeBlackPoint, m_cachedBeforeWhitePoint);
        } else {
            m_previewPanel->loadRgb16BitImage(
                m_cachedStackedData, m_cachedWidth, m_cachedHeight);
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
    }

    void cancelQuickPreview(bool waitForDone) {
        if (m_quickPreviewTimer) m_quickPreviewTimer->stop();
        m_quickPreviewPending = false;
        ++m_quickPreviewGeneration;
        if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
            m_quickPreviewWorker->requestCancel();
            if (waitForDone) m_quickPreviewWorker->wait();
        }
    }

    void scheduleQuickPreview() {
        if (!quickPreviewEligible() || processingActive()) return;
        ++m_quickPreviewGeneration;
        if (m_quickPreviewWorker && m_quickPreviewWorker->isRunning()) {
            m_quickPreviewWorker->requestCancel();
        }
        if (m_quickPreviewTimer) m_quickPreviewTimer->start();
        setWorkflowStage(3, QString::fromUtf8("参数已修改 · 准备快速预览"));
    }

    void startQuickPreview() {
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
                        generation == m_quickPreviewGeneration &&
                        finishingSignature ==
                            m_paramsPanel->finishingSignature() &&
                        quickPreviewEligible();
                    if (isLatest && !worker->wasCancelled() &&
                        worker->errorString().isEmpty()) {
                        std::vector<uint16_t> result = worker->takeResult();
                        if (!result.empty()) {
                            m_cachedQuickPreviewResult = std::move(result);
                            m_quickPreviewSignature = finishingSignature;
                            showBestAvailableResult();
                            m_toolbar->enableExport(false);
                            setWorkflowStage(3, QString::fromUtf8(
                                "快速预览已更新 · 完整导出需重新处理"));
                            statusBar()->showMessage(
                                QString::fromUtf8("快速预览已更新"), 2500);
                        }
                    } else if (isLatest && !worker->wasCancelled()) {
                        statusBar()->showMessage(
                            QString::fromUtf8("快速预览失败：%1")
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

    void invalidateCachedResult() {
        cancelQuickPreview(false);
        m_cachedStackedData.clear();
        m_cachedStackedData.shrink_to_fit();
        m_cachedBeforePreview = QImage();
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
        m_runningParamsSignature.clear();
        m_runningUpstreamSignature.clear();
        if (m_toolbar) m_toolbar->enableExport(false);
        if (m_previewPanel) {
            m_previewPanel->setResultAvailable(false);
            m_previewPanel->clearComparison();
        }
    }

private slots:
    void onImportClicked() {
        if (processingActive()) {
            statusBar()->showMessage(QString::fromUtf8("处理完成后再添加照片"), 3000);
            return;
        }
        QStringList fileNames = QFileDialog::getOpenFileNames(
            this,
            "选择 RAW 文件",
            QString(),
            "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef *.cr3);;所有文件 (*)"
        );

        if (!fileNames.isEmpty()) {
            importFiles(fileNames);
            statusBar()->showMessage(
                QString("已导入 %1 个文件").arg(fileNames.size()),
                5000
            );
        }
    }

    void onImportFolderClicked() {
        if (processingActive()) {
            statusBar()->showMessage(QString::fromUtf8("处理完成后再添加文件夹"), 3000);
            return;
        }
        QString dir = QFileDialog::getExistingDirectory(this, "选择包含 RAW 文件的文件夹");
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
                if (!m_sceneActive) {
                    activateScene(ProcessingScene::Nightscape);
                } else if (m_contentStack->currentWidget() == m_sceneLauncher) {
                    activateScene(m_scene);
                }
                m_projectPanel->addFiles(fullPaths);
                statusBar()->showMessage(
                    QString("从文件夹导入 %1 个文件").arg(fullPaths.size()),
                    5000
                );
            }
        }
    }

    void onViewMetadata(const QString& filePath) {
        RawImageLoader loader;
        RawImageLoader::Metadata metadata;
        if (!loader.loadMetadata(filePath.toStdString(), metadata)) {
            QMessageBox::warning(this, "元数据", "无法加载文件元数据");
            return;
        }
        QString info = QString(
            "<b>文件路径：</b>%1<br>"
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
        QMessageBox::information(this, "图像元数据", info);
    }

    void onAboutClicked() {
        QMessageBox::about(
            this,
            "关于 StarProcessor",
            "<h2>StarProcessor</h2>"
            "<p>为星空摄影师打造的跨平台 RAW 处理工具</p>"
            "<p><b>版本：</b>" STARPROCESSOR_VERSION "</p>"
            "<p><b>能力：</b>Bayer 深空校准、RAW 堆栈、天地分离、延时序列降噪、缩星与自动优化</p>"
            "<p><b>技术栈：</b>C++17 + Qt6 + CMake + LibRaw</p>"
            "<p><b>目标平台：</b>Windows + macOS</p>"
            "<hr>"
            "<p>全部代码开源，基于 MIT License</p>"
        );
    }

    void onProcessClicked() {
        if (m_worker && m_worker->isRunning()) {
            statusBar()->showMessage("已有处理任务正在运行", 3000);
            return;
        }

        // 1. 收集文件
        auto files = m_projectPanel->includedFilePaths();
        if (files.size() < requiredFrameCount()) {
            QMessageBox::warning(
                this, "处理",
                requiredFrameCount() == 1
                    ? QString::fromUtf8("请先导入 1 张 RAW 图像")
                    : requiredFrameCount() == 3
                        ? (m_scene == ProcessingScene::StarTrail
                               ? QString::fromUtf8("星轨合成需要至少 3 张未排除的固定机位 RAW 图像")
                               : QString::fromUtf8("延时降噪需要至少 3 张未排除的 RAW 图像"))
                        : QString::fromUtf8("需要至少 2 张未排除的图像才能开始处理"));
            return;
        }
        if (m_scene == ProcessingScene::SingleFrame) {
            QString source = m_projectPanel->currentFilePath();
            if (source.isEmpty() || !files.contains(source)) source = files.first();
            files = {source};
        }
        if (m_scene == ProcessingScene::DeepSky &&
            (m_paramsPanel->darkFramePaths().size() < 3 ||
             m_paramsPanel->flatFramePaths().size() < 3 ||
             m_paramsPanel->biasFramePaths().size() < 3)) {
            QMessageBox::warning(
                this, QString::fromUtf8("深空校准帧不足"),
                QString::fromUtf8(
                    "标准深空流程至少需要 3 张 Dark、3 张 Flat 和 3 张 Bias。\n"
                    "建议每类拍摄 10–20 张，以降低主校准帧自身噪声。"));
            return;
        }
        if (m_scene == ProcessingScene::DeepSky) {
            QSet<QString> usedPaths;
            QString duplicatePath;
            auto addUniquePaths = [&](const QStringList& paths) {
                for (const QString& path : paths) {
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
                !addUniquePaths(m_paramsPanel->biasFramePaths())) {
                QMessageBox::warning(
                    this, QString::fromUtf8("校准素材重复"),
                    QString::fromUtf8(
                        "同一张 RAW 不能同时作为 Light、Dark、Flat 或 Bias：\n%1")
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

        QString refFrame = m_paramsPanel->selectedReferenceFrame();
        if (refFrame.isEmpty() || !files.contains(refFrame)) {
            refFrame = m_projectPanel->referenceFramePath();
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
        params.noiseReductionEnabled =
            m_paramsPanel->noiseReductionEnabled();
        params.noiseReductionStrength =
            m_paramsPanel->noiseReductionStrength();
        params.modifiedCameraColorEnabled =
            m_paramsPanel->modifiedCameraColorEnabled();
        params.modifiedCameraColor.strength =
            m_paramsPanel->modifiedCameraColorStrength();
        params.modifiedCameraColor.neutralMode =
            m_paramsPanel->modifiedCameraColorMode();
        params.modifiedCameraColor.manualPointX =
            m_paramsPanel->modifiedCameraGrayPointX();
        params.modifiedCameraColor.manualPointY =
            m_paramsPanel->modifiedCameraGrayPointY();
        params.dewarpEnabled = m_paramsPanel->dewarpEnabled();
        params.dewarpStrength = m_paramsPanel->dewarpStrength();
        params.stretchEnabled = m_paramsPanel->stretchEnabled();
        params.starReduceEnabled = !params.starTrailMode &&
            m_paramsPanel->starReduceEnabled();
        params.starReduceStrength = m_paramsPanel->starReduceStrength();
        params.outputFormat = m_paramsPanel->outputFormat();
        params.outputPath = m_paramsPanel->outputPath();
        params.skyGroundSepEnabled = !params.singleFrameMode &&
            !params.timelapseMode &&
            !params.starTrailMode &&
            !params.deepSkyMode &&
            m_paramsPanel->skyGroundSeparationEnabled();
        params.skyGroundMode = m_paramsPanel->skyGroundMode();
        params.userMaskPath = m_paramsPanel->userMaskPath();
        params.featherRadius = m_paramsPanel->featherRadius();
        params.groundStackMethod = m_paramsPanel->groundStackMethod();
        params.groundDetailStrength = params.starTrailMode
            ? 0 : m_paramsPanel->groundDetailStrength();

        // A new run invalidates the previous export immediately. A cancelled or
        // failed run must never leave an old result looking current.
        invalidateCachedResult();
        m_runningParamsSignature = currentProcessingSignature();
        m_runningUpstreamSignature = currentUpstreamSignature();
        m_toolbar->setProcessing(true);
        // Worker parameters and source paths are immutable once captured.
        // Disable their editors so the visible project always describes the
        // task currently running; preview navigation remains available.
        m_projectPanel->setEnabled(false);
        m_paramsPanel->setEnabled(false);
        m_inlineProgress->setValue(0);
        m_inlineProgress->setVisible(true);
        setWorkflowStage(
            m_scene == ProcessingScene::DeepSky ? 0 : 1,
            m_scene == ProcessingScene::DeepSky
                ? QString::fromUtf8("正在准备 Bayer 校准")
                : QString::fromUtf8("正在准备处理"));

        // 3. 创建进度对话框
        auto* dialog = new QProgressDialog(this);
        dialog->setWindowTitle("处理中...");
        dialog->setLabelText("初始化...");
        dialog->setRange(0, 100);
        dialog->setValue(0);
        dialog->setMinimumDuration(0);
        dialog->setCancelButtonText("取消");
        dialog->setWindowModality(Qt::NonModal);
        dialog->setStyleSheet(
            "QProgressDialog { background-color: #171F21; color: #F3F7F6; }"
            "QLabel { color: #D2DDDA; }"
            "QProgressBar { border: 1px solid #344548; border-radius: 4px; "
            "  background-color: #111719; color: #D2DDDA; text-align: center; }"
            "QProgressBar::chunk { background-color: #4ED7AE; border-radius: 3px; }"
            "QPushButton { background-color: #202A2D; color: #D2DDDA; "
            "  border: 1px solid #344548; border-radius: 5px; padding: 6px 16px; }"
        );

        // 4. 创建后台线程
        auto* worker = new ProcessingWorker(files, refFrame, params, this);
        m_worker = worker;
        connect(worker, &ProcessingWorker::progress, dialog, &QProgressDialog::setValue);
        connect(worker, &ProcessingWorker::progress, m_inlineProgress, &QProgressBar::setValue);
        connect(worker, &ProcessingWorker::stageMessage, dialog, [this, dialog](const QString& msg) {
            dialog->setLabelText(msg);
            int stage = 1;
            if (m_scene == ProcessingScene::StarTrail) {
                if (msg.contains(QString::fromUtf8("地景")) ||
                    msg.contains(QString::fromUtf8("蒙版"))) {
                    stage = 2;
                } else if (msg.contains(QString::fromUtf8("优化")) ||
                           msg.contains(QString::fromUtf8("降噪")) ||
                           msg.contains(QString::fromUtf8("色彩")) ||
                           msg.contains(QString::fromUtf8("导出")) ||
                           msg.contains(QString::fromUtf8("完成"))) {
                    stage = 3;
                }
            } else if (m_scene == ProcessingScene::Timelapse) {
                if (msg.contains(QString::fromUtf8("逐帧")) ||
                    msg.contains(QString::fromUtf8("滑动窗口"))) {
                    stage = 2;
                } else if (msg.contains(QString::fromUtf8("输出")) ||
                           msg.contains(QString::fromUtf8("完成"))) {
                    stage = 3;
                }
            } else if (m_scene == ProcessingScene::SingleFrame) {
                if (msg.contains(QString::fromUtf8("导出")) ||
                    msg.contains(QString::fromUtf8("完成"))) {
                    stage = 3;
                } else if (msg.contains(QString::fromUtf8("预览"))) {
                    stage = 2;
                }
            } else if (m_scene == ProcessingScene::DeepSky) {
                if (msg.contains(QString::fromUtf8("Master")) ||
                    msg.contains(QString::fromUtf8("Bayer")) ||
                    msg.contains(QString::fromUtf8("校准"))) {
                    stage = 0;
                } else if (msg.contains(QString::fromUtf8("堆栈")) ||
                           msg.contains(QString::fromUtf8("裁切"))) {
                    stage = 2;
                } else if (msg.contains(QString::fromUtf8("优化")) ||
                           msg.contains(QString::fromUtf8("降噪")) ||
                           msg.contains(QString::fromUtf8("色彩")) ||
                           msg.contains(QString::fromUtf8("缩星")) ||
                           msg.contains(QString::fromUtf8("导出")) ||
                           msg.contains(QString::fromUtf8("完成"))) {
                    stage = 3;
                }
            } else if (msg.contains(QString::fromUtf8("堆栈")) ||
                msg.contains(QString::fromUtf8("蒙版")) ||
                msg.contains(QString::fromUtf8("裁切"))) {
                stage = 2;
            } else if (msg.contains(QString::fromUtf8("优化")) ||
                       msg.contains(QString::fromUtf8("降噪")) ||
                       msg.contains(QString::fromUtf8("色彩")) ||
                       msg.contains(QString::fromUtf8("地景细节")) ||
                       msg.contains(QString::fromUtf8("缩星")) ||
                       msg.contains(QString::fromUtf8("导出")) ||
                       msg.contains(QString::fromUtf8("完成"))) {
                stage = 3;
            }
            setWorkflowStage(stage, msg);
        });
        connect(worker, &ProcessingWorker::finished, this, [this, dialog, worker]() {
            dialog->close();
            dialog->deleteLater();
            m_toolbar->setProcessing(false);
            m_projectPanel->setEnabled(true);
            m_paramsPanel->setEnabled(true);
            m_inlineProgress->setVisible(false);
            if (worker->wasCancelled()) {
                setWorkflowStage(1, QString::fromUtf8("处理已取消，可调整参数后重试"));
                statusBar()->showMessage("处理已取消", 3000);
            } else if (worker->errorString().isEmpty()) {
                // 成功：缓存堆栈结果
                m_cachedStackedData = worker->takeStackedData();
                m_cachedBeforePreview = worker->takeBeforePreview();
                m_cachedBeforeBlackPoint = worker->beforePreviewBlackPoint();
                m_cachedBeforeWhitePoint = worker->beforePreviewWhitePoint();
                m_cachedWidth = worker->stackedWidth();
                m_cachedHeight = worker->stackedHeight();
                m_cachedFrameCount = worker->stackedFrameCount();
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
                m_lastProcessedSignature = m_runningParamsSignature;
                m_lastProcessedUpstreamSignature =
                    m_runningUpstreamSignature;

                // 自动保存到缓存目录
                QSettings settings("StarProcessor", "App");
                QString cacheDir = settings.value(
                    "cacheDir", QDir::homePath() + "/StarProcessor/Cache").toString();
                QDir().mkpath(cacheDir);
                QString cacheFile = cacheDir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_cached.tiff";
                if (!ImageExporter::exportRgb16(m_cachedStackedData, m_cachedWidth, m_cachedHeight, cacheFile.toStdString())) {
                    qWarning() << "缓存 TIFF 写入失败:" << cacheFile;
                }

                showBestAvailableResult();
                m_toolbar->enableExport(
                    m_scene != ProcessingScene::Timelapse);
                int frameCount = m_cachedFrameCount;
                const QString referenceName = QFileInfo(
                    worker->selectedReferenceFrame()).fileName();
                const int rejectedCount = worker->qualityRejectedFiles().size();
                const int skippedCount = static_cast<int>(
                    worker->skippedFrames().size());
                const double medianEllipticity =
                    FrameQualityEvaluator::medianValidEllipticity(
                        worker->frameQualityMetrics());
                const bool elongatedStars =
                    m_scene != ProcessingScene::SingleFrame &&
                    m_scene != ProcessingScene::Timelapse &&
                    m_scene != ProcessingScene::StarTrail &&
                    medianEllipticity >= 0.22;
                setWorkflowStage(3,
                    (QString::fromUtf8("处理完成 · %1 帧 · %2×%3")
                        .arg(frameCount).arg(m_cachedWidth).arg(m_cachedHeight) +
                     (elongatedStars
                          ? QString::fromUtf8(" · 星点偏长，请检查曝光拖线/像差")
                          : QString())),
                    true);
                if (currentProcessingSignature() !=
                    m_lastProcessedSignature) {
                    setWorkflowStage(1,
                        QString::fromUtf8("处理完成，但参数已变化，请重新处理"));
                }
                statusBar()->showMessage(
                    m_scene == ProcessingScene::SingleFrame
                        ? QString::fromUtf8("单张精修完成 — %1×%2 | %3")
                              .arg(m_cachedWidth).arg(m_cachedHeight)
                              .arg(referenceName)
                        : m_scene == ProcessingScene::Timelapse
                            ? QString::fromUtf8(
                                  "延时降噪完成 — 已输出 %1 张 · %2×%3 | %4")
                                  .arg(frameCount).arg(m_cachedWidth).arg(m_cachedHeight)
                                  .arg(worker->outputFile())
                            : m_scene == ProcessingScene::StarTrail
                                ? QString::fromUtf8(
                                      "星轨合成完成 — %1×%2 · %3 帧 | %4")
                                      .arg(m_cachedWidth).arg(m_cachedHeight)
                                      .arg(frameCount).arg(worker->outputFile())
                        : (QString::fromUtf8(
                              "处理完成 — %1×%2 已堆栈 %3 帧 | 参考 %4 | 质量排除 %5 帧 | 跳过 %6 帧")
                              .arg(m_cachedWidth).arg(m_cachedHeight)
                              .arg(frameCount).arg(referenceName)
                              .arg(rejectedCount).arg(skippedCount) +
                           (elongatedStars
                                ? QString::fromUtf8(
                                      " | 星点偏长，检查曝光拖线/像差")
                                : QString())),
                    5000);
            } else {
                QMessageBox::warning(this, "处理失败", worker->errorString());
                setWorkflowStage(1, QString::fromUtf8("处理失败，请检查素材或参数"));
                statusBar()->showMessage("处理失败", 3000);
            }
            worker->deleteLater();
            if (m_worker == worker) m_worker = nullptr;
        });
        connect(dialog, &QProgressDialog::canceled, this, [this]() {
            if (m_worker) {
                m_worker->requestCancel();
                if (m_workflowStatus) {
                    m_workflowStatus->setText(QString::fromUtf8("正在安全停止..."));
                }
                statusBar()->showMessage("处理已取消", 3000);
            }
        });

        worker->start();
    }

    void onExportClicked() {
        if (processingActive()) {
            statusBar()->showMessage(QString::fromUtf8("处理完成后再导出"), 3000);
            return;
        }
        if (m_scene == ProcessingScene::Timelapse) {
            QMessageBox::information(
                this, QString::fromUtf8("延时图片序列"),
                QString::fromUtf8("处理完成时已经逐张输出整个序列，无需再次导出。"));
            return;
        }
        if (m_cachedStackedData.empty()) {
            QMessageBox::warning(this, "导出", "没有可用的堆栈结果，请先完成处理");
            return;
        }
        if (currentProcessingSignature() != m_lastProcessedSignature) {
            QMessageBox::information(
                this, QString::fromUtf8("结果需要更新"),
                QString::fromUtf8("处理参数已经改变。请重新处理后再导出，避免把旧结果误认为当前参数的结果。"));
            return;
        }

        QString outPath = m_paramsPanel->outputPath();
        if (outPath.isEmpty()) outPath = QDir::homePath() + "/StarProcessor/Output";
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

        if (ImageExporter::exportRgb16(m_cachedStackedData, m_cachedWidth, m_cachedHeight,
                                        fullPath.toStdString(), fmt)) {
            QMessageBox::information(this, "导出成功", QString("已导出到：%1").arg(fullPath));
            statusBar()->showMessage(QString("已导出：%1").arg(fileName), 5000);
        } else {
            QMessageBox::warning(this, "导出失败", "无法写入文件，请检查输出目录权限");
        }
    }

    void onSettingsClicked() {
        auto* dialog = new QDialog(this);
        dialog->setWindowTitle("设置");
        dialog->setFixedSize(480, 240);
        dialog->setStyleSheet(
            "QDialog { background-color: #171F21; color: #F3F7F6; }"
            "QLabel { color: #D2DDDA; background-color: transparent; }"
            "QLineEdit { background-color: #202A2D; color: #D2DDDA; "
            "  border: 1px solid #344548; border-radius: 5px; padding: 5px 8px; }"
            "QPushButton { background-color: #202A2D; color: #D2DDDA; "
            "  border: 1px solid #344548; border-radius: 5px; padding: 6px 16px; }"
            "QPushButton:hover { background-color: #273336; border-color: #4D6265; }"
            "QComboBox { background-color: #202A2D; color: #D2DDDA; "
            "  border: 1px solid #344548; border-radius: 5px; padding: 5px 8px; }"
        );

        auto* layout = new QVBoxLayout(dialog);
        layout->setSpacing(16);
        layout->setContentsMargins(20, 20, 20, 20);

        auto* title = new QLabel(QString::fromUtf8("应用设置"), dialog);
        title->setStyleSheet("font-size: 16px; font-weight: 700; color: #F3F7F6;");
        layout->addWidget(title);

        // 输出目录
        auto* outDirRow = new QHBoxLayout();
        auto* outDirLabel = new QLabel("输出目录:", dialog);
        QSettings settings("StarProcessor", "App");
        QString defaultOutDir = settings.value("outputPath", QDir::homePath() + "/StarProcessor/Output").toString();
        auto* outDirEdit = new QLineEdit(defaultOutDir, dialog);
        outDirEdit->setReadOnly(true);
        auto* outDirBtn = new QPushButton(dialog);
        outDirBtn->setIcon(
            UiAssets::icon(UiAssets::Glyph::Folder, QColor("#A7B8B4")));
        outDirBtn->setToolTip(QString::fromUtf8("选择输出目录"));
        outDirBtn->setFixedSize(28, 28);
        connect(outDirBtn, &QPushButton::clicked, this, [outDirEdit]() {
            QString dir = QFileDialog::getExistingDirectory(nullptr, "选择输出目录");
            if (!dir.isEmpty()) outDirEdit->setText(dir);
        });
        outDirRow->addWidget(outDirLabel);
        outDirRow->addWidget(outDirEdit, 1);
        outDirRow->addWidget(outDirBtn);
        layout->addLayout(outDirRow);

        // 缓存目录
        auto* cacheRow = new QHBoxLayout();
        auto* cacheLabel = new QLabel("缓存目录:", dialog);
        QString defaultCacheDir = settings.value("cacheDir", QDir::homePath() + "/StarProcessor/Cache").toString();
        auto* cacheEdit = new QLineEdit(defaultCacheDir, dialog);
        cacheEdit->setReadOnly(true);
        auto* cacheBtn = new QPushButton(dialog);
        cacheBtn->setIcon(
            UiAssets::icon(UiAssets::Glyph::Folder, QColor("#A7B8B4")));
        cacheBtn->setToolTip(QString::fromUtf8("选择缓存目录"));
        cacheBtn->setFixedSize(28, 28);
        connect(cacheBtn, &QPushButton::clicked, this, [cacheEdit]() {
            QString dir = QFileDialog::getExistingDirectory(nullptr, "选择缓存目录");
            if (!dir.isEmpty()) cacheEdit->setText(dir);
        });
        cacheRow->addWidget(cacheLabel);
        cacheRow->addWidget(cacheEdit, 1);
        cacheRow->addWidget(cacheBtn);
        layout->addLayout(cacheRow);

        layout->addStretch();

        // 底部按钮
        auto* btnRow = new QHBoxLayout();
        btnRow->addStretch();
        auto* okBtn = new QPushButton("确定", dialog);
        okBtn->setStyleSheet(
            "QPushButton { background-color: #4ED7AE; color: #0D211B; "
            "  font-weight: 700; border: none; border-radius: 5px; padding: 6px 24px; }"
            "QPushButton:hover { background-color: #67E2BE; }"
        );
        connect(okBtn, &QPushButton::clicked, dialog, [this, dialog, outDirEdit, cacheEdit]() {
            QSettings s("StarProcessor", "App");
            s.setValue("outputPath", outDirEdit->text());
            s.setValue("cacheDir", cacheEdit->text());
            // 同步更新参数面板的输出路径（无需重启即生效）
            if (m_paramsPanel) {
                m_paramsPanel->setOutputPath(outDirEdit->text());
            }
            dialog->accept();
        });
        btnRow->addWidget(okBtn);
        layout->addLayout(btnRow);

        dialog->exec();
        dialog->deleteLater();
    }

private:
    Toolbar* m_toolbar = nullptr;
    SceneLauncher* m_sceneLauncher = nullptr;
    QStackedWidget* m_contentStack = nullptr;
    QWidget* m_workspacePage = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    PreviewPanel* m_previewPanel = nullptr;
    ParamsPanel* m_paramsPanel = nullptr;
    QWidget* m_stepBar = nullptr;
    QList<QLabel*> m_stepLabels;
    QLabel* m_workflowStatus = nullptr;
    QProgressBar* m_inlineProgress = nullptr;
    ProcessingScene m_scene = ProcessingScene::Nightscape;
    bool m_sceneActive = false;
    QStringList m_sceneStepNames = {
        QString::fromUtf8("素材"), QString::fromUtf8("对齐"),
        QString::fromUtf8("堆栈"), QString::fromUtf8("结果")};
    QAction* m_beforeAfterAction = nullptr;
    ProcessingWorker* m_worker = nullptr;
    MaskPreviewWorker* m_maskPreviewWorker = nullptr;
    QSet<MaskPreviewWorker*> m_activeMaskPreviewWorkers;
    QuickPreviewWorker* m_quickPreviewWorker = nullptr;
    QTimer* m_quickPreviewTimer = nullptr;
    uint64_t m_quickPreviewGeneration = 0;
    bool m_quickPreviewPending = false;

    // 缓存最后一次堆栈结果（用于导出）
    std::vector<uint16_t> m_cachedStackedData;
    QImage m_cachedBeforePreview;
    uint16_t m_cachedBeforeBlackPoint = 0;
    uint16_t m_cachedBeforeWhitePoint = 65535;
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    int m_cachedFrameCount = 0;
    std::shared_ptr<const std::vector<uint16_t>> m_quickPreviewSource;
    std::shared_ptr<const std::vector<uint8_t>> m_quickPreviewMask;
    std::vector<uint16_t> m_cachedQuickPreviewResult;
    int m_quickPreviewWidth = 0;
    int m_quickPreviewHeight = 0;
    QString m_quickPreviewSignature;
    QString m_lastProcessedSignature;
    QString m_lastProcessedUpstreamSignature;
    QString m_runningParamsSignature;
    QString m_runningUpstreamSignature;
};

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication app(argc, argv);
    app.setApplicationName("StarProcessor");
    app.setApplicationVersion(STARPROCESSOR_VERSION);
    app.setOrganizationName("StarProcessor");
    app.setWindowIcon(UiAssets::appIcon());

    app.setStyleSheet(
        "QMainWindow, QDialog, QMessageBox, QFileDialog { background-color: #111719; color: #F3F7F6; }"
        "QWidget { color: #D2DDDA; letter-spacing: 0px; }"
        "QMenuBar { background-color: #171F21; color: #C7D3D0; border-bottom: 1px solid #263234; }"
        "QMenuBar::item { padding: 5px 11px; background-color: transparent; }"
        "QMenuBar::item:selected { background-color: #273336; color: #F3F7F6; }"
        "QStatusBar { background-color: #171F21; color: #81938F; }"
        "QSplitter::handle { background-color: #263234; }"
        "QScrollBar:vertical { background-color: #111719; width: 10px; }"
        "QScrollBar::handle:vertical { background-color: #344548; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background-color: #4D6265; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar:horizontal { background-color: #111719; height: 10px; }"
        "QScrollBar::handle:horizontal { background-color: #344548; border-radius: 5px; min-width: 24px; }"
        "QScrollBar::handle:horizontal:hover { background-color: #4D6265; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QToolTip { background-color: #273336; color: #F3F7F6; border: 1px solid #4D6265; "
        "  border-radius: 4px; padding: 5px 7px; }"
    );

    MainWindow window;
    window.show();

    for (const QString& argument : app.arguments()) {
        if (argument == "--scene=single") {
            window.selectStartupScene(ProcessingScene::SingleFrame);
        } else if (argument == "--scene=nightscape") {
            window.selectStartupScene(ProcessingScene::Nightscape);
        } else if (argument == "--scene=deep-sky") {
            window.selectStartupScene(ProcessingScene::DeepSky);
        } else if (argument == "--scene=sky-ground") {
            window.selectStartupScene(ProcessingScene::SkyGround);
        } else if (argument == "--scene=star-trail") {
            window.selectStartupScene(ProcessingScene::StarTrail);
        } else if (argument == "--scene=timelapse") {
            window.selectStartupScene(ProcessingScene::Timelapse);
        }
    }

    const QSet<QString> rawExtensions = {
        "nef", "cr2", "arw", "dng", "raw", "orf", "raf", "pef", "cr3"
    };
    QStringList startupFiles;
    for (const QString& argument : app.arguments().mid(1)) {
        if (argument.startsWith("--")) continue;
        const QFileInfo info(argument);
        if (info.isFile() && rawExtensions.contains(info.suffix().toLower())) {
            startupFiles.append(info.absoluteFilePath());
        }
    }
    window.importFiles(startupFiles);

    // Offscreen screenshot mode keeps visual regression checks reproducible
    // without requiring macOS screen-recording permission.
    for (const QString& argument : app.arguments()) {
        constexpr auto prefix = "--screenshot=";
        if (!argument.startsWith(prefix)) continue;
        const QString outputPath = argument.mid(QString(prefix).size());
        QTimer::singleShot(1500, &window, [&app, &window, outputPath]() {
            if (!window.grab().save(outputPath)) {
                qWarning() << "无法保存 UI 截图:" << outputPath;
                app.exit(2);
                return;
            }
            app.quit();
        });
        break;
    }

    return app.exec();
}

#include "main.moc"
