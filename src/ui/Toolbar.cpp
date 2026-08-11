#include "Toolbar.h"
#include "UiAssets.h"
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
    setObjectName("mainToolbar");
    setFixedHeight(68);
    setStyleSheet(
        "QWidget#mainToolbar { background-color: #14191A; "
        "border-bottom: 1px solid #283132; }"
        "QLabel { color: #F2F6F5; background-color: transparent; "
        "letter-spacing: 0; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 0, 18, 0);
    layout->setSpacing(6);

    // The mark combines stacked frames and a star, giving the workspace a
    // recognisable product identity instead of a platform toolbar silhouette.
    m_logoLabel = new QLabel(this);
    m_logoLabel->setFixedSize(36, 36);
    m_logoLabel->setPixmap(UiAssets::logoMark(36));
    m_logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logoLabel);

    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(0);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* brandRow = new QHBoxLayout();
    brandRow->setSpacing(6);
    m_brandLabel = new QLabel("StarProcessor", this);
    m_brandLabel->setStyleSheet(
        "font-size: 15px; font-weight: 700; color: #F2F6F5;"
    );
    brandRow->addWidget(m_brandLabel);

    m_versionLabel = new QLabel("BETA", this);
    m_versionLabel->setStyleSheet(
        "font-size: 8px; font-weight: 700; color: #74DDBF; "
        "background-color: #192824; border: 1px solid #28483F; "
        "border-radius: 3px; padding: 1px 5px;"
    );
    brandRow->addWidget(m_versionLabel);
    leftLayout->addLayout(brandRow);

    m_projectSummaryLabel = new QLabel(QString::fromUtf8("等待导入素材"), this);
    m_projectSummaryLabel->setStyleSheet("font-size: 10px; color: #778984;");
    leftLayout->addWidget(m_projectSummaryLabel);

    layout->addLayout(leftLayout);
    layout->addSpacing(14);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet("color: #303A3B;");
    separator->setFixedWidth(1);
    separator->setFixedHeight(28);
    layout->addWidget(separator);
    layout->addSpacing(12);

    m_sceneBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::Scenes, QColor("#6FD9BB")),
        QString::fromUtf8("处理场景"));
    m_sceneBtn->setToolTip(QString::fromUtf8("选择处理场景"));
    connect(m_sceneBtn, &QPushButton::clicked, this,
            &Toolbar::sceneSelectorClicked);
    layout->addWidget(m_sceneBtn);

    // 素材命令保持克制；真正的主操作是右侧“开始处理”。
    m_importFilesBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::AddPhotos, QColor("#C4D0CD")),
        QString::fromUtf8("添加照片"));
    connect(m_importFilesBtn, &QPushButton::clicked, this, &Toolbar::importFilesClicked);
    layout->addWidget(m_importFilesBtn);

    m_importFolderBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Folder, QColor("#92A39F")),
        QString::fromUtf8("导入文件夹"));
    connect(m_importFolderBtn, &QPushButton::clicked, this, &Toolbar::importFolderClicked);
    layout->addWidget(m_importFolderBtn);

    m_clearProjectBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Trash, QColor("#92A39F")),
        QString::fromUtf8("清空项目"));
    connect(m_clearProjectBtn, &QPushButton::clicked, this, &Toolbar::clearProjectClicked);
    layout->addWidget(m_clearProjectBtn);

    layout->addStretch();

    m_exportResultBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::Export, QColor("#C4D0CD")),
        QString::fromUtf8("导出"));
    connect(m_exportResultBtn, &QPushButton::clicked, this, &Toolbar::exportResultClicked);
    layout->addWidget(m_exportResultBtn);

    m_startProcessBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::Play, QColor("#10201C")),
        QString::fromUtf8("开始处理"), true);
    connect(m_startProcessBtn, &QPushButton::clicked, this, &Toolbar::startProcessClicked);
    layout->addWidget(m_startProcessBtn);

    layout->addSpacing(10);
    m_settingsBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Sliders, QColor("#92A39F")),
        QString::fromUtf8("设置"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &Toolbar::settingsClicked);
    layout->addWidget(m_settingsBtn);

    m_aboutBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Info, QColor("#92A39F")),
        QString::fromUtf8("关于"));
    connect(m_aboutBtn, &QPushButton::clicked, this, &Toolbar::aboutClicked);
    layout->addWidget(m_aboutBtn);

    updateButtonStates();
}

QPushButton* Toolbar::createIconButton(const QIcon& icon, const QString& tooltip) {
    auto* btn = new QPushButton(this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(16, 16));
    btn->setToolTip(tooltip);
    btn->setAccessibleName(tooltip);
    btn->setFixedSize(32, 32);
    btn->setStyleSheet(
        "QPushButton {"
        "  background-color: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 5px;"
        "  padding: 0;"
        "}"
        "QPushButton:hover {"
        "  background-color: #202829;"
        "  border-color: #374243;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #263132;"
        "}"
        "QPushButton:disabled {"
        "  background-color: transparent;"
        "  border-color: transparent;"
        "}"
    );
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

QPushButton* Toolbar::createActionButton(const QIcon& icon, const QString& text,
                                         bool isPrimary) {
    auto* btn = new QPushButton(text, this);
    btn->setIcon(icon);
    btn->setIconSize(QSize(16, 16));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(34);

    if (isPrimary) {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #59D7B2;"
            "  color: #10201C;"
            "  border: 1px solid #59D7B2;"
            "  border-radius: 5px;"
            "  padding: 5px 17px;"
            "  font-size: 12px;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover {"
            "  background-color: #73E2C3;"
            "  border-color: #73E2C3;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #3FC29D;"
            "  border-color: #3FC29D;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #222A2B;"
            "  color: #5E6B68;"
            "  border-color: #222A2B;"
            "}"
        );
    } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #1B2324;"
            "  color: #D9E3E0;"
            "  border: 1px solid #344142;"
            "  border-radius: 5px;"
            "  padding: 5px 12px;"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "}"
            "QPushButton:hover {"
            "  background-color: #242D2E;"
            "  border-color: #4A5958;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #1B4B40;"
            "  border-color: #54D5B0;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #181E1F;"
            "  color: #5D6B68;"
            "  border-color: #2B3435;"
            "}"
        );
    }
    return btn;
}

void Toolbar::enableProcess(bool enabled) {
    m_canProcess = enabled;
    updateButtonStates();
}

void Toolbar::enableExport(bool enabled) {
    m_canExport = enabled;
    updateButtonStates();
}

void Toolbar::setProcessing(bool processing) {
    m_processing = processing;
    if (m_startProcessBtn) {
        m_startProcessBtn->setText(processing
            ? QString::fromUtf8("处理中...")
            : QString::fromUtf8("开始处理"));
    }
    updateButtonStates();
}

void Toolbar::setProjectSummary(const QString& summary) {
    if (m_projectSummaryLabel) m_projectSummaryLabel->setText(summary);
}

void Toolbar::updateButtonStates() {
    if (m_startProcessBtn) m_startProcessBtn->setEnabled(m_canProcess && !m_processing);
    if (m_exportResultBtn) m_exportResultBtn->setEnabled(m_canExport && !m_processing);
    if (m_importFilesBtn) m_importFilesBtn->setEnabled(!m_processing);
    if (m_sceneBtn) m_sceneBtn->setEnabled(!m_processing);
    if (m_importFolderBtn) m_importFolderBtn->setEnabled(!m_processing);
    if (m_clearProjectBtn) m_clearProjectBtn->setEnabled(!m_processing);
}
