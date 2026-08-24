#include "Toolbar.h"

#include "StyleTokens.h"
#include "UiAssets.h"

#include <QAction>
#include <QComboBox>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

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
    setFixedHeight(64);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 0, 14, 0);
    layout->setSpacing(10);

    auto* logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("brandLogo"));
    logo->setFixedSize(32, 32);
    logo->setPixmap(UiAssets::logoMark(30));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto* brand = new QWidget(this);
    brand->setObjectName(QStringLiteral("brandBlock"));
    brand->setFixedWidth(142);
    auto* brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(0, 8, 0, 7);
    brandLayout->setSpacing(1);
    auto* brandName = new QLabel(QString::fromUtf8("星炼"), brand);
    brandName->setObjectName(QStringLiteral("brandName"));
    brandLayout->addWidget(brandName);
    auto* brandTag = new QLabel(QStringLiteral("ASTRO IMAGING STUDIO"), brand);
    brandTag->setObjectName(QStringLiteral("brandTag"));
    brandLayout->addWidget(brandTag);
    layout->addWidget(brand);

    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("toolbarVerticalDivider"));
    divider->setFixedSize(1, 30);
    layout->addWidget(divider);

    auto* modeBlock = new QWidget(this);
    auto* modeLayout = new QVBoxLayout(modeBlock);
    modeLayout->setContentsMargins(0, 7, 0, 6);
    modeLayout->setSpacing(2);
    auto* modeLabel = new QLabel(QStringLiteral("MODE / 处理场景"), modeBlock);
    modeLabel->setObjectName(QStringLiteral("toolbarKicker"));
    modeLayout->addWidget(modeLabel);
    m_sceneCombo = new QComboBox(modeBlock);
    m_sceneCombo->setObjectName(QStringLiteral("toolbarSceneCombo"));
    m_sceneCombo->setFixedWidth(154);
    m_sceneCombo->addItem(QString::fromUtf8("单张精修"),
                          static_cast<int>(ProcessingScene::SingleFrame));
    m_sceneCombo->addItem(QString::fromUtf8("银河星景"),
                          static_cast<int>(ProcessingScene::Nightscape));
    m_sceneCombo->addItem(QString::fromUtf8("深空天体"),
                          static_cast<int>(ProcessingScene::DeepSky));
    m_sceneCombo->addItem(QString::fromUtf8("天地分离"),
                          static_cast<int>(ProcessingScene::SkyGround));
    m_sceneCombo->addItem(QString::fromUtf8("星轨合成"),
                          static_cast<int>(ProcessingScene::StarTrail));
    m_sceneCombo->addItem(QString::fromUtf8("延时序列"),
                          static_cast<int>(ProcessingScene::Timelapse));
    connect(m_sceneCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (index < 0) return;
                emit sceneChanged(static_cast<ProcessingScene>(
                    m_sceneCombo->itemData(index).toInt()));
            });
    modeLayout->addWidget(m_sceneCombo);
    layout->addWidget(modeBlock);

    m_projectSummary = new QLabel(QString::fromUtf8("银河星景 · 等待素材"), this);
    m_projectSummary->setObjectName(QStringLiteral("toolbarProjectSummary"));
    m_projectSummary->setSizePolicy(QSizePolicy::Ignored,
                                    QSizePolicy::Preferred);
    layout->addWidget(m_projectSummary, 1);


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
        tr("开始处理"), StyleTokens::Properties::kPrimary);
    m_startProcessBtn->setToolTip(tr("使用当前参数开始处理"));
    m_startProcessBtn->setAccessibleName(tr("开始处理"));
    connect(m_startProcessBtn, &QPushButton::clicked, this,
            &Toolbar::startProcessClicked);
    layout->addWidget(m_exportResultBtn);
    layout->addWidget(m_startProcessBtn);

    m_processGlow = new QGraphicsDropShadowEffect(m_startProcessBtn);
    m_processGlow->setBlurRadius(18.0);
    QColor glow = StyleTokens::Colors::fromHex(
        StyleTokens::Colors::kAccent);
    glow.setAlpha(80);
    m_processGlow->setColor(glow);
    m_processGlow->setOffset(0, 0);
    m_startProcessBtn->setGraphicsEffect(m_processGlow);

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
    m_importFilesAction = m_overflowMenu->addAction(tr("导入 RAW 文件..."));
    m_importFolderAction = m_overflowMenu->addAction(tr("导入目录..."));
    m_overflowMenu->addSeparator();
    m_clearProjectAction = m_overflowMenu->addAction(tr("清空项目"));
    m_overflowMenu->addSeparator();
    m_settingsAction = m_overflowMenu->addAction(tr("设置"));
    m_checkUpdatesAction = m_overflowMenu->addAction(tr("检查更新"));
    m_shortcutsAction = m_overflowMenu->addAction(tr("快捷键"));
    m_overflowMenu->addSeparator();
    m_aboutAction = m_overflowMenu->addAction(tr("关于"));

    connect(m_importFilesAction, &QAction::triggered, this,
            &Toolbar::importFilesClicked);
    connect(m_importFolderAction, &QAction::triggered, this,
            &Toolbar::importFolderClicked);
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
    m_progress = 0;
    if (m_startProcessBtn) {
        if (processing) {
            m_startProcessBtn->setText(tr("取消处理"));
            m_startProcessBtn->setIcon(QIcon());
            m_startProcessBtn->setToolTip(tr("取消当前处理任务"));
            m_startProcessBtn->setAccessibleName(tr("取消处理"));
            setButtonVariant(m_startProcessBtn,
                             StyleTokens::Properties::kSecondaryButton);
            if (m_processGlow) {
                QColor active = StyleTokens::Colors::fromHex(
                    StyleTokens::Colors::kOk);
                active.setAlpha(72);
                m_processGlow->setColor(active);
            }
        } else {
            m_startProcessBtn->setText(tr("开始处理"));
            m_startProcessBtn->setIcon(toolbarIcon(
                UiAssets::Glyph::Play, StyleTokens::Colors::kActionText,
                StyleTokens::Colors::kActionText));
            m_startProcessBtn->setToolTip(tr("使用当前参数开始处理"));
            m_startProcessBtn->setAccessibleName(tr("开始处理"));
            setButtonVariant(m_startProcessBtn,
                             StyleTokens::Properties::kPrimary);
            if (m_processGlow) {
                QColor idle = StyleTokens::Colors::fromHex(
                    StyleTokens::Colors::kAccent);
                idle.setAlpha(80);
                m_processGlow->setColor(idle);
            }
        }
    }
    updateButtonStates();
}

