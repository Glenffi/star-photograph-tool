#include "Toolbar.h"

#include "StyleTokens.h"
#include "UiAssets.h"

#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>

#include <algorithm>

namespace {

QIcon toolbarIcon(UiAssets::Glyph glyph, const char* color) {
    return UiAssets::controlIcon(glyph, StyleTokens::Colors::fromHex(color),
                                 StyleTokens::Controls::kIconSize);
}

class ProjectSummaryLabel final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignLeading | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight,
                                                  contentsRect().width()));
    }
};

QString appendixTitle() {
    const QString title = QCoreApplication::translate("Nebulens", "参数附页");
    if (title != QStringLiteral("参数附页")) return title;
    // Older catalogs already translate the entry point into this appendix.
    const QString existing = QCoreApplication::translate("Nebulens", "高级设置");
    return existing == QStringLiteral("高级设置") ? title : existing;
}

}  // namespace

Toolbar::Toolbar(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void Toolbar::setupUI() {
    setObjectName(QStringLiteral("mainToolbar"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, StyleTokens::Spacing::kMicro,
                               0, StyleTokens::Spacing::kBase);
    layout->setSpacing(StyleTokens::Spacing::kControlGap);

    auto* orderButton = createActionButton(
        toolbarIcon(UiAssets::Glyph::ChevronLeft, StyleTokens::Colors::kAccent),
        QCoreApplication::translate("Nebulens", "返回冲印单"),
        StyleTokens::Properties::kGhost);
    orderButton->setObjectName(QStringLiteral("returnToPrintOrder"));
    orderButton->setAccessibleName(orderButton->text());
    connect(orderButton, &QPushButton::clicked, this, &Toolbar::printOrderRequested);
    layout->addWidget(orderButton);

    auto* divider = new QFrame(this);
    divider->setObjectName(QStringLiteral("toolbarVerticalDivider"));
    divider->setFixedSize(1, 22);
    layout->addWidget(divider);

    auto* title = new QLabel(appendixTitle(), this);
    title->setObjectName(QStringLiteral("toolbarKicker"));
    title->setProperty(StyleTokens::Properties::kTextRole,
                       StyleTokens::Properties::kSection);
    layout->addWidget(title);

    m_sceneCombo = new QComboBox(this);
    m_sceneCombo->setObjectName(QStringLiteral("toolbarSceneCombo"));
    m_sceneCombo->setAccessibleName(
        QCoreApplication::translate("Nebulens", "模式 / 处理场景"));
    m_sceneCombo->setToolTip(m_sceneCombo->accessibleName());
    m_sceneCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_sceneCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_sceneCombo->setFocusPolicy(Qt::StrongFocus);
    m_sceneCombo->addItem(QCoreApplication::translate("Nebulens", "单张精修"),
                          static_cast<int>(ProcessingScene::SingleFrame));
    m_sceneCombo->addItem(QCoreApplication::translate("Nebulens", "银河星景"),
                          static_cast<int>(ProcessingScene::Nightscape));
    m_sceneCombo->addItem(QCoreApplication::translate("Nebulens", "深空天体"),
                          static_cast<int>(ProcessingScene::DeepSky));
    m_sceneCombo->addItem(QCoreApplication::translate("Nebulens", "天地分离"),
                          static_cast<int>(ProcessingScene::SkyGround));
    m_sceneCombo->addItem(QCoreApplication::translate("Nebulens", "星轨合成"),
                          static_cast<int>(ProcessingScene::StarTrail));
    m_sceneCombo->addItem(QCoreApplication::translate("Nebulens", "延时序列"),
                          static_cast<int>(ProcessingScene::Timelapse));
    connect(m_sceneCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (index < 0) return;
                emit sceneChanged(static_cast<ProcessingScene>(
                    m_sceneCombo->itemData(index).toInt()));
            });
    layout->addWidget(m_sceneCombo);

    m_projectSummary = new ProjectSummaryLabel(this);
    m_projectSummary->setObjectName(QStringLiteral("toolbarProjectSummary"));
    m_projectSummary->setSizePolicy(QSizePolicy::Ignored,
                                    QSizePolicy::Preferred);
    setProjectSummary(QCoreApplication::translate("Nebulens", "银河星景 · 等待素材"));
    layout->addWidget(m_projectSummary, 1);

    m_exportResultBtn = createActionButton(
        toolbarIcon(UiAssets::Glyph::Export, StyleTokens::Colors::kTextPrimary),
        tr("导出"), StyleTokens::Properties::kSecondaryButton);
    m_exportResultBtn->setToolTip(tr("导出当前处理结果"));
    m_exportResultBtn->setAccessibleName(tr("导出结果"));
    connect(m_exportResultBtn, &QPushButton::clicked, this,
            &Toolbar::exportResultClicked);

    m_startProcessBtn = createActionButton(
        toolbarIcon(UiAssets::Glyph::Play, StyleTokens::Colors::kActionText),
        tr("开始处理"), StyleTokens::Properties::kPrimary);
    m_startProcessBtn->setToolTip(tr("使用当前参数开始处理"));
    m_startProcessBtn->setAccessibleName(tr("开始处理"));
    connect(m_startProcessBtn, &QPushButton::clicked, this,
            &Toolbar::startProcessClicked);
    // Keep adjacent commands in place as the process button changes state.
    const QString idleText = m_startProcessBtn->text();
    int actionWidth = m_startProcessBtn->sizeHint().width();
    for (const QString& text : {tr("取消处理"), tr("正在处理 %1%").arg(100)}) {
        m_startProcessBtn->setText(text);
        actionWidth = std::max(actionWidth, m_startProcessBtn->sizeHint().width());
    }
    m_startProcessBtn->setText(idleText);
    m_startProcessBtn->setMinimumWidth(actionWidth);
    layout->addWidget(m_exportResultBtn);
    layout->addWidget(m_startProcessBtn);

    auto* overflow = createIconButton(
        toolbarIcon(UiAssets::Glyph::More, StyleTokens::Colors::kTextPrimary),
        tr("更多"));
    overflow->setObjectName(QStringLiteral("toolbarMore"));
    layout->addWidget(overflow);

    auto* menu = new QMenu(overflow);
    overflow->setMenu(menu);
    m_importFilesAction = menu->addAction(tr("导入 RAW 文件..."));
    m_importFolderAction = menu->addAction(tr("导入目录..."));
    menu->addSeparator();
    m_clearProjectAction = menu->addAction(tr("清空项目"));
    menu->addSeparator();
    m_settingsAction = menu->addAction(tr("设置"));
    m_checkUpdatesAction = menu->addAction(tr("检查更新"));
    m_shortcutsAction = menu->addAction(tr("快捷键"));
    menu->addSeparator();
    m_aboutAction = menu->addAction(tr("关于"));

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
    button->setFocusPolicy(Qt::StrongFocus);
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
    button->setFocusPolicy(Qt::StrongFocus);
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
            m_startProcessBtn->setIcon(toolbarIcon(
                UiAssets::Glyph::Stop, StyleTokens::Colors::kTextPrimary));
            m_startProcessBtn->setToolTip(tr("取消当前处理任务"));
            m_startProcessBtn->setAccessibleName(tr("取消处理"));
            setButtonVariant(m_startProcessBtn,
                             StyleTokens::Properties::kSecondaryButton);
        } else {
            m_startProcessBtn->setText(tr("开始处理"));
            m_startProcessBtn->setIcon(toolbarIcon(
                UiAssets::Glyph::Play, StyleTokens::Colors::kActionText));
            m_startProcessBtn->setToolTip(tr("使用当前参数开始处理"));
            m_startProcessBtn->setAccessibleName(tr("开始处理"));
            setButtonVariant(m_startProcessBtn,
                             StyleTokens::Properties::kPrimary);
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
        m_startProcessBtn->setEnabled(m_processing || m_canProcess);
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

void Toolbar::setButtonVariant(QPushButton* button, const char* variant) {
    if (!button) return;
    button->setProperty(StyleTokens::Properties::kVariant, variant);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}
