#include <QApplication>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("StarProcessor — 星空摄影师 RAW 处理工具");
        resize(1280, 800);

        auto* centralWidget = new QWidget(this);
        setCentralWidget(centralWidget);

        auto* layout = new QVBoxLayout(centralWidget);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(15);

        auto* titleLabel = new QLabel("✨ StarProcessor", this);
        titleLabel->setStyleSheet(
            "font-size: 32px; font-weight: bold; color: #F0B90B;"
        );
        titleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(titleLabel);

        auto* subtitleLabel = new QLabel("为星空摄影师打造的 RAW 处理工具", this);
        subtitleLabel->setStyleSheet(
            "font-size: 16px; color: #8B949E;"
        );
        subtitleLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(subtitleLabel);

        auto* infoLabel = new QLabel(
            "P0.1 里程碑：跨平台项目骨架\n"
            "目标平台：Windows + macOS\n"
            "技术栈：C++17 + Qt6 + CMake",
            this
        );
        infoLabel->setStyleSheet(
            "font-size: 14px; color: #E6EDF3; line-height: 1.6;"
        );
        infoLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(infoLabel);

        auto* importButton = new QPushButton("📁 导入 RAW 文件（演示）", this);
        importButton->setStyleSheet(
            "QPushButton {"
            "  background-color: #F0B90B;"
            "  color: #0D1117;"
            "  font-size: 14px;"
            "  font-weight: bold;"
            "  padding: 12px 30px;"
            "  border-radius: 8px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #FFD700;"
            "}"
        );
        importButton->setCursor(Qt::PointingHandCursor);
        connect(importButton, &QPushButton::clicked, this, &MainWindow::onImportClicked);
        layout->addWidget(importButton, 0, Qt::AlignCenter);

        auto* versionLabel = new QLabel(
            QString("Version %1 | Phase 0 — Infrastructure")
                .arg(QApplication::applicationVersion()),
            this
        );
        versionLabel->setStyleSheet("font-size: 12px; color: #484F58;");
        versionLabel->setAlignment(Qt::AlignCenter);
        layout->addWidget(versionLabel);

        layout->addStretch();

        setupMenuBar();
        statusBar()->showMessage("就绪 — P0.1 项目骨架已启动");
    }

private:
    void setupMenuBar() {
        auto* fileMenu = menuBar()->addMenu("文件");
        
        auto* importAction = new QAction("导入 RAW...", this);
        importAction->setShortcut(QKeySequence::Open);
        connect(importAction, &QAction::triggered, this, &MainWindow::onImportClicked);
        fileMenu->addAction(importAction);
        
        fileMenu->addSeparator();
        
        auto* exitAction = new QAction("退出", this);
        exitAction->setShortcut(QKeySequence::Quit);
        connect(exitAction, &QAction::triggered, this, &QWidget::close);
        fileMenu->addAction(exitAction);

        auto* helpMenu = menuBar()->addMenu("帮助");
        
        auto* aboutAction = new QAction("关于", this);
        connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutClicked);
        helpMenu->addAction(aboutAction);
    }

private slots:
    void onImportClicked() {
        QStringList fileNames = QFileDialog::getOpenFileNames(
            this,
            "选择 RAW 文件",
            QString(),
            "RAW 文件 (*.nef *.cr2 *.arw *.dng *.raw *.orf *.raf *.pef);;所有文件 (*)"
        );

        if (!fileNames.isEmpty()) {
            statusBar()->showMessage(
                QString("已选择 %1 个文件（P0.1 演示阶段，实际处理将在 P1 实现）")
                    .arg(fileNames.size()),
                5000
            );
            
            QMessageBox::information(
                this,
                "导入成功",
                QString("已选择 %1 个 RAW 文件。\n\n"
                        "P0.1 阶段仅展示文件选择功能。\n"
                        "实际的 RAW 解码和预览将在 P0.2 中实现。")
                    .arg(fileNames.size())
            );
        }
    }

    void onAboutClicked() {
        QMessageBox::about(
            this,
            "关于 StarProcessor",
            "<h2>StarProcessor</h2>"
            "<p>为星空摄影师打造的跨平台 RAW 处理工具</p>"
            "<p><b>版本：</b>0.1.0</p>"
            "<p><b>阶段：</b>P0 — 基础设施</p>"
            "<p><b>技术栈：</b>C++17 + Qt6 + CMake</p>"
            "<p><b>目标平台：</b>Windows + macOS</p>"
            "<hr>"
            "<p>全部代码开源，基于 MIT License</p>"
        );
    }
};

int main(int argc, char* argv[]) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough
    );
#endif

    QApplication app(argc, argv);
    app.setApplicationName("StarProcessor");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("StarProcessor");

    app.setStyleSheet(
        "QMainWindow { background-color: #0D1117; }"
        "QWidget { background-color: #0D1117; color: #E6EDF3; }"
        "QMenuBar { background-color: #161B22; color: #E6EDF3; }"
        "QMenuBar::item:selected { background-color: #30363D; }"
        "QMenu { background-color: #161B22; color: #E6EDF3; border: 1px solid #30363D; }"
        "QMenu::item:selected { background-color: #30363D; }"
        "QStatusBar { background-color: #161B22; color: #8B949E; }"
        "QMessageBox { background-color: #161B22; }"
        "QFileDialog { background-color: #161B22; color: #E6EDF3; }"
    );

    MainWindow window;
    window.show();

    return app.exec();
}

#include "main.moc"