void Toolbar::setProgress(int value) {
    m_progress = std::clamp(value, 0, 100);
    if (!m_processing || !m_startProcessBtn) return;
    m_startProcessBtn->setText(
        tr("正在处理 %1%").arg(m_progress));
    m_startProcessBtn->setAccessibleName(
        tr("取消处理，当前进度 %1%").arg(m_progress));
}

void Toolbar::setProjectSummary(const QString& summary) {
    if (!m_projectSummary) return;
    m_projectSummary->setText(summary);
    m_projectSummary->setToolTip(summary);
}

void Toolbar::setScene(ProcessingScene scene) {
    if (!m_sceneCombo) return;
    const int index = m_sceneCombo->findData(static_cast<int>(scene));
    if (index < 0) return;
    const QSignalBlocker blocker(m_sceneCombo);
    m_sceneCombo->setCurrentIndex(index);
}

void Toolbar::updateButtonStates() {
    if (m_startProcessBtn) {
        // During processing this remains enabled and emits the same signal;
        // MainWindow owns cancellation and task lifetime.
        const bool enabled = m_processing || m_canProcess;
        m_startProcessBtn->setEnabled(enabled);
        if (m_processGlow) m_processGlow->setEnabled(enabled);
    }
    if (m_exportResultBtn) {
        m_exportResultBtn->setEnabled(m_canExport && !m_processing);
    }
    if (m_sceneCombo) m_sceneCombo->setEnabled(!m_processing);
    if (m_importFilesAction) m_importFilesAction->setEnabled(!m_processing);
    if (m_importFolderAction) m_importFolderAction->setEnabled(!m_processing);
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
