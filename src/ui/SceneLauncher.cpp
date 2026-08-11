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
    SceneCard(const QString& index, UiAssets::Glyph glyph,
              const QColor& accent, const QString& requirement,
              const QString& title, const QString& summary,
              const QString& steps, std::function<void()> activate,
              QWidget* parent = nullptr)
        : QFrame(parent)
        , m_accent(accent.name())
        , m_activate(std::move(activate)) {
        setObjectName("sceneCard");
        setAttribute(Qt::WA_StyledBackground, true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(140);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAccessibleName(title);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(15, 14, 15, 14);
        layout->setSpacing(14);

        auto* accentBar = new QFrame(this);
        accentBar->setFixedSize(3, 88);
        accentBar->setStyleSheet(
            QString("background-color: %1; border: none; border-radius: 1px;")
                .arg(m_accent));
        accentBar->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(accentBar, 0, Qt::AlignVCenter);

        auto* icon = new QLabel(this);
        icon->setFixedSize(34, 34);
        icon->setAlignment(Qt::AlignCenter);
        icon->setPixmap(UiAssets::icon(glyph, accent, 25).pixmap(25, 25));
        icon->setStyleSheet("background-color: transparent; border: none;");
        icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(icon, 0, Qt::AlignTop);

        auto* body = new QVBoxLayout();
        body->setContentsMargins(0, 0, 0, 0);
        body->setSpacing(6);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(8);

        auto* indexLabel = new QLabel(index, this);
        indexLabel->setStyleSheet(QString(
            "color: %1; font-size: 9px; font-weight: 700; "
            "background-color: transparent; border: none;").arg(m_accent));
        indexLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(indexLabel, 0, Qt::AlignVCenter);

        auto* titleLabel = new QLabel(title, this);
        titleLabel->setStyleSheet(
            "color: #F1F5F4; font-size: 15px; font-weight: 700; "
            "background-color: transparent; border: none;");
        titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(titleLabel);
        titleRow->addStretch();

        auto* requirementDot = new QFrame(this);
        requirementDot->setFixedSize(5, 5);
        requirementDot->setStyleSheet(QString(
            "background-color: %1; border: none; border-radius: 2px;")
            .arg(m_accent));
        requirementDot->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(requirementDot, 0, Qt::AlignVCenter);

        auto* requirementLabel = new QLabel(requirement, this);
        requirementLabel->setStyleSheet(
            "color: #93A39F; font-size: 10px; font-weight: 600; "
            "background-color: transparent; border: none;");
        requirementLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        titleRow->addWidget(requirementLabel, 0, Qt::AlignVCenter);
        body->addLayout(titleRow);

        auto* summaryLabel = new QLabel(summary, this);
        summaryLabel->setWordWrap(true);
        summaryLabel->setStyleSheet(
            "color: #A8B5B2; font-size: 11px; background-color: transparent; "
            "border: none;");
        summaryLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        body->addWidget(summaryLabel);
        body->addStretch();

        auto* workflowRow = new QHBoxLayout();
        workflowRow->setContentsMargins(0, 0, 0, 0);
        workflowRow->setSpacing(8);
        auto* stepsLabel = new QLabel(steps, this);
        stepsLabel->setStyleSheet(
            "color: #71837F; font-size: 10px; background-color: transparent; "
            "border: none;");
        stepsLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        workflowRow->addWidget(stepsLabel);
        workflowRow->addStretch();

        auto* arrow = new QLabel(this);
        arrow->setFixedSize(20, 20);
        arrow->setAlignment(Qt::AlignCenter);
        arrow->setPixmap(
            UiAssets::icon(UiAssets::Glyph::ChevronRight, accent, 15)
                .pixmap(15, 15));
        arrow->setStyleSheet("background-color: transparent; border: none;");
        arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
        workflowRow->addWidget(arrow);
        body->addLayout(workflowRow);

        layout->addLayout(body, 1);
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
            "QFrame#sceneCard { background-color: %1; border: 1px solid %2; "
            "border-radius: 6px; }")
            .arg(hovered ? "#1D2626" : "#171D1E",
                 hovered ? "#455553" : "#293334"));
    }

    QString m_accent;
    std::function<void()> m_activate;
};

} // namespace

