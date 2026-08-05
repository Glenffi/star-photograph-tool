#include "SceneLauncher.h"
#include "UiAssets.h"

#include <QEnterEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace {

class SceneCard final : public QFrame {
public:
    SceneCard(UiAssets::Glyph glyph, const QColor& accent,
              const QString& requirement, const QString& title,
              const QString& summary, const QString& steps,
              std::function<void()> activate, QWidget* parent = nullptr)
        : QFrame(parent)
        , m_accent(accent.name())
        , m_activate(std::move(activate)) {
        setObjectName("sceneCard");
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(190);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(title);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 15);
        layout->setSpacing(8);

        auto* topRow = new QHBoxLayout();
        topRow->setSpacing(10);
        auto* icon = new QLabel(this);
        icon->setFixedSize(46, 46);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(UiAssets::icon(glyph, accent, 28).pixmap(28, 28));
        icon->setStyleSheet(QString(
            "background-color: #1B2527; border: 1px solid %1; border-radius: 6px;")
            .arg(m_accent));
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        topRow->addWidget(icon);
        topRow->addStretch();

        auto* requirementLabel = new QLabel(requirement, this);
        requirementLabel->setStyleSheet(QString(
            "color: %1; background-color: #151C1E; border: 1px solid #2B393B; "
            "border-radius: 4px; padding: 3px 7px; font-size: 10px; font-weight: 600;")
            .arg(m_accent));
        requirementLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        topRow->addWidget(requirementLabel, 0, Qt::AlignTop);
        layout->addLayout(topRow);

        auto* titleLabel = new QLabel(title, this);
        titleLabel->setStyleSheet(
            "color: #F3F7F6; font-size: 17px; font-weight: 700; background: transparent;");
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(titleLabel);

        auto* summaryLabel = new QLabel(summary, this);
        summaryLabel->setWordWrap(true);
        summaryLabel->setStyleSheet(
            "color: #91A39F; font-size: 11px; background: transparent;");
        summaryLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(summaryLabel);

        layout->addStretch();

        auto* bottomRow = new QHBoxLayout();
        auto* stepsLabel = new QLabel(steps, this);
        stepsLabel->setStyleSheet(
            "color: #718681; font-size: 10px; background: transparent;");
        stepsLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        bottomRow->addWidget(stepsLabel);
        bottomRow->addStretch();
        auto* arrow = new QLabel(this);
        arrow->setPixmap(
            UiAssets::icon(UiAssets::Glyph::ChevronRight, accent, 16)
                .pixmap(16, 16));
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        bottomRow->addWidget(arrow);
        layout->addLayout(bottomRow);

        updateStyle(false);
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        updateStyle(true);
        QFrame::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        updateStyle(false);
        QFrame::leaveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) &&
            m_activate) {
            m_activate();
        }
        QFrame::mouseReleaseEvent(event);
    }

private:
    void updateStyle(bool hovered) {
        setStyleSheet(QString(
            "#sceneCard { background-color: %1; border: 1px solid %2; "
            "border-radius: 6px; }")
            .arg(hovered ? "#1B2527" : "#171F21",
                 hovered ? m_accent : "#2B393B"));
    }

    QString m_accent;
    std::function<void()> m_activate;
};

} // namespace

SceneLauncher::SceneLauncher(QWidget* parent)
    : QWidget(parent) {
    setStyleSheet("SceneLauncher { background-color: #111719; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(34, 28, 34, 32);
    outer->setSpacing(0);

    auto* content = new QWidget(this);
    content->setMinimumWidth(900);
    content->setMaximumWidth(980);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(18);

    auto* eyebrow = new QLabel(QString::fromUtf8("处理场景"), content);
    eyebrow->setStyleSheet(
        "color: #4ED7AE; font-size: 10px; font-weight: 700; background: transparent;");
    contentLayout->addWidget(eyebrow);

    auto* title = new QLabel(QString::fromUtf8("这次要处理什么照片？"), content);
    title->setStyleSheet(
        "color: #F3F7F6; font-size: 24px; font-weight: 700; background: transparent;");
    contentLayout->addWidget(title);

    auto* subtitle = new QLabel(
        QString::fromUtf8("选择拍摄场景后，工作台只保留当前流程需要的步骤与参数。"),
        content);
    subtitle->setStyleSheet(
        "color: #91A39F; font-size: 12px; background: transparent;");
    contentLayout->addWidget(subtitle);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    auto addCard = [this, content, grid](int row, int column,
                                        ProcessingScene scene,
                                        UiAssets::Glyph glyph,
                                        const QColor& accent,
                                        const QString& requirement,
                                        const QString& titleText,
                                        const QString& summary,
                                        const QString& steps) {
        auto* card = new SceneCard(
            glyph, accent, requirement, titleText, summary, steps,
            [this, scene]() { emit sceneSelected(scene); }, content);
        grid->addWidget(card, row, column);
    };

    addCard(0, 0, ProcessingScene::SingleFrame,
            UiAssets::Glyph::SingleFrame, QColor("#4ED7AE"),
            QString::fromUtf8("1 张 RAW"), QString::fromUtf8("单张 RAW 精修"),
            QString::fromUtf8("对单张照片执行降噪、曲线拉伸与缩星，不进行对齐堆栈。"),
            QString::fromUtf8("导入  →  调整  →  导出"));
    addCard(0, 1, ProcessingScene::Nightscape,
            UiAssets::Glyph::Nightscape, QColor("#6FA8FF"),
            QString::fromUtf8("2 张以上"), QString::fromUtf8("银河星景堆栈"),
            QString::fromUtf8("面向广角银河和纯天空序列，自动选参考帧并完成对齐降噪。"),
            QString::fromUtf8("素材  →  对齐  →  堆栈  →  优化"));
    addCard(1, 0, ProcessingScene::DeepSky,
            UiAssets::Glyph::DeepSky, QColor("#B397FF"),
            QString::fromUtf8("建议 6 张+"), QString::fromUtf8("深空天体堆栈"),
            QString::fromUtf8("为星云和星系启用长序列稳健堆栈、线性降噪与拉伸。"),
            QString::fromUtf8("校验  →  对齐  →  稳健堆栈  →  拉伸"));
    addCard(1, 1, ProcessingScene::SkyGround,
            UiAssets::Glyph::SkyGround, QColor("#F2B65A"),
            QString::fromUtf8("固定机位"), QString::fromUtf8("天地分离合成"),
            QString::fromUtf8("天空跟随星点对齐，地景保持原坐标，避免山体和建筑拖影。"),
            QString::fromUtf8("检测地平线  →  双路堆栈  →  融合"));

    contentLayout->addLayout(grid);
    outer->addStretch();
    outer->addWidget(content, 0, Qt::AlignHCenter);
    outer->addStretch();
}
