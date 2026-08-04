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
    setFixedHeight(64);
    setStyleSheet(
        "Toolbar { background-color: #131A1C; border-bottom: 1px solid #293638; }"
        "QLabel { color: #F3F7F6; background-color: transparent; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);
    layout->setSpacing(7);

    // The mark combines stacked frames and a star, giving the workspace a
    // recognisable product identity instead of a platform toolbar silhouette.
    m_logoLabel = new QLabel(this);
    m_logoLabel->setFixedSize(38, 38);
    m_logoLabel->setPixmap(UiAssets::logoMark(38));
    m_logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_logoLabel);

    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(0);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* brandRow = new QHBoxLayout();
    brandRow->setSpacing(7);
    m_brandLabel = new QLabel("StarProcessor", this);
    m_brandLabel->setStyleSheet(
        "font-size: 16px; font-weight: 700; color: #F3F7F6;"
    );
    brandRow->addWidget(m_brandLabel);

    m_versionLabel = new QLabel("BETA", this);
    m_versionLabel->setStyleSheet(
        "font-size: 8px; font-weight: 700; color: #4ED7AE; "
        "background-color: #183B33; border-radius: 3px; padding: 1px 5px;"
    );
    brandRow->addWidget(m_versionLabel);
    leftLayout->addLayout(brandRow);

    m_projectSummaryLabel = new QLabel(QString::fromUtf8("等待导入素材"), this);
    m_projectSummaryLabel->setStyleSheet("font-size: 10px; color: #81938F;");
    leftLayout->addWidget(m_projectSummaryLabel);

    layout->addLayout(leftLayout);
    layout->addSpacing(10);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet("color: #2B393B;");
    separator->setFixedWidth(1);
    separator->setFixedHeight(30);
    layout->addWidget(separator);
    layout->addSpacing(8);

    // 素材命令保持克制；真正的主操作是右侧“开始处理”。
    m_importFilesBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::AddPhotos, QColor("#B7C7C3")),
        QString::fromUtf8("添加照片"));
    connect(m_importFilesBtn, &QPushButton::clicked, this, &Toolbar::importFilesClicked);
    layout->addWidget(m_importFilesBtn);

    m_importFolderBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Folder, QColor("#A7B8B4")),
        QString::fromUtf8("导入文件夹"));
    connect(m_importFolderBtn, &QPushButton::clicked, this, &Toolbar::importFolderClicked);
    layout->addWidget(m_importFolderBtn);

    m_clearProjectBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Trash, QColor("#A7B8B4")),
        QString::fromUtf8("清空项目"));
    connect(m_clearProjectBtn, &QPushButton::clicked, this, &Toolbar::clearProjectClicked);
    layout->addWidget(m_clearProjectBtn);

    layout->addStretch();

    m_exportResultBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::Export, QColor("#B7C7C3")),
        QString::fromUtf8("导出"));
    connect(m_exportResultBtn, &QPushButton::clicked, this, &Toolbar::exportResultClicked);
    layout->addWidget(m_exportResultBtn);

    m_startProcessBtn = createActionButton(
        UiAssets::icon(UiAssets::Glyph::Play, QColor("#0D211B")),
        QString::fromUtf8("开始处理"), true);
    connect(m_startProcessBtn, &QPushButton::clicked, this, &Toolbar::startProcessClicked);
    layout->addWidget(m_startProcessBtn);

    layout->addSpacing(6);
    m_settingsBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Sliders, QColor("#A7B8B4")),
        QString::fromUtf8("设置"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &Toolbar::settingsClicked);
    layout->addWidget(m_settingsBtn);

    m_aboutBtn = createIconButton(
        UiAssets::icon(UiAssets::Glyph::Info, QColor("#A7B8B4")),
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
        "  background-color: #202A2D;"
        "  border-color: #344548;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #293638;"
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
            "  background-color: #4ED7AE;"
            "  color: #0D211B;"
            "  border: none;"
            "  border-radius: 5px;"
            "  padding: 5px 16px;"
            "  font-size: 12px;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover {"
            "  background-color: #67E2BE;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #32B98F;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #242E31;"
            "  color: #526663;"
            "}"
        );
    } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #202A2D;"
            "  color: #D2DDDA;"
            "  border: 1px solid #344548;"
            "  border-radius: 5px;"
            "  padding: 5px 13px;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #293638;"
            "  border-color: #4D6265;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #344548;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #171F21;"
            "  color: #536763;"
            "  border-color: #263234;"
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
    if (m_importFolderBtn) m_importFolderBtn->setEnabled(!m_processing);
    if (m_clearProjectBtn) m_clearProjectBtn->setEnabled(!m_processing);
}
