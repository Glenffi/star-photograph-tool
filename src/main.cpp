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
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QNetworkProxyFactory>
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
#include "ui/ParamsPanel.h"
#include "ui/Toolbar.h"
#include "ui/UiAssets.h"
#include "ui/ProcessingScene.h"
#include "ui/StyleTokens.h"
#include "ui/TaskStatusBar.h"
#include "update/UpdateManager.h"

#include "core/RawImageLoader.h"
#include "core/ImageExporter.h"
#include "workers/MaskPreviewWorker.h"
#include "workers/ExportWorker.h"
#include "workers/ProcessingWorker.h"
#include "workers/QuickPreviewWorker.h"
#include "workers/HistoryPreviewWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("StarProcessor — 星空摄影师 RAW 处理工具");
        resize(1400, 900);
        setMinimumSize(1180, 760);
        setAcceptDrops(true);

        setupCentralWidget();
        m_updateManager = new UpdateManager(this);
        setupMenuBar();
        setupStatusBar();
        setupConnections();

        QSettings settings("StarProcessor", "App");
        const int storedScene = settings.value(
            "ui/processingScene",
            static_cast<int>(ProcessingScene::Nightscape)).toInt();
        activateScene(static_cast<ProcessingScene>(std::clamp(storedScene, 0, 5)));

        connect(m_updateManager, &UpdateManager::statusMessage,
                this, [this](const QString& message, int timeoutMs) {
                    statusBar()->showMessage(message, timeoutMs);
                });
        m_updateManager->scheduleAutomaticCheck();

        statusBar()->showMessage("就绪 — 拖入 RAW 文件或点击导入开始");
    }

    void importFiles(const QStringList& paths) {
        if (m_projectPanel && !paths.isEmpty()) {
            if (!m_sceneActive) {
                activateScene(paths.size() == 1
                                  ? ProcessingScene::SingleFrame
                                  : ProcessingScene::Nightscape);
            }
            m_projectPanel->addFiles(paths);
        }
    }

    void selectStartupScene(ProcessingScene scene) {
        activateScene(scene);
    }

    void selectStartupInspectorOutput() {
        if (m_paramsPanel) m_paramsPanel->showOutputSettings();
    }

    void loadStartupHistory(const QString& path) {
        QTimer::singleShot(0, this, [this, path]() {
            loadRecentResultPreview(path);
        });
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

protected:
    void closeEvent(QCloseEvent* event) override {
        if (m_paramsPanel) m_paramsPanel->saveCurrentSettings();
        QMainWindow::closeEvent(event);
    }

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

    void keyPressEvent(QKeyEvent* event) override {
        QWidget* focused = QApplication::focusWidget();
        const bool editingText = qobject_cast<QLineEdit*>(focused) ||
            qobject_cast<QPlainTextEdit*>(focused) ||
            qobject_cast<QTextEdit*>(focused) ||
            qobject_cast<QAbstractSpinBox*>(focused) ||
            (qobject_cast<QComboBox*>(focused) &&
             qobject_cast<QComboBox*>(focused)->isEditable());
        const Qt::KeyboardModifiers modifiers = event->modifiers();
        const bool commandModifier = modifiers.testFlag(Qt::ControlModifier) ||
            modifiers.testFlag(Qt::MetaModifier) ||
            modifiers.testFlag(Qt::AltModifier);
        if (editingText || commandModifier) {
            QMainWindow::keyPressEvent(event);
            return;
        }

        bool handled = true;
        switch (event->key()) {
        case Qt::Key_F:
            m_previewPanel->fitToView();
            break;
        case Qt::Key_1:
            m_previewPanel->resetZoom();
            break;
        case Qt::Key_B:
            if (modifiers.testFlag(Qt::ShiftModifier)) {
                m_previewPanel->showSplitComparison();
            } else {
                m_previewPanel->cycleComparisonMode();
            }
            break;
        case Qt::Key_M:
            m_previewPanel->toggleMaskOverlay();
            break;
        case Qt::Key_R:
            showBestAvailableResult();
            break;
        case Qt::Key_G:
            m_previewPanel->beginPointSelection();
            break;
        case Qt::Key_BracketLeft:
            m_previewPanel->adjustMaskBrushSize(-4);
            break;
        case Qt::Key_BracketRight:
            m_previewPanel->adjustMaskBrushSize(4);
            break;
        case Qt::Key_Plus:
        case Qt::Key_Equal:
            m_previewPanel->zoomIn();
            break;
        case Qt::Key_Minus:
            m_previewPanel->zoomOut();
            break;
        case Qt::Key_Space:
            if (!processingActive()) m_projectPanel->toggleSelectedExclusion();
            break;
        case Qt::Key_Escape:
            m_previewPanel->cancelInteractiveMode();
            break;
        default:
            handled = false;
            break;
        }
        if (handled) {
            event->accept();
        } else {
            QMainWindow::keyPressEvent(event);
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

        // 主内容区：素材、预览、参数三栏。
        m_contentSplitter = new QSplitter(Qt::Horizontal, centralWidget);
        m_contentSplitter->setHandleWidth(1);

        m_projectPanel = new ProjectPanel(m_contentSplitter);
        m_projectPanel->setMinimumWidth(StyleTokens::Layout::kLeftPanelMinimumWidth);
        m_projectPanel->setMaximumWidth(StyleTokens::Layout::kLeftPanelMaximumWidth);
        m_contentSplitter->addWidget(m_projectPanel);

        m_previewPanel = new PreviewPanel(m_contentSplitter);
        m_contentSplitter->addWidget(m_previewPanel);

        m_paramsPanel = new ParamsPanel(m_contentSplitter);
        m_paramsPanel->setMinimumWidth(StyleTokens::Layout::kRightPanelMinimumWidth);
        m_paramsPanel->setMaximumWidth(StyleTokens::Layout::kRightPanelMaximumWidth);
        m_contentSplitter->addWidget(m_paramsPanel);

        QSettings settings("StarProcessor", "App");
        const int leftWidth = std::clamp(
            settings.value("ui/leftPanelWidth",
                           StyleTokens::Layout::kLeftPanelWidth).toInt(),
            StyleTokens::Layout::kLeftPanelMinimumWidth,
            StyleTokens::Layout::kLeftPanelMaximumWidth);
        const int rightWidth = std::clamp(
            settings.value("ui/rightPanelWidth",
                           StyleTokens::Layout::kRightPanelWidth).toInt(),
            StyleTokens::Layout::kRightPanelMinimumWidth,
            StyleTokens::Layout::kRightPanelMaximumWidth);
        m_contentSplitter->setSizes({leftWidth, 840, rightWidth});
        connect(m_contentSplitter, &QSplitter::splitterMoved,
                this, [this](int, int) {
                    const QList<int> sizes = m_contentSplitter->sizes();
                    if (sizes.size() != 3) return;
                    QSettings settings("StarProcessor", "App");
                    settings.setValue("ui/leftPanelWidth", sizes[0]);
                    settings.setValue("ui/rightPanelWidth", sizes[2]);
                });
        mainLayout->addWidget(m_contentSplitter, 1);
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

        if (changed) {
            m_paramsPanel->clearModifiedCameraGrayPoint();
            markCachedResultStale(
                QString::fromUtf8("场景已切换 · 这是上次处理的成片"));
            m_paramsPanel->applySceneProfile(scene);
        }
        m_projectPanel->setScene(scene);
        QSettings("StarProcessor", "App").setValue(
            "ui/processingScene", static_cast<int>(scene));
        updateProjectReadiness();
        statusBar()->showMessage(
            QString::fromUtf8("已进入%1流程").arg(sceneName()), 3000);
    }

    void updateProjectReadiness() {
        if (!m_sceneActive) return;
        const int count = m_projectPanel->includedFilePaths().size();
        const int required = requiredFrameCount();
        const bool calibrationReady = m_scene != ProcessingScene::DeepSky ||
            m_paramsPanel->deepSkyCalibrationInputsComplete();
        const auto calibrationSummary = [](const QStringList& paths,
                                           const QString& master) {
            if (!master.isEmpty()) return QStringLiteral("Master");
            if (!paths.isEmpty()) {
                return QString::fromUtf8("%1 张").arg(paths.size());
            }
            return QString::fromUtf8("未设置");
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
                "请补齐 Dark、Flat，以及 Bias 或同曝光 Dark Flat；也可导入 Master"));
        }
    }

    void setupMenuBar() {
        // 文件菜单
        auto* fileMenu = menuBar()->addMenu("文件");

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

        auto* removeAction = new QAction("移除所选", this);
        removeAction->setShortcut(QKeySequence::Delete);
        connect(removeAction, &QAction::triggered, this, [this]() {
            if (!processingActive()) m_projectPanel->removeSelected();
        });
        editMenu->addAction(removeAction);

        // 视图菜单
        auto* viewMenu = menuBar()->addMenu("视图");

        auto* cycleCompareAction = new QAction(
            QString::fromUtf8("循环对比\tB"), this);
        connect(cycleCompareAction, &QAction::triggered,
                m_previewPanel, &PreviewPanel::cycleComparisonMode);
        viewMenu->addAction(cycleCompareAction);

        m_beforeAfterAction = new QAction(
            QString::fromUtf8("处理前后分屏\tShift+B"), this);
        m_beforeAfterAction->setCheckable(true);
        m_beforeAfterAction->setEnabled(false);
        connect(m_beforeAfterAction, &QAction::toggled,
                m_previewPanel, &PreviewPanel::setBeforeAfterMode);
        viewMenu->addAction(m_beforeAfterAction);

        viewMenu->addSeparator();

        auto* fitViewAction = new QAction(QString::fromUtf8("适应视图\tF"), this);
        connect(fitViewAction, &QAction::triggered, m_previewPanel, &PreviewPanel::fitToView);
        viewMenu->addAction(fitViewAction);

        auto* actualPixelsAction = new QAction(
            QString::fromUtf8("实际像素 (1:1)\t1"), this);
        connect(actualPixelsAction, &QAction::triggered, m_previewPanel, &PreviewPanel::resetZoom);
        viewMenu->addAction(actualPixelsAction);

        auto* maskAction = new QAction(QString::fromUtf8("蒙版叠加\tM"), this);
        connect(maskAction, &QAction::triggered,
                m_previewPanel, &PreviewPanel::toggleMaskOverlay);
        viewMenu->addAction(maskAction);
        auto* resultAction = new QAction(QString::fromUtf8("返回当前结果\tR"), this);
        connect(resultAction, &QAction::triggered,
                this, &MainWindow::showBestAvailableResult);
        viewMenu->addAction(resultAction);

        // 处理菜单
        auto* processMenu = menuBar()->addMenu("处理");

        auto* startAction = new QAction("开始处理", this);
        startAction->setShortcut(QKeySequence("Ctrl+Return"));
        connect(startAction, &QAction::triggered, this, &MainWindow::onProcessClicked);
        processMenu->addAction(startAction);

        auto* exportAction = new QAction("导出结果", this);
        exportAction->setShortcut(QKeySequence("Ctrl+E"));
        connect(exportAction, &QAction::triggered, this, &MainWindow::onExportClicked);
        processMenu->addAction(exportAction);

        // 帮助菜单
        auto* helpMenu = menuBar()->addMenu("帮助");

        auto* updateAction = new QAction("检查更新...", this);
        connect(updateAction, &QAction::triggered, this, [this]() {
            m_updateManager->checkForUpdates(true);
        });
        helpMenu->addAction(updateAction);
        auto* shortcutsAction = new QAction(QString::fromUtf8("快捷键..."), this);
        connect(shortcutsAction, &QAction::triggered,
                this, &MainWindow::showShortcutsDialog);
        helpMenu->addAction(shortcutsAction);
        helpMenu->addSeparator();

        auto* aboutAction = new QAction("关于", this);
        connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutClicked);
        helpMenu->addAction(aboutAction);
    }

    void setupStatusBar() {
        m_taskStatusBar = new TaskStatusBar(this);
        setStatusBar(m_taskStatusBar);
        m_undoRemoveBtn = new QPushButton(statusBar());
        m_undoRemoveBtn->setProperty(
            StyleTokens::Properties::kVariant,
            StyleTokens::Properties::kGhost);
        m_undoRemoveBtn->setAccessibleName(QString::fromUtf8("撤销移除素材"));
        m_undoRemoveBtn->hide();
        statusBar()->addWidget(m_undoRemoveBtn);
        connect(m_undoRemoveBtn, &QPushButton::clicked, this, [this]() {
            m_projectPanel->undoLastRemoval();
        });
        m_pixelInfoLabel = new QLabel(QString::fromUtf8("RGB —"), statusBar());
        m_pixelInfoLabel->setProperty(
            StyleTokens::Properties::kTextRole,
            QString::fromLatin1(StyleTokens::Properties::kMono));
        m_pixelInfoLabel->setMinimumWidth(190);
        m_pixelInfoLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        statusBar()->addPermanentWidget(m_pixelInfoLabel);

        m_inlineProgress = m_taskStatusBar->progressBar();
    }

    void setWorkflowStage(int stage, const QString& status, bool complete = false) {
        Q_UNUSED(stage)
        statusBar()->showMessage(status, complete ? 5000 : 0);
    }

    void setupConnections() {
        m_quickPreviewTimer = new QTimer(this);
        m_quickPreviewTimer->setSingleShot(true);
        m_quickPreviewTimer->setInterval(400);
        connect(m_quickPreviewTimer, &QTimer::timeout,
                this, &MainWindow::startQuickPreview);

        // Toolbar 信号
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
        connect(m_toolbar, &Toolbar::checkUpdatesClicked, this, [this]() {
            m_updateManager->checkForUpdates(true);
        });
        connect(m_toolbar, &Toolbar::shortcutsClicked,
                this, &MainWindow::showShortcutsDialog);
        connect(m_toolbar, &Toolbar::aboutClicked, this, &MainWindow::onAboutClicked);
        connect(m_projectPanel, &ProjectPanel::sceneChanged,
                this, &MainWindow::activateScene);
        connect(m_projectPanel, &ProjectPanel::calibrationSettingsRequested,
                m_paramsPanel, &ParamsPanel::showCalibrationSettings);
        connect(m_projectPanel, &ProjectPanel::undoAvailabilityChanged,
                this, [this](bool available, const QString& fileName) {
                    if (!m_undoRemoveBtn) return;
                    m_undoRemoveBtn->setText(
                        available
                            ? QString::fromUtf8("撤销移除 · %1").arg(fileName)
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
                        m_pixelInfoLabel->setText(
                            QString::fromUtf8("%1×%2 · RGB —")
                                .arg(image.width()).arg(image.height()));
                        statusBar()->showMessage(
                            QString("已加载：%1")
                                .arg(QFileInfo(filePath).fileName()),
                            3000);
                    }
                });
        connect(m_previewPanel, &PreviewPanel::mousePixelInfo,
                this, [this](int x, int y, int r, int g, int b) {
                    m_pixelInfoLabel->setText(
                        QString::fromUtf8("%1,%2 · RGB %3,%4,%5")
                            .arg(x).arg(y).arg(r).arg(g).arg(b));
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
                QString::fromUtf8("素材已修改 · 这是上次处理的成片"));
            updateProjectReadiness();
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
                    ? QString::fromUtf8(
                          "参考帧已更新，请为新参考帧重新检测和修补地景")
                    : QString::fromUtf8("参考帧已更新"),
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
                                QString::fromUtf8(
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
                        QString::fromUtf8("正在根据笔迹贴合地景边缘..."));
                });
        connect(m_previewPanel, &PreviewPanel::maskRefinementFinished,
                this, [this](bool success) {
                    statusBar()->showMessage(
                        success
                            ? QString::fromUtf8("蒙版已贴合边缘，可继续修补或开始处理")
                            : QString::fromUtf8("蒙版精修失败，已保留上一次结果"),
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
                        m_maskOverlaySourceFile = maskSourceFile;
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
        return (m_worker && m_worker->isRunning()) ||
               (m_exportWorker && m_exportWorker->isRunning());
    }

    QString effectiveReferenceFrame() const {
        const QStringList files = m_projectPanel->includedFilePaths();
        QString reference = m_paramsPanel->selectedReferenceFrame();
        if (reference.isEmpty() || !files.contains(reference)) {
            reference = m_projectPanel->referenceFramePath();
        }
        return files.contains(reference) ? reference : QString();
    }

    bool discardEditedMaskForDifferentReference() {
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

    QString currentUpstreamSignature() const {
        // ParamsPanel owns the algorithm settings, while ProjectPanel owns the
        // context-menu reference choice. Sign the effective value used by the
        // worker so cache/export validity follows both controls.
        return m_paramsPanel->upstreamSignature() +
            QStringLiteral("|effective-reference=") + effectiveReferenceFrame() +
            QStringLiteral("|edited-mask-source=") + m_editedMaskSourceFile +
            QStringLiteral("|edited-mask-revision=") +
            QString::number(m_editedMaskRevision);
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
            if (m_previewPanel->isShowingResult()) {
                m_previewPanel->setResultLabel(
                    QString::fromUtf8("参数已修改 · 这是上次处理的成片"));
            }
            if (currentQuickPreviewAvailable()) {
                setWorkflowStage(3, QString::fromUtf8(
                    "快速预览已更新 · 完整导出需重新处理"));
            } else {
                scheduleQuickPreview();
            }
        } else {
            cancelQuickPreview(false);
            if (m_previewPanel->isShowingResult()) {
                m_previewPanel->setResultLabel(
                    QString::fromUtf8("参数已修改 · 这是上次处理的成片"));
            }
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
        options.basicAdjustments =
            m_paramsPanel->basicAdjustmentOptions();
        options.skyGroundSeparation =
            (m_scene == ProcessingScene::SkyGround &&
             m_paramsPanel->skyGroundSeparationEnabled()) ||
            (m_scene == ProcessingScene::StarTrail &&
             m_paramsPanel->starTrailProtectGround());
        options.groundDetailStrength =
            m_scene == ProcessingScene::SkyGround &&
                options.skyGroundSeparation
                ? m_paramsPanel->groundDetailStrength() : 0;
        options.starDefringeEnabled =
            m_paramsPanel->starDefringeEnabled();
        options.starDefringeStrength =
            m_paramsPanel->starDefringeStrength();
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
            const QString contentKey = QStringLiteral("quick:") +
                m_quickPreviewSignature;
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
        const QString contentKey = QStringLiteral("result:") +
            m_lastProcessedSignature;
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
                QString::fromUtf8("参数已修改 · 这是上次处理的成片"));
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
                            const StarReductionStats& starStats =
                                worker->starReductionStats();
                            if (m_paramsPanel->starReduceEnabled()) {
                                statusBar()->showMessage(
                                    QString::fromUtf8(
                                        "快速预览已更新 · 缩星处理 %1 颗，"
                                        "请在 100% 下检查")
                                        .arg(starStats.processedStars),
                                    4000);
                            } else if (m_paramsPanel->starDefringeEnabled()) {
                                statusBar()->showMessage(
                                    QString::fromUtf8(
                                        "快速预览已更新 · 去紫边修正 %1 个星缘像素")
                                        .arg(worker->starDefringeStats()
                                                 .defringedPixels),
                                    4000);
                            } else {
                                statusBar()->showMessage(
                                    QString::fromUtf8("快速预览已更新"), 2500);
                            }
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

    void markCachedResultStale(const QString& reason) {
        cancelQuickPreview(false);
        m_quickPreviewSource.reset();
        m_quickPreviewMask.reset();
        m_cachedQuickPreviewResult.clear();
        m_cachedQuickPreviewResult.shrink_to_fit();
        m_quickPreviewWidth = 0;
        m_quickPreviewHeight = 0;
        m_quickPreviewSignature.clear();
        if (m_toolbar) m_toolbar->enableExport(false);
        if (!m_previewPanel || m_cachedStackedData.empty()) return;
        const bool viewingResult = m_previewPanel->isShowingResult();
        m_previewPanel->setResultAvailable(true, viewingResult);
        if (viewingResult) m_previewPanel->setResultLabel(reason);
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
                if (!m_sceneActive) activateScene(ProcessingScene::Nightscape);
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
        if (!loader.loadMetadata(filePath, metadata)) {
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
            "StarProcessor\n"
            "版本 " STARPROCESSOR_VERSION "\n"
            "MIT License"
        );
    }

    void onProcessClicked() {
        if (m_worker && m_worker->isRunning()) {
            m_worker->requestCancel();
            setWorkflowStage(1, QString::fromUtf8("正在安全停止..."));
            return;
        }
        if (m_exportWorker && m_exportWorker->isRunning()) {
            m_exportWorker->requestCancel();
            setWorkflowStage(3, QString::fromUtf8("正在取消导出..."));
            return;
        }
        if (m_previewPanel->maskRefinementActive()) {
            statusBar()->showMessage(
                QString::fromUtf8("请等待地景蒙版精修完成"), 3000);
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
            !m_paramsPanel->deepSkyCalibrationInputsComplete()) {
            QMessageBox::warning(
                this, QString::fromUtf8("深空校准来源不完整"),
                QString::fromUtf8(
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
                    this, QString::fromUtf8("校准素材重复"),
                    QString::fromUtf8(
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

        QString refFrame = m_paramsPanel->selectedReferenceFrame();
        if (refFrame.isEmpty() || !files.contains(refFrame)) {
            refFrame = m_projectPanel->referenceFramePath();
        }
        if (!m_editedSkyGroundMask.empty()) {
            if (refFrame.isEmpty()) {
                // The edited mask is expressed in one source frame's
                // coordinates. Pin automatic selection to that frame so the
                // previewed horizon and formal blend use the same geometry.
                refFrame = m_editedMaskSourceFile;
            } else if (refFrame != m_editedMaskSourceFile) {
                QMessageBox::warning(
                    this, QString::fromUtf8("地景蒙版参考帧已变化"),
                    QString::fromUtf8(
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
        params.basicAdjustments =
            m_paramsPanel->basicAdjustmentOptions();
        params.starDefringeEnabled = !params.starTrailMode &&
            m_paramsPanel->starDefringeEnabled();
        params.starDefringeStrength = m_paramsPanel->starDefringeStrength();
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
        m_projectPanel->setEditingEnabled(false);
        m_paramsPanel->setEnabled(false);
        m_inlineProgress->setValue(0);
        m_inlineProgress->setVisible(true);
        setWorkflowStage(
            m_scene == ProcessingScene::DeepSky ? 0 : 1,
            m_scene == ProcessingScene::DeepSky
                ? QString::fromUtf8("正在准备 Bayer 校准")
                : QString::fromUtf8("正在准备处理"));

        // 创建后台线程。进度与取消都留在主窗口，不再弹出第二个窗口。
        auto* worker = new ProcessingWorker(files, refFrame, params, this);
        m_worker = worker;
        connect(worker, &ProcessingWorker::progress, m_inlineProgress, &QProgressBar::setValue);
        connect(worker, &ProcessingWorker::stageMessage, this, [this](const QString& msg) {
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
        connect(worker, &ProcessingWorker::finished, this, [this, worker]() {
            m_toolbar->setProcessing(false);
            m_projectPanel->setEditingEnabled(true);
            m_paramsPanel->setEnabled(true);
            m_inlineProgress->setVisible(false);
            if (worker->wasCancelled()) {
                setWorkflowStage(1, QString::fromUtf8("处理已取消，可调整参数后重试"));
                statusBar()->showMessage("处理已取消", 3000);
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
                m_lastProcessedSignature = m_runningParamsSignature;
                m_lastProcessedUpstreamSignature =
                    m_runningUpstreamSignature;

                showBestAvailableResult();
                m_toolbar->enableExport(
                    m_scene != ProcessingScene::Timelapse);
                int frameCount = m_cachedFrameCount;
                const QString referenceName = QFileInfo(
                    worker->selectedReferenceFrame()).fileName();
                const int rejectedCount = worker->qualityRejectedFiles().size();
                const int skippedCount = static_cast<int>(
                    worker->skippedFrames().size());
                QStringList reportLines;
                reportLines << QString::fromUtf8("输出：%1×%2，%3 帧")
                                   .arg(m_cachedWidth).arg(m_cachedHeight)
                                   .arg(frameCount);
                if (m_scene != ProcessingScene::SingleFrame &&
                    m_scene != ProcessingScene::Timelapse &&
                    m_scene != ProcessingScene::StarTrail) {
                    reportLines << QString::fromUtf8(
                        "对齐：RMS %1 px，P95 %2 px，最低网格覆盖 %3%")
                        .arg(worker->averageAlignmentRms(), 0, 'f', 2)
                        .arg(worker->worstAlignmentP95(), 0, 'f', 2)
                        .arg(worker->minimumAlignmentGridCoverage() * 100.0,
                             0, 'f', 0);
                    reportLines << QString::fromUtf8(
                        "模型：Affine %1 帧，Homography %2 帧")
                        .arg(worker->affineFrameCount())
                        .arg(worker->homographyFrameCount());
                }
                reportLines << QString::fromUtf8(
                    "帧筛选：质量排除 %1，处理跳过 %2")
                    .arg(rejectedCount).arg(skippedCount);
                if (worker->photometricNormalizedFrameCount() > 0) {
                    reportLines << QString::fromUtf8(
                        "光度归一化：%1 帧，平均增益 %2，范围 %3–%4")
                        .arg(worker->photometricNormalizedFrameCount())
                        .arg(worker->averagePhotometricGain(), 0, 'f', 3)
                        .arg(worker->minimumPhotometricGain(), 0, 'f', 3)
                        .arg(worker->maximumPhotometricGain(), 0, 'f', 3);
                }
                if (m_scene == ProcessingScene::SkyGround) {
                    reportLines << QString::fromUtf8(
                        "天地蒙版：天空占比 %1%，来源 %2")
                        .arg(worker->skyGroundSkyFraction() * 100.0, 0, 'f', 1)
                        .arg(worker->skyGroundMaskSource());
                }
                reportLines << QString::fromUtf8("堆栈耗时：%1 s")
                                   .arg(worker->stackingElapsedMs() / 1000.0,
                                        0, 'f', 2);
                if (!worker->outputFile().isEmpty()) {
                    reportLines << QString::fromUtf8("文件：%1")
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
                    (QString::fromUtf8("处理完成 · %1 帧 · %2×%3")
                        .arg(frameCount).arg(m_cachedWidth).arg(m_cachedHeight) +
                     (elongatedStars
                          ? QString::fromUtf8(" · 星点偏长，请检查曝光拖线/像差")
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
                    QString::fromUtf8("%1×%2 · %3 帧 · %4")
                        .arg(m_cachedWidth).arg(m_cachedHeight)
                        .arg(frameCount).arg(methodName));
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
                const QStringList calibrationWarnings =
                    worker->calibrationPreflightWarnings();
                const QStringList generatedMasters =
                    worker->generatedMasterFiles();
                if (!generatedMasters.isEmpty()) {
                    statusBar()->showMessage(
                        QString::fromUtf8(
                            "处理完成，已保存 %1 个 Master 到 %2")
                            .arg(generatedMasters.size())
                            .arg(QFileInfo(generatedMasters.first())
                                     .absolutePath()),
                        7000);
                }
                if (m_scene == ProcessingScene::DeepSky &&
                    !calibrationWarnings.isEmpty()) {
                    auto* advice = new QMessageBox(
                        QMessageBox::Information,
                        QString::fromUtf8("校准素材建议"),
                        QString::fromUtf8(
                            "处理已经完成，但校准素材还有可改进之处。"),
                        QMessageBox::Ok, this);
                    advice->setInformativeText(
                        QString::fromUtf8("• ") +
                        calibrationWarnings.join(QString::fromUtf8("\n• ")));
                    advice->setAttribute(Qt::WA_DeleteOnClose);
                    advice->open();
                }
            } else {
                QMessageBox::warning(this, "处理失败", worker->errorString());
                setWorkflowStage(1, QString::fromUtf8("处理失败，请检查素材或参数"));
                statusBar()->showMessage("处理失败", 3000);
            }
            worker->deleteLater();
            if (m_worker == worker) m_worker = nullptr;
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

        auto image = std::make_shared<std::vector<uint16_t>>(
            std::move(m_cachedStackedData));
        auto* worker = new ExportWorker(
            image, m_cachedWidth, m_cachedHeight, fullPath, fmt, this);
        m_exportWorker = worker;
        m_projectPanel->setEditingEnabled(false);
        m_paramsPanel->setEnabled(false);
        m_toolbar->setProcessing(true);
        m_inlineProgress->setRange(0, 100);
        m_inlineProgress->setVisible(false);
        statusBar()->showMessage(QString::fromUtf8("正在导出 %1...").arg(fileName));

        connect(worker, &ExportWorker::finished, this,
                [this, worker, image, fileName]() {
                    m_cachedStackedData = std::move(*image);
                    m_toolbar->setProcessing(false);
                    m_toolbar->enableExport(true);
                    m_projectPanel->setEditingEnabled(true);
                    m_paramsPanel->setEnabled(true);
                    m_inlineProgress->setRange(0, 100);
                    m_inlineProgress->setVisible(false);
                    if (worker->succeeded()) {
                        m_paramsPanel->refreshRecentResults();
                        QMessageBox::information(
                            this, QString::fromUtf8("导出成功"),
                            QString::fromUtf8("已导出到：%1")
                                .arg(worker->outputPath()));
                        statusBar()->showMessage(
                            QString::fromUtf8("已导出：%1").arg(fileName), 5000);
                    } else if (worker->wasCancelled()) {
                        statusBar()->showMessage(QString::fromUtf8("导出已取消"), 3000);
                    } else {
                        QMessageBox::warning(
                            this, QString::fromUtf8("导出失败"),
                            QString::fromUtf8("无法写入文件，请检查输出目录权限"));
                    }
                    if (m_exportWorker == worker) m_exportWorker = nullptr;
                    worker->deleteLater();
                });
        worker->start();
    }

    void onSettingsClicked() {
        auto* dialog = new QDialog(this);
        dialog->setWindowTitle("设置");
        dialog->setFixedSize(480, 160);

        auto* layout = new QVBoxLayout(dialog);
        layout->setSpacing(16);
        layout->setContentsMargins(20, 20, 20, 20);

        auto* title = new QLabel(QString::fromUtf8("应用设置"), dialog);
        title->setProperty(
            StyleTokens::Properties::kTextRole,
            StyleTokens::Properties::kTitle);
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
            UiAssets::icon(
                UiAssets::Glyph::Folder,
                StyleTokens::Colors::fromHex(
                    StyleTokens::Colors::kTextSecondary)));
        outDirBtn->setToolTip(QString::fromUtf8("选择输出目录"));
        outDirBtn->setFixedSize(28, 28);
        outDirBtn->setProperty(
            StyleTokens::Properties::kVariant,
            StyleTokens::Properties::kIcon);
        connect(outDirBtn, &QPushButton::clicked, this, [outDirEdit]() {
            QString dir = QFileDialog::getExistingDirectory(nullptr, "选择输出目录");
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
        auto* okBtn = new QPushButton("确定", dialog);
        okBtn->setProperty(
            StyleTokens::Properties::kVariant,
            StyleTokens::Properties::kPrimary);
        connect(okBtn, &QPushButton::clicked, dialog, [this, dialog, outDirEdit]() {
            QSettings s("StarProcessor", "App");
            s.setValue("outputPath", outDirEdit->text());
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

    void showShortcutsDialog() {
        QMessageBox::information(
            this, QString::fromUtf8("快捷键"),
            QString::fromUtf8(
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

    void loadRecentResultPreview(const QString& path) {
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        if (m_historyPreviewWorker && m_historyPreviewWorker->isRunning()) {
            statusBar()->showMessage(
                QString::fromUtf8("正在载入另一张历史结果，请稍候"), 3000);
            return;
        }
        auto* worker = new HistoryPreviewWorker(path, this);
        m_historyPreviewWorker = worker;
        statusBar()->showMessage(
            QString::fromUtf8("正在载入历史结果：%1")
                .arg(QFileInfo(path).fileName()));
        connect(worker, &HistoryPreviewWorker::loaded, this,
                [this, worker](const QString& loadedPath,
                               const QImage& preview) {
                    if (m_historyPreviewWorker != worker) return;
                    m_previewPanel->loadImage(
                        preview, QStringLiteral("history:") + loadedPath);
                    m_previewPanel->setResultAvailable(
                        !m_cachedStackedData.empty(), false);
                    m_toolbar->enableExport(false);
                    statusBar()->showMessage(
                        QString::fromUtf8("历史结果只读预览 · %1")
                            .arg(QFileInfo(loadedPath).fileName()), 5000);
                });
        connect(worker, &HistoryPreviewWorker::failed, this,
                [this, worker](const QString& failedPath,
                               const QString& reason) {
                    if (m_historyPreviewWorker != worker) return;
                    statusBar()->showMessage(
                        QString::fromUtf8("历史结果载入失败：%1 · %2")
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

private:
    Toolbar* m_toolbar = nullptr;
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
    // This path deliberately avoids QApplication. The packaging job uses it
    // to make dyld load the completed bundle without requiring a GUI session.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--runtime-check") == 0) {
            std::cout << "StarProcessor " << STARPROCESSOR_VERSION << '\n';
            return 0;
        }
    }

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication app(argc, argv);
    app.setApplicationName("StarProcessor");
    app.setApplicationVersion(STARPROCESSOR_VERSION);
    app.setOrganizationName("StarProcessor");
    app.setWindowIcon(UiAssets::appIcon());

    // QNetworkAccessManager otherwise defaults to NoProxy. Respect the
    // operating system proxy/PAC configuration for update checks and package
    // downloads without storing proxy credentials in the application.
    QNetworkProxyFactory::setUseSystemConfiguration(true);

    app.setStyleSheet(StyleTokens::appStyleSheet());

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
        } else if (argument == "--inspector=output") {
            window.selectStartupInspectorOutput();
        } else if (argument.startsWith("--history=")) {
            window.loadStartupHistory(
                argument.mid(QStringLiteral("--history=").size()));
        } else if (argument.startsWith("--window-size=")) {
            const QStringList dimensions = argument.mid(
                QStringLiteral("--window-size=").size()).split('x');
            if (dimensions.size() == 2) {
                bool widthOk = false;
                bool heightOk = false;
                const int width = dimensions[0].toInt(&widthOk);
                const int height = dimensions[1].toInt(&heightOk);
                if (widthOk && heightOk && width > 0 && height > 0) {
                    window.resize(width, height);
                }
            }
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
