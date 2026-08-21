#include "Toolbar.h"

#include "StyleTokens.h"
#include "UiAssets.h"

#include <QAction>
#include <QFont>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>

namespace {

QIcon toolbarIcon(UiAssets::Glyph glyph, const char* normalColor,
                  const char* activeColor) {
    constexpr int kIconSize = StyleTokens::Controls::kIconSize;

    QIcon result;
    result.addPixmap(
        UiAssets::icon(glyph, StyleTokens::Colors::fromHex(normalColor),
                       kIconSize)
            .pixmap(kIconSize, kIconSize),
        QIcon::Normal);
    result.addPixmap(
        UiAssets::icon(glyph, StyleTokens::Colors::fromHex(activeColor),
                       kIconSize)
            .pixmap(kIconSize, kIconSize),
        QIcon::Active);
    result.addPixmap(
        UiAssets::icon(glyph,
                       StyleTokens::Colors::fromHex(
                           StyleTokens::Colors::kTextFaint),
                       kIconSize)
            .pixmap(kIconSize, kIconSize),
        QIcon::Disabled);
    return result;
}

}  // namespace

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void Toolbar::setupUI() {
    setObjectName(QStringLiteral("mainToolbar"));
    setFixedHeight(36);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(StyleTokens::Spacing::kBase);

    m_importFilesBtn = createActionButton(
        toolbarIcon(UiAssets::Glyph::AddPhotos,
                    StyleTokens::Colors::kTextSecondary,
                    StyleTokens::Colors::kTextPrimary),
        tr("导入文件"), StyleTokens::Properties::kGhost);
    m_importFilesBtn->setToolTip(tr("选择 RAW 文件"));
    m_importFilesBtn->setAccessibleName(tr("导入文件"));
    connect(m_importFilesBtn, &QPushButton::clicked, this,
            &Toolbar::importFilesClicked);
    layout->addWidget(m_importFilesBtn);

    m_importFolderBtn = createActionButton(
        toolbarIcon(UiAssets::Glyph::Folder,
                    StyleTokens::Colors::kTextSecondary,
                    StyleTokens::Colors::kTextPrimary),
        tr("导入目录"), StyleTokens::Properties::kGhost);
    m_importFolderBtn->setToolTip(tr("导入目录中的 RAW 文件"));
    m_importFolderBtn->setAccessibleName(tr("导入目录"));
    connect(m_importFolderBtn, &QPushButton::clicked, this,
            &Toolbar::importFolderClicked);
    layout->addWidget(m_importFolderBtn);

    layout->addStretch(1);

    m_exportResultBtn = createActionButton(
        toolbarIcon(UiAssets::Glyph::Export,
                    StyleTokens::Colors::kTextSecondary,
                    StyleTokens::Colors::kTextPrimary),
        tr("导出"), StyleTokens::Properties::kSecondaryButton);
    m_exportResultBtn->setToolTip(tr("导出当前处理结果"));
    m_exportResultBtn->setAccessibleName(tr("导出结果"));
    connect(m_exportResultBtn, &QPushButton::clicked, this,
            &Toolbar::exportResultClicked);

    m_startProcessBtn = createActionButton(
        toolbarIcon(UiAssets::Glyph::Play,
                    StyleTokens::Colors::kActionText,
                    StyleTokens::Colors::kActionText),
        tr("处理"), StyleTokens::Properties::kPrimary);
    m_startProcessBtn->setToolTip(tr("使用当前参数开始处理"));
    m_startProcessBtn->setAccessibleName(tr("开始处理"));
    connect(m_startProcessBtn, &QPushButton::clicked, this,
            &Toolbar::startProcessClicked);
    layout->addWidget(m_startProcessBtn);
    layout->addWidget(m_exportResultBtn);

    m_overflowBtn = createIconButton(QIcon(), tr("更多"));
    m_overflowBtn->setText(QString::fromUtf8("⋯"));
    QFont overflowFont = m_overflowBtn->font();
    overflowFont.setPixelSize(StyleTokens::Controls::kIconSize);
    overflowFont.setWeight(QFont::DemiBold);
    m_overflowBtn->setFont(overflowFont);
    connect(m_overflowBtn, &QPushButton::clicked, this,
            &Toolbar::showOverflowMenu);
    layout->addWidget(m_overflowBtn);

    m_overflowMenu = new QMenu(this);
    m_clearProjectAction = m_overflowMenu->addAction(tr("清空项目"));
    m_overflowMenu->addSeparator();
    m_settingsAction = m_overflowMenu->addAction(tr("设置"));
    m_checkUpdatesAction = m_overflowMenu->addAction(tr("检查更新"));
    m_shortcutsAction = m_overflowMenu->addAction(tr("快捷键"));
    m_overflowMenu->addSeparator();
    m_aboutAction = m_overflowMenu->addAction(tr("关于"));

    connect(m_clearProjectAction, &QAction::triggered, this,
            &Toolbar::clearProjectClicked);
    connect(m_settingsAction, &QAction::triggered, this,
            &Toolbar::settingsClicked);
    connect(m_checkUpdatesAction, &QAction::triggered, this,
            &Toolbar::checkUpdatesClicked);
    connect(m_shortcutsAction, &QAction::triggered, this,
            &Toolbar::shortcutsClicked);
    connect(m_aboutAction, &QAction::triggered, this,
            &Toolbar::aboutClicked);

    updateButtonStates();
}

