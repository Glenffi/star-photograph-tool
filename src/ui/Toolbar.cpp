#include "Toolbar.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void Toolbar::setupUI() {
    setFixedHeight(40);
    setStyleSheet(
        "QWidget { background-color: #161B22; }"
        "QLabel { color: #E6EDF3; background-color: transparent; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    // 左侧：品牌名 + 版本
    auto* leftLayout = new QHBoxLayout();
    leftLayout->setSpacing(6);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_brandLabel = new QLabel("StarProcessor", this);
    m_brandLabel->setStyleSheet(
        "font-size: 15px; font-weight: bold; color: #E6EDF3; background-color: transparent;"
    );
    leftLayout->addWidget(m_brandLabel);

    m_versionLabel = new QLabel("v0.2.0", this);
    m_versionLabel->setStyleSheet(
        "font-size: 10px; color: #8B949E; background-color: #21262D; "
        "border-radius: 4px; padding: 2px 6px;"
    );
    leftLayout->addWidget(m_versionLabel);

    layout->addLayout(leftLayout);
    layout->addSpacing(20);

    // 中间：操作按钮组
    m_importFilesBtn = createActionButton(QString::fromUtf8("📁"), QString::fromUtf8("导入 RAW"), true);
    connect(m_importFilesBtn, &QPushButton::clicked, this, &Toolbar::importFilesClicked);
    layout->addWidget(m_importFilesBtn);

    m_importFolderBtn = createActionButton(QString::fromUtf8("📂"), QString::fromUtf8("导入文件夹"));
    connect(m_importFolderBtn, &QPushButton::clicked, this, &Toolbar::importFolderClicked);
    layout->addWidget(m_importFolderBtn);

    m_clearProjectBtn = createActionButton(QString::fromUtf8("🗑️"), QString::fromUtf8("清空项目"));
    connect(m_clearProjectBtn, &QPushButton::clicked, this, &Toolbar::clearProjectClicked);
    layout->addWidget(m_clearProjectBtn);

    // 分隔线
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet("color: #30363D;");
    separator->setFixedWidth(1);
    layout->addWidget(separator);

    m_startProcessBtn = createActionButton(QString::fromUtf8("⏯"), QString::fromUtf8("开始处理"));
    m_startProcessBtn->setEnabled(false);
    connect(m_startProcessBtn, &QPushButton::clicked, this, &Toolbar::startProcessClicked);
    layout->addWidget(m_startProcessBtn);

    m_exportResultBtn = createActionButton(QString::fromUtf8("💾"), QString::fromUtf8("导出结果"));
    m_exportResultBtn->setEnabled(false);
    connect(m_exportResultBtn, &QPushButton::clicked, this, &Toolbar::exportResultClicked);
    layout->addWidget(m_exportResultBtn);

    layout->addStretch();

    // 右侧：图标按钮
    m_settingsBtn = createIconButton(QString::fromUtf8("⚙️"), QString::fromUtf8("设置"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &Toolbar::settingsClicked);
    layout->addWidget(m_settingsBtn);

    m_aboutBtn = createIconButton(QString::fromUtf8("ⓘ"), QString::fromUtf8("关于"));
    connect(m_aboutBtn, &QPushButton::clicked, this, &Toolbar::aboutClicked);
    layout->addWidget(m_aboutBtn);

    // 底部边框
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor("#161B22"));
    setPalette(pal);
}

QPushButton* Toolbar::createIconButton(const QString& icon, const QString& tooltip) {
    auto* btn = new QPushButton(icon, this);
    btn->setToolTip(tooltip);
    btn->setFixedSize(32, 32);
    btn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  color: #8B949E;"
        "  border: none;"
        "  border-radius: 6px;"
        "  font-size: 14px;"
        "  padding: 0;"
        "}"
        "QPushButton:hover {"
        "  background-color: #21262D;"
        "  color: #E6EDF3;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #30363D;"
        "}"
    );
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

QPushButton* Toolbar::createActionButton(const QString& icon, const QString& text, bool isPrimary) {
    auto* btn = new QPushButton(icon + "  " + text, this);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(28);

    if (isPrimary) {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #F0B90B;"
            "  color: #0D1117;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 4px 14px;"
            "  font-size: 12px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "  background-color: #F5C518;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #D4A009;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #30363D;"
            "  color: #8B949E;"
            "}"
        );
    } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #21262D;"
            "  color: #E6EDF3;"
            "  border: 1px solid #30363D;"
            "  border-radius: 6px;"
            "  padding: 4px 14px;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #30363D;"
            "  border: 1px solid #484F58;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #484F58;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #161B22;"
            "  color: #484F58;"
            "  border: 1px solid #21262D;"
            "}"
        );
    }
    return btn;
}

void Toolbar::enableProcess(bool enabled) {
    if (m_startProcessBtn) m_startProcessBtn->setEnabled(enabled);
}

void Toolbar::enableExport(bool enabled) {
    if (m_exportResultBtn) m_exportResultBtn->setEnabled(enabled);
}
