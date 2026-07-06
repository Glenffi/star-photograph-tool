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

#include "ui/ProjectPanel.h"
#include "ui/PreviewPanel.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("StarProcessor — 星空摄影师 RAW 处理工具");
        resize(1400, 900);

        setupCentralWidget();
        setupMenuBar();
        setupStatusBar();
        setupStepBar();
        setupConnections();

        statusBar()->showMessage("就绪 — P0.2 核心框架已启动");
    }

private:
    void setupCentralWidget() {
        auto* centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);

        auto* mainLayout = new QVBoxLayout(centralWidget);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        // 主内容区：三栏布局
        auto* contentSplitter = new QSplitter(Qt::Horizontal, this);
        contentSplitter->setHandleWidth(2);
        contentSplitter->setStyleSheet(
            "QSplitter::handle { background-color: #30363D; }"
        );

        // 左侧面板：ProjectPanel
        m_projectPanel = new ProjectPanel(this);
        m_projectPanel->setMinimumWidth(200);
        m_projectPanel->setMaximumWidth(400);
        contentSplitter->addWidget(m_projectPanel);

        // 中央面板：PreviewPanel
        m_previewPanel = new PreviewPanel(this);
        contentSplitter->addWidget(m_previewPanel);

        // 右侧面板：参数面板（占位）
        m_paramsPanel = new QWidget(this);
        m_paramsPanel->setMinimumWidth(200);
        m_paramsPanel->setMaximumWidth(400);
        m_paramsPanel->setStyleSheet(
            "QWidget { background-color: #161B22; border-left: 1px solid #30363D; }"
        );
        auto* paramsLayout = new QVBoxLayout(m_paramsPanel);
        paramsLayout->setContentsMargins(12, 12, 12, 12);
        paramsLayout->setSpacing(8);

        auto* paramsTitle = new QLabel("处理参数", m_paramsPanel);
        paramsTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #E6EDF3;");
        paramsLayout->addWidget(paramsTitle);

        auto* paramsPlaceholder = new QLabel(
            "P0.2 占位面板\n\n"
            "后续阶段将添加：\n"
            "• 对齐参数\n"
            "• 堆栈算法选择\n"
            "• 降噪强度\n"
            "• 输出格式",
            m_paramsPanel
        );
        paramsPlaceholder->setStyleSheet(
            "color: #8B949E; font-size: 12px; line-height: 1.6;"
        );
        paramsPlaceholder->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        paramsLayout->addWidget(paramsPlaceholder);
        paramsLayout->addStretch();

        contentSplitter->addWidget(m_paramsPanel);

        // 设置默认比例：20% / 60% / 20%
        contentSplitter->setSizes({280, 840, 280});

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
        connect(fitViewAction, &QAction::triggered, this, []() {
            // 通过 PreviewPanel 的方法实现
        });
        viewMenu->addAction(fitViewAction);

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
        connect(m_projectPanel, &ProjectPanel::fileSelected, this, [this](const QString& filePath) {
            m_previewPanel->loadImage(filePath);
            statusBar()->showMessage(QString("已加载：%1").arg(filePath), 3000);
        });

        connect(m_projectPanel, &ProjectPanel::requestMetadata, this, [this](const QString& filePath) {
            onViewMetadata(filePath);
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
        QMessageBox::information(
            this,
            "图像元数据",
            QString("<b>文件路径：</b>%1<br><br>"
                    "<i>完整元数据读取将在 P1 阶段实现</i>")
                .arg(filePath)
        );
    }

    void onAboutClicked() {
        QMessageBox::about(
            this,
            "关于 StarProcessor",
            "<h2>StarProcessor</h2>"
            "<p>为星空摄影师打造的跨平台 RAW 处理工具</p>"
            "<p><b>版本：</b>0.2.0</p>"
            "<p><b>阶段：</b>P0 — 基础设施</p>"
            "<p><b>技术栈：</b>C++17 + Qt6 + CMake + LibRaw</p>"
            "<p><b>目标平台：</b>Windows + macOS</p>"
            "<hr>"
            "<p>全部代码开源，基于 MIT License</p>"
        );
    }

private:
    ProjectPanel* m_projectPanel = nullptr;
    PreviewPanel* m_previewPanel = nullptr;
    QWidget* m_paramsPanel = nullptr;
    QWidget* m_stepBar = nullptr;
    QButtonGroup* m_stepGroup = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );

    QApplication app(argc, argv);
    app.setApplicationName("StarProcessor");
    app.setApplicationVersion("0.2.0");
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