QPushButton* Toolbar::createIconButton(const QIcon& icon,
                                       const QString& tooltip) {
    auto* button = new QPushButton(this);
    button->setIcon(icon);
    button->setIconSize(QSize(StyleTokens::Controls::kIconSize,
                              StyleTokens::Controls::kIconSize));
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setProperty(StyleTokens::Properties::kVariant,
                        StyleTokens::Properties::kIcon);
    button->setFixedSize(StyleTokens::Controls::kIconButtonSize,
                         StyleTokens::Controls::kIconButtonSize);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QPushButton* Toolbar::createActionButton(const QIcon& icon,
                                         const QString& text,
                                         const char* variant) {
    auto* button = new QPushButton(text, this);
    button->setIcon(icon);
    button->setIconSize(QSize(StyleTokens::Controls::kIconSize,
                              StyleTokens::Controls::kIconSize));
    button->setProperty(StyleTokens::Properties::kVariant, variant);
    button->setFixedHeight(StyleTokens::Controls::kCompactHeight);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setCursor(Qt::PointingHandCursor);
    return button;
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
        if (processing) {
            m_startProcessBtn->setText(tr("取消"));
            m_startProcessBtn->setIcon(QIcon());
            m_startProcessBtn->setToolTip(tr("取消当前处理任务"));
            m_startProcessBtn->setAccessibleName(tr("取消处理"));
            setButtonVariant(m_startProcessBtn,
                             StyleTokens::Properties::kDanger);
        } else {
            m_startProcessBtn->setText(tr("处理"));
            m_startProcessBtn->setIcon(toolbarIcon(
                UiAssets::Glyph::Play, StyleTokens::Colors::kActionText,
                StyleTokens::Colors::kActionText));
            m_startProcessBtn->setToolTip(tr("使用当前参数开始处理"));
            m_startProcessBtn->setAccessibleName(tr("开始处理"));
            setButtonVariant(m_startProcessBtn,
                             StyleTokens::Properties::kPrimary);
        }
    }
    updateButtonStates();
}

void Toolbar::setProjectSummary(const QString& summary) {
    // Compatibility only: the 36px command bar deliberately has no project
    // summary. MainWindow may continue calling this during staged migration.
    Q_UNUSED(summary)
}

void Toolbar::updateButtonStates() {
    if (m_startProcessBtn) {
        // During processing this remains enabled and emits the same signal;
        // MainWindow owns cancellation and task lifetime.
        m_startProcessBtn->setEnabled(m_processing || m_canProcess);
    }
    if (m_exportResultBtn) {
        m_exportResultBtn->setEnabled(m_canExport && !m_processing);
    }
    if (m_importFilesBtn) m_importFilesBtn->setEnabled(!m_processing);
    if (m_importFolderBtn) m_importFolderBtn->setEnabled(!m_processing);
    if (m_clearProjectAction) {
        m_clearProjectAction->setEnabled(!m_processing);
    }
    if (m_settingsAction) m_settingsAction->setEnabled(!m_processing);
}

void Toolbar::showOverflowMenu() {
    if (!m_overflowMenu || !m_overflowBtn) return;

    const QSize menuSize = m_overflowMenu->sizeHint();
    const QPoint anchor = m_overflowBtn->mapToGlobal(
        QPoint(m_overflowBtn->width() - menuSize.width(),
               m_overflowBtn->height()));
    m_overflowMenu->popup(anchor);
}

void Toolbar::setButtonVariant(QPushButton* button, const char* variant) {
    if (!button) return;
    button->setProperty(StyleTokens::Properties::kVariant, variant);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}
