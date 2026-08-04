#include "Toolbar.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QStyle>

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void Toolbar::setupUI() {
    setFixedHeight(56);
    setStyleSheet(
        "Toolbar { background-color: #171A1D; border-bottom: 1px solid #2A3035; }"
        "QLabel { color: #F2F4F5; background-color: transparent; }"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 12, 0);
    layout->setSpacing(6);

    // 品牌与项目状态始终留在最左侧，形成稳定的视觉锚点。
    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(1);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* brandRow = new QHBoxLayout();
    brandRow->setSpacing(7);
    m_brandLabel = new QLabel("StarProcessor", this);
    m_brandLabel->setStyleSheet(
        "font-size: 16px; font-weight: 700; color: #F2F4F5;"
    );
    brandRow->addWidget(m_brandLabel);

    m_versionLabel = new QLabel("BETA", this);
    m_versionLabel->setStyleSheet(
        "font-size: 9px; font-weight: 700; color: #3CC7A5; "
        "background-color: #17342D; border-radius: 3px; padding: 1px 5px;"
    );
    brandRow->addWidget(m_versionLabel);
    leftLayout->addLayout(brandRow);

    m_projectSummaryLabel = new QLabel(QString::fromUtf8("等待导入素材"), this);
    m_projectSummaryLabel->setStyleSheet("font-size: 10px; color: #7F8992;");
    leftLayout->addWidget(m_projectSummaryLabel);

    layout->addLayout(leftLayout);
    layout->addSpacing(14);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet("color: #2A3035;");
    separator->setFixedWidth(1);
    separator->setFixedHeight(30);
    layout->addWidget(separator);
    layout->addSpacing(8);

    // 素材命令保持克制；真正的主操作是右侧“开始处理”。
    m_importFilesBtn = createActionButton(
        style()->standardIcon(QStyle::SP_DialogOpenButton),
        QString::fromUtf8("添加照片"));
    connect(m_importFilesBtn, &QPushButton::clicked, this, &Toolbar::importFilesClicked);
    layout->addWidget(m_importFilesBtn);

    m_importFolderBtn = createIconButton(
        style()->standardIcon(QStyle::SP_DirOpenIcon),
        QString::fromUtf8("导入文件夹"));
    connect(m_importFolderBtn, &QPushButton::clicked, this, &Toolbar::importFolderClicked);
    layout->addWidget(m_importFolderBtn);

    m_clearProjectBtn = createIconButton(
        style()->standardIcon(QStyle::SP_TrashIcon),
        QString::fromUtf8("清空项目"));
    connect(m_clearProjectBtn, &QPushButton::clicked, this, &Toolbar::clearProjectClicked);
    layout->addWidget(m_clearProjectBtn);

    layout->addStretch();

    m_exportResultBtn = createActionButton(
        style()->standardIcon(QStyle::SP_DialogSaveButton),
        QString::fromUtf8("导出"));
    connect(m_exportResultBtn, &QPushButton::clicked, this, &Toolbar::exportResultClicked);
    layout->addWidget(m_exportResultBtn);

    m_startProcessBtn = createActionButton(
        style()->standardIcon(QStyle::SP_MediaPlay),
        QString::fromUtf8("开始处理"), true);
    connect(m_startProcessBtn, &QPushButton::clicked, this, &Toolbar::startProcessClicked);
    layout->addWidget(m_startProcessBtn);

    layout->addSpacing(6);
    m_settingsBtn = createIconButton(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView),
        QString::fromUtf8("设置"));
    connect(m_settingsBtn, &QPushButton::clicked, this, &Toolbar::settingsClicked);
    layout->addWidget(m_settingsBtn);

    m_aboutBtn = createIconButton(
        style()->standardIcon(QStyle::SP_MessageBoxInformation),
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
        "  background-color: #22272B;"
        "  border-color: #30373D;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #2A3035;"
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
            "  background-color: #3CC7A5;"
            "  color: #0E1714;"
            "  border: none;"
            "  border-radius: 5px;"
            "  padding: 5px 16px;"
            "  font-size: 12px;"
            "  font-weight: 700;"
            "}"
            "QPushButton:hover {"
            "  background-color: #53D4B3;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #2EAB8D;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #27302E;"
            "  color: #64716D;"
            "}"
        );
    } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #1D2125;"
            "  color: #DDE1E4;"
            "  border: 1px solid #30373D;"
            "  border-radius: 5px;"
            "  padding: 5px 13px;"
            "  font-size: 12px;"
            "}"
            "QPushButton:hover {"
            "  background-color: #272C31;"
            "  border-color: #465059;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #30373D;"
            "}"
            "QPushButton:disabled {"
            "  background-color: #171A1D;"
            "  color: #566068;"
            "  border-color: #24292D;"
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