SceneLauncher::SceneLauncher(QWidget* parent)
    : QWidget(parent) {
    setObjectName("sceneLauncher");
    setStyleSheet(
        "QWidget#sceneLauncher { background-color: #121718; }"
        "QLabel { background-color: transparent; border: none; letter-spacing: 0; }"
    );

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(38, 24, 38, 30);
    outer->setSpacing(0);

    auto* content = new QWidget(this);
    content->setMinimumWidth(940);
    content->setMaximumWidth(1060);
    content->setStyleSheet("background-color: transparent;");
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(15);

    auto* headerAccent = new QFrame(content);
    headerAccent->setFixedSize(3, 62);
    headerAccent->setStyleSheet(
        "background-color: #54D5B0; border: none; border-radius: 1px;"
    );
    header->addWidget(headerAccent, 0, Qt::AlignVCenter);

    auto* heading = new QVBoxLayout();
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setSpacing(4);
    auto* eyebrow = new QLabel(QString::fromUtf8("新建处理流程"), content);
    eyebrow->setStyleSheet(
        "color: #79DCC1; font-size: 10px; font-weight: 700;"
    );
    heading->addWidget(eyebrow);

    auto* title = new QLabel(QString::fromUtf8("选择照片类型"), content);
    title->setStyleSheet(
        "color: #F2F6F5; font-size: 25px; font-weight: 700;"
    );
    heading->addWidget(title);

    auto* subtitle = new QLabel(
        QString::fromUtf8("每种工作流会准备匹配的素材要求、处理步骤与参数。"),
        content);
    subtitle->setStyleSheet("color: #8C9D99; font-size: 11px;");
    heading->addWidget(subtitle);
    header->addLayout(heading);
    header->addStretch();

    auto* workflowCount = new QLabel(QString::fromUtf8("6 个专业工作流"), content);
    workflowCount->setStyleSheet(
        "color: #738580; font-size: 10px; font-weight: 600;"
    );
    header->addWidget(workflowCount, 0, Qt::AlignBottom);
    contentLayout->addLayout(header);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    auto addCard = [this, content, grid](int row, int column,
                                        const QString& index,
                                        ProcessingScene scene,
                                        UiAssets::Glyph glyph,
                                        const QColor& accent,
                                        const QString& requirement,
                                        const QString& titleText,
                                        const QString& summary,
                                        const QString& steps) {
        auto* card = new SceneCard(
            index, glyph, accent, requirement, titleText, summary, steps,
            [this, scene]() { emit sceneSelected(scene); }, content);
        grid->addWidget(card, row, column);
    };

    addCard(0, 0, QStringLiteral("01"), ProcessingScene::SingleFrame,
            UiAssets::Glyph::SingleFrame, QColor("#65D7B6"),
            QString::fromUtf8("1 张 RAW"), QString::fromUtf8("单张 RAW 精修"),
            QString::fromUtf8("对单张照片进行色彩恢复、曲线拉伸、降噪与缩星。"),
            QString::fromUtf8("导入  /  调整  /  导出"));
    addCard(0, 1, QStringLiteral("02"), ProcessingScene::Nightscape,
            UiAssets::Glyph::Nightscape, QColor("#77B7D3"),
            QString::fromUtf8("2 张以上"), QString::fromUtf8("银河星景堆栈"),
            QString::fromUtf8("自动选择参考帧，对齐星空并降低广角银河序列噪声。"),
            QString::fromUtf8("素材  /  对齐  /  堆栈  /  优化"));
    addCard(1, 0, QStringLiteral("03"), ProcessingScene::DeepSky,
            UiAssets::Glyph::DeepSky, QColor("#A6B4DE"),
            QString::fromUtf8("Light + 校准帧"), QString::fromUtf8("深空天体堆栈"),
            QString::fromUtf8("使用 Bias、Dark 与 Flat 校准后完成稳健对齐和堆栈。"),
            QString::fromUtf8("校准  /  去马赛克  /  对齐  /  堆栈"));
    addCard(1, 1, QStringLiteral("04"), ProcessingScene::SkyGround,
            UiAssets::Glyph::SkyGround, QColor("#E4B86B"),
            QString::fromUtf8("固定机位"), QString::fromUtf8("天地分离合成"),
            QString::fromUtf8("天空跟随星点对齐，地景保留原坐标，避免山体拖影。"),
            QString::fromUtf8("蒙版  /  双路堆栈  /  边界融合"));
    addCard(2, 0, QStringLiteral("05"), ProcessingScene::StarTrail,
            UiAssets::Glyph::StarTrail, QColor("#DD8D79"),
            QString::fromUtf8("3 张以上"), QString::fromUtf8("星轨合成"),
            QString::fromUtf8("逐帧取亮生成连续或彗星式星轨，并单独保护地景。"),
            QString::fromUtf8("素材  /  星轨累积  /  地景融合"));
    addCard(2, 1, QStringLiteral("06"), ProcessingScene::Timelapse,
            UiAssets::Glyph::Timelapse, QColor("#73C7C5"),
            QString::fromUtf8("3 张以上"), QString::fromUtf8("星空延时序列降噪"),
            QString::fromUtf8("按滑动窗口对齐邻近帧，逐张输出稳定的降噪序列。"),
            QString::fromUtf8("预分析  /  时域降噪  /  图片序列"));

    contentLayout->addLayout(grid);
    outer->addStretch();
    outer->addWidget(content, 0, Qt::AlignHCenter);
    outer->addStretch();
}
