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
#include <QToolButton>
#include <QButtonGroup>
#include <QStyle>
#include <QStyleFactory>
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
#include "ui/ProjectPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/ParamsPanel.h"
#include "ui/Toolbar.h"

#include "core/RawImageLoader.h"
#include "core/ImageExporter.h"
#include "workers/MaskPreviewWorker.h"
#include "workers/ProcessingWorker.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("StarProcessor — 星空摄影师 RAW 处理工具");
        resize(1400, 900);
        setAcceptDrops(true);

        setupCentralWidget();
        setupMenuBar();
        setupStatusBar();
        setupStepBar();
        setupConnections();

        statusBar()->showMessage("就绪 — 拖入 RAW 文件或点击导入开始");
    }

    ~MainWindow() {
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
        if (event->mimeData()->hasUrls()) {
            event->acceptProposedAction();
        }
    }

    void dropEvent(QDropEvent* event) override {
        const QMimeData* mimeData = event->mimeData();
        if (!mimeData->hasUrls()) return;

        QStringList filePaths;
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                filePaths.append(url.toLocalFile());
            }
        }

        if (!filePaths.isEmpty()) {
            m_projectPanel->addFiles(filePaths);
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

        // 主内容区：三栏布局
        auto* contentSplitter = new QSplitter(Qt::Horizontal, this);
        contentSplitter->setHandleWidth(2);
        contentSplitter->setStyleSheet(
            "QSplitter::handle { background-color: #30363D; }"
        );

        // 左侧面板：ProjectPanel（卡片式文件列表）
        m_projectPanel = new ProjectPanel(this);
        m_projectPanel->setMinimumWidth(240);
        m_projectPanel->setMaximumWidth(420);
        contentSplitter->addWidget(m_projectPanel);

        // 中央面板：PreviewPanel（QLabel + QScrollArea）
        m_previewPanel = new PreviewPanel(this);
        contentSplitter->addWidget(m_previewPanel);

        // 右侧面板：ParamsPanel（实际参数面板）
        m_paramsPanel = new ParamsPanel(this);
        m_paramsPanel->setMinimumWidth(240);
        m_paramsPanel->setMaximumWidth(420);
        contentSplitter->addWidget(m_paramsPanel);

        // 设置默认比例：22% / 56% / 22%
        contentSplitter->setSizes({308, 784, 308});

        mainLayout->addWidget(contentSplitter, 1);

        // 步骤条（底部）
        m_stepBar = new QWidget(this);
        m_stepBar->setFixedHeight(48);
        m_stepBar->setStyleSheet(
            "QWidget { background-color: #161B22; border-top: 1px solid #30363D; }"
        );
        auto* stepLayout = new QHBoxLayout(m_stepBar);
        stepLayout->setContentsMargins(16, 4, 16, 4);
        stepLayout->setSpacing(8);

        m_stepGroup = new QButtonGroup(this);
        m_stepGroup->setExclusive(true);

        QStringList steps = {"1. 导入", "2. 对齐", "3. 堆栈", "4. 导出"};
        for (int i = 0; i < steps.size(); ++i) {
            auto* btn = new QToolButton(m_stepBar);
            btn->setText(steps[i]);
            btn->setCheckable(true);
            btn->setChecked(i == 0);
            btn->setMinimumWidth(120);
            btn->setStyleSheet(
                "QToolButton {"
                "  background-color: #21262D;"
                "  color: #8B949E;"
                "  border: 1px solid #30363D;"
                "  border-radius: 6px;"
                "  padding: 6px 16px;"
                "  font-size: 13px;"
                "}"
                "QToolButton:checked {"
                "  background-color: #F0B90B;"
                "  color: #0D1117;"
                "  border: 1px solid #F0B90B;"
                "  font-weight: bold;"
                "}"
                "QToolButton:hover:!checked {"
                "  background-color: #30363D;"
                "  color: #E6EDF3;"
                "}"
            );
            m_stepGroup->addButton(btn, i);
            stepLayout->addWidget(btn);
        }
        stepLayout->addStretch();

        mainLayout->addWidget(m_stepBar);
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
            m_projectPanel->clearFiles();
            m_previewPanel->clearImage();
            m_toolbar->enableProcess(false);
            m_toolbar->enableExport(false);
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
        connect(removeAction, &QAction::triggered, m_projectPanel, &ProjectPanel::removeSelected);
        editMenu->addAction(removeAction);

        // 视图菜单
        auto* viewMenu = menuBar()->addMenu("视图");
        viewMenu->setStyleSheet(menuStyleSheet());

        auto* beforeAfterAction = new QAction("Before/After 对比", this);
        beforeAfterAction->setCheckable(true);
        connect(beforeAfterAction, &QAction::toggled, m_previewPanel, &PreviewPanel::setBeforeAfterMode);
        viewMenu->addAction(beforeAfterAction);

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
            "QStatusBar { background-color: #161B22; color: #8B949E; border-top: 1px solid #30363D; }"
        );
        m_mouseStatusLabel = new QLabel(this);
        m_mouseStatusLabel->setStyleSheet("color: #8B949E; padding: 0 8px; font-size: 11px;");
        statusBar()->addPermanentWidget(m_mouseStatusLabel);
    }

    void setupStepBar() {
        connect(m_stepGroup, QOverload<int>::of(&QButtonGroup::idClicked), this, [this](int step) {
            QString stepName;
            switch (step) {
                case 0: stepName = "导入"; break;
                case 1: stepName = "对齐"; break;
                case 2: stepName = "堆栈"; break;
                case 3: stepName = "导出"; break;
            }
            statusBar()->showMessage(QString("当前步骤：%1").arg(stepName), 3000);
        });
    }

    void setupConnections() {
        // Toolbar 信号
        connect(m_toolbar, &Toolbar::importFilesClicked, this, &MainWindow::onImportClicked);
        connect(m_toolbar, &Toolbar::importFolderClicked, this, &MainWindow::onImportFolderClicked);
        connect(m_toolbar, &Toolbar::clearProjectClicked, this, [this]() {
            m_projectPanel->clearFiles();
            m_previewPanel->clearImage();
            m_toolbar->enableProcess(false);
            m_toolbar->enableExport(false);
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
            statusBar()->showMessage(QString("已加载：%1").arg(QFileInfo(filePath).fileName()), 3000);
        });

        // 元数据请求
        connect(m_projectPanel, &ProjectPanel::requestMetadata, this, [this](const QString& filePath) {
            onViewMetadata(filePath);
        });

        // 预览区空状态导入按钮
        connect(m_previewPanel, &PreviewPanel::importRequested, this, &MainWindow::onImportClicked);

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
            int count = m_projectPanel->includedFilePaths().size();
            m_toolbar->enableProcess(count >= 2);
            if (count < 2) m_toolbar->enableExport(false);
            m_paramsPanel->updateRefFrameList(m_projectPanel->includedFilePaths());
            if (count >= 2) {
                m_paramsPanel->recommendStackMethod(count);
            }
        });

        // 参考帧变化
        connect(m_projectPanel, &ProjectPanel::referenceFrameChanged, this, [this]() {
            statusBar()->showMessage("参考帧已更新", 2000);
        });

        // 鼠标像素信息 -> 状态栏永久显示
        connect(m_previewPanel, &PreviewPanel::mousePixelInfo, this, [this](int x, int y, int r, int g, int b) {
            if (m_mouseStatusLabel) {
                m_mouseStatusLabel->setText(
                    QString("坐标: (%1, %2) | RGB: (%3, %4, %5)").arg(x).arg(y).arg(r).arg(g).arg(b)
                );
            }
        });

        // 参数变化
        connect(m_previewPanel, &PreviewPanel::mousePixelInfo, this, [](int x, int y, int r, int g, int b) {
            Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(r) Q_UNUSED(g) Q_UNUSED(b)
        });

        // 参数变化
        connect(m_paramsPanel, &ParamsPanel::paramsChanged, this, [this]() {
            statusBar()->showMessage("参数已更新", 2000);
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
            MaskPreviewWorker* worker = new MaskPreviewWorker(currentFile, feather, this);
            m_maskPreviewWorker = worker;
            m_activeMaskPreviewWorkers.insert(worker);
            connect(worker, &MaskPreviewWorker::finished, this, [this, worker]() {
                m_activeMaskPreviewWorkers.remove(worker);
                if (worker->errorString().isEmpty()) {
                    // 只处理最新 worker 的结果
                    if (worker == m_maskPreviewWorker) {
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
        return "QMenu { background-color: #161B22; color: #E6EDF3; border: 1px solid #30363D; padding: 4px; }"
               "QMenu::item { padding: 6px 20px; border-radius: 4px; }"
               "QMenu::item:selected { background-color: #30363D; }"
               "QMenu::separator { height: 1px; background-color: #30363D; margin: 4px 8px; }";
    }

private slots:
    void onImportClicked() {
        QStringList fileNames = QFileDialog::getOpenFileNames(
            this,
            "选择 RAW 文件",
            QString(),
            "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef *.cr3);;所有文件 (*)"
        );

        if (!fileNames.isEmpty()) {
            m_projectPanel->addFiles(fileNames);
            statusBar()->showMessage(
                QString("已导入 %1 个文件").arg(fileNames.size()),
                5000
            );
        }
    }

    void onImportFolderClicked() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择包含 RAW 文件的文件夹");
        if (!dir.isEmpty()) {
            QDir directory(dir);
            QStringList filters;
            filters << "*.nef" << "*.cr2" << "*.arw" << "*.dng" << "*.raw" << "*.orf" << "*.raf" << "*.pef" << "*.cr3";
            directory.setNameFilters(filters);
            QStringList files = directory.entryList(QDir::Files);
            QStringList fullPaths;
            for (const QString& f : files) {
                fullPaths.append(directory.absoluteFilePath(f));
            }
            if (!fullPaths.isEmpty()) {
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
         .arg(metadata.exposureTime > 0 ? QString("1/%1s").arg(qRound(1.0 / metadata.exposureTime)) : "—")
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
            "<p><b>版本：</b>0.4.0</p>"
            "<p><b>阶段：</b>P2 — 堆栈降噪与自动优化</p>"
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
        if (files.size() < 2) {
            QMessageBox::warning(this, "处理", "需要至少 2 张未排除的图像才能开始处理");
            return;
        }

        // 保存当前参数设置
        m_paramsPanel->saveCurrentSettings();

        QString refFrame = m_projectPanel->referenceFramePath();
        if (refFrame.isEmpty()) {
            // 自动选择第一个为参考帧
            refFrame = files.first();
            m_projectPanel->setReferenceFrame(refFrame);
        }

        // 2. 构建参数
        ProcessingWorker::Params params;
        params.stackMethod = m_paramsPanel->stackMethod();
        params.kappaValue = m_paramsPanel->kappaValue();
        params.dewarpEnabled = m_paramsPanel->dewarpEnabled();
        params.dewarpStrength = m_paramsPanel->dewarpStrength();
        params.stretchEnabled = m_paramsPanel->stretchEnabled();
        params.starReduceEnabled = m_paramsPanel->starReduceEnabled();
        params.starReduceStrength = m_paramsPanel->starReduceStrength();
        params.outputFormat = m_paramsPanel->outputFormat();
        params.outputPath = m_paramsPanel->outputPath();
        params.skyGroundSepEnabled = m_paramsPanel->skyGroundSeparationEnabled();
        params.skyGroundMode = m_paramsPanel->skyGroundMode();
        params.userMaskPath = m_paramsPanel->userMaskPath();
        params.featherRadius = m_paramsPanel->featherRadius();

        // 3. 创建进度对话框
        auto* dialog = new QProgressDialog(this);
        dialog->setWindowTitle("处理中...");
        dialog->setLabelText("初始化...");
        dialog->setRange(0, 100);
        dialog->setValue(0);
        dialog->setMinimumDuration(0);
        dialog->setCancelButtonText("取消");
        dialog->setStyleSheet(
            "QProgressDialog { background-color: #161B22; color: #E6EDF3; }"
            "QLabel { color: #E6EDF3; }"
            "QProgressBar { border: 1px solid #30363D; background-color: #21262D; color: #E6EDF3; }"
            "QProgressBar::chunk { background-color: #F0B90B; }"
            "QPushButton { background-color: #21262D; color: #E6EDF3; border: 1px solid #30363D; }"
        );

        // 4. 创建后台线程
        auto* worker = new ProcessingWorker(files, refFrame, params, this);
        m_worker = worker;
        connect(worker, &ProcessingWorker::progress, dialog, &QProgressDialog::setValue);
        connect(worker, &ProcessingWorker::stageMessage, dialog, [dialog](const QString& msg) {
            dialog->setLabelText(msg);
        });
        connect(worker, &ProcessingWorker::finished, this, [this, dialog, worker]() {
            dialog->close();
            dialog->deleteLater();
            if (worker->wasCancelled()) {
                statusBar()->showMessage("处理已取消", 3000);
            } else if (worker->errorString().isEmpty()) {
                // 成功：缓存堆栈结果
                m_cachedStackedData = worker->takeStackedData();
                m_cachedWidth = worker->stackedWidth();
                m_cachedHeight = worker->stackedHeight();
                m_cachedFrameCount = worker->stackedFrameCount();

                // 自动保存到缓存目录
                QSettings settings("StarProcessor", "App");
                QString cacheDir = settings.value(
                    "cacheDir", QDir::homePath() + "/StarProcessor/Cache").toString();
                QDir().mkpath(cacheDir);
                QString cacheFile = cacheDir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_cached.tiff";
                if (!ImageExporter::exportRgb16(m_cachedStackedData, m_cachedWidth, m_cachedHeight, cacheFile.toStdString())) {
                    qWarning() << "缓存 TIFF 写入失败:" << cacheFile;
                }

                m_previewPanel->loadRgb16BitImage(m_cachedStackedData, m_cachedWidth, m_cachedHeight);
                m_toolbar->enableExport(true);
                int frameCount = m_cachedFrameCount;
                statusBar()->showMessage(
                    QString("处理完成 — %1×%2 已堆栈 %3 帧")
                        .arg(m_cachedWidth)
                        .arg(m_cachedHeight)
                        .arg(frameCount),
                    5000
                );
            } else {
                QMessageBox::warning(this, "处理失败", worker->errorString());
                statusBar()->showMessage("处理失败", 3000);
            }
            worker->deleteLater();
            if (m_worker == worker) m_worker = nullptr;
        });
        connect(dialog, &QProgressDialog::canceled, this, [this]() {
            if (m_worker) {
                m_worker->requestCancel();
                statusBar()->showMessage("处理已取消", 3000);
            }
        });

        worker->start();
    }

    void onExportClicked() {
        if (m_cachedStackedData.empty()) {
            QMessageBox::warning(this, "导出", "没有可用的堆栈结果，请先完成处理");
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

        QString fileName = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + "_stacked_export" + ext;
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
            "QDialog { background-color: #161B22; color: #E6EDF3; }"
            "QLabel { color: #C9D1D9; background-color: transparent; }"
            "QLineEdit { background-color: #21262D; color: #E6EDF3; "
            "  border: 1px solid #30363D; border-radius: 4px; padding: 4px 8px; }"
            "QPushButton { background-color: #21262D; color: #E6EDF3; "
            "  border: 1px solid #30363D; border-radius: 4px; padding: 6px 16px; }"
            "QPushButton:hover { background-color: #30363D; }"
            "QComboBox { background-color: #21262D; color: #E6EDF3; "
            "  border: 1px solid #30363D; border-radius: 4px; padding: 4px 8px; }"
        );

        auto* layout = new QVBoxLayout(dialog);
        layout->setSpacing(16);
        layout->setContentsMargins(20, 20, 20, 20);

        auto* title = new QLabel("⚙️ 应用设置", dialog);
        title->setStyleSheet("font-size: 16px; font-weight: bold; color: #E6EDF3;");
        layout->addWidget(title);

        // 输出目录
        auto* outDirRow = new QHBoxLayout();
        auto* outDirLabel = new QLabel("输出目录:", dialog);
        QSettings settings("StarProcessor", "App");
        QString defaultOutDir = settings.value("outputPath", QDir::homePath() + "/StarProcessor/Output").toString();
        auto* outDirEdit = new QLineEdit(defaultOutDir, dialog);
        outDirEdit->setReadOnly(true);
        auto* outDirBtn = new QPushButton("📁", dialog);
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
        auto* cacheBtn = new QPushButton("📁", dialog);
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
            "QPushButton { background-color: #F0B90B; color: #0D1117; "
            "  font-weight: bold; border: none; border-radius: 4px; padding: 6px 24px; }"
            "QPushButton:hover { background-color: #F5C518; }"
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
    QLabel* m_mouseStatusLabel = nullptr;
    Toolbar* m_toolbar = nullptr;
    ProjectPanel* m_projectPanel = nullptr;
    PreviewPanel* m_previewPanel = nullptr;
    ParamsPanel* m_paramsPanel = nullptr;
    QWidget* m_stepBar = nullptr;
    QButtonGroup* m_stepGroup = nullptr;
    ProcessingWorker* m_worker = nullptr;
    MaskPreviewWorker* m_maskPreviewWorker = nullptr;
    QSet<MaskPreviewWorker*> m_activeMaskPreviewWorkers;

    // 缓存最后一次堆栈结果（用于导出）
    std::vector<uint16_t> m_cachedStackedData;
    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    int m_cachedFrameCount = 0;
};

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication app(argc, argv);
    app.setApplicationName("StarProcessor");
    app.setApplicationVersion("0.4.0");
    app.setOrganizationName("StarProcessor");

    app.setStyleSheet(
        "QMainWindow { background-color: #0D1117; }"
        "QWidget { background-color: #0D1117; color: #E6EDF3; }"
        "QMenuBar { background-color: #161B22; color: #E6EDF3; }"
        "QMenuBar::item:selected { background-color: #30363D; }"
        "QMenuBar::item { padding: 4px 12px; }"
        "QStatusBar { background-color: #161B22; color: #8B949E; }"
        "QMessageBox { background-color: #161B22; }"
        "QFileDialog { background-color: #161B22; color: #E6EDF3; }"
        "QSplitter::handle { background-color: #30363D; }"
        "QScrollBar:vertical { background-color: #161B22; width: 12px; border-radius: 6px; }"
        "QScrollBar::handle:vertical { background-color: #30363D; border-radius: 6px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background-color: #484F58; }"
        "QScrollBar:horizontal { background-color: #161B22; height: 12px; border-radius: 6px; }"
        "QScrollBar::handle:horizontal { background-color: #30363D; border-radius: 6px; min-width: 20px; }"
        "QScrollBar::handle:horizontal:hover { background-color: #484F58; }"
        "QToolTip { background-color: #161B22; color: #E6EDF3; border: 1px solid #30363D; padding: 4px; }"
    );

    MainWindow window;
    window.show();

    return app.exec();
}

#include "main.moc"
