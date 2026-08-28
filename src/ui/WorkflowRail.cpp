#include "WorkflowRail.h"

#include "StyleTokens.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFont>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace {

struct StageText {
    const char* number;
    const char* source;
};

constexpr std::array<StageText, 6> kStageText{{
    {"01", QT_TRANSLATE_NOOP("Nebulens", "素材")},
    {"02", QT_TRANSLATE_NOOP("Nebulens", "校准")},
    {"03", QT_TRANSLATE_NOOP("Nebulens", "对齐")},
    {"04", QT_TRANSLATE_NOOP("Nebulens", "合成")},
    {"05", QT_TRANSLATE_NOOP("Nebulens", "调整")},
    {"06", QT_TRANSLATE_NOOP("Nebulens", "输出")}
}};

int stageIndex(WorkflowStage stage) {
    return std::clamp(static_cast<int>(stage), 0, 5);
}

} // namespace

class WorkflowStageButton final : public QAbstractButton {
public:
    WorkflowStageButton(WorkflowStage stage, QWidget* parent)
        : QAbstractButton(parent), m_stage(stage) {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(76, 57);
        const StageText& text = kStageText[stageIndex(stage)];
        const QString label = QCoreApplication::translate(
            "Nebulens", text.source);
        setAccessibleName(label);
        setToolTip(label);
    }

    WorkflowStage stage() const { return m_stage; }

    void setComplete(bool complete) {
        if (m_complete == complete) return;
        m_complete = complete;
        update();
    }

    void setWarning(bool warning) {
        if (m_warning == warning) return;
        m_warning = warning;
        update();
    }

protected:
    void focusInEvent(QFocusEvent* event) override {
        m_keyboardFocus =
            event->reason() == Qt::TabFocusReason ||
            event->reason() == Qt::BacktabFocusReason ||
            event->reason() == Qt::ShortcutFocusReason;
        setProperty("keyboardFocusVisible", m_keyboardFocus);
        update();
        QAbstractButton::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override {
        m_keyboardFocus = false;
        setProperty("keyboardFocusVisible", false);
        update();
        QAbstractButton::focusOutEvent(event);
    }

    void enterEvent(QEnterEvent* event) override {
        m_hovered = true;
        update();
        QAbstractButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        m_hovered = false;
        update();
        QAbstractButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF body = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

        if (isChecked()) {
            QLinearGradient fill(body.topLeft(), body.topRight());
            QColor start = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kAccent);
            start.setAlpha(72);
            QColor end = start;
            end.setAlpha(5);
            fill.setColorAt(0.0, start);
            fill.setColorAt(1.0, end);
            painter.setBrush(fill);
            QColor border = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kAccent);
            border.setAlpha(142);
            painter.setPen(QPen(border, 1.0));
            painter.drawRoundedRect(body, 6.0, 6.0);

            QColor glow = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kAccentStrong);
            for (int width = 7; width >= 1; width -= 2) {
                glow.setAlpha(18 + (7 - width) * 7);
                painter.setPen(QPen(glow, width, Qt::SolidLine,
                                    Qt::RoundCap));
                painter.drawLine(QPointF(1.5, 12.0),
                                 QPointF(1.5, height() - 12.0));
            }
        } else if (m_hovered) {
            QColor hover = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kBackgroundOverlay);
            hover.setAlpha(150);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            painter.drawRoundedRect(body, 6.0, 6.0);
        }

        const StageText& text = kStageText[stageIndex(m_stage)];
        const QColor primary = isChecked()
            ? StyleTokens::Colors::fromHex(StyleTokens::Colors::kAccentStrong)
            : StyleTokens::Colors::fromHex(
                  m_hovered ? StyleTokens::Colors::kTextPrimary
                            : StyleTokens::Colors::kTextSecondary);
        const QColor faint = StyleTokens::Colors::fromHex(
            StyleTokens::Colors::kTextFaint);

        QFont numberFont = font();
        QString monoFamily = StyleTokens::Typography::monoFontFamily();
        monoFamily.remove(QLatin1Char('"'));
        numberFont.setFamily(monoFamily);
        numberFont.setPixelSize(8);
        painter.setFont(numberFont);
        painter.setPen(isChecked() ? primary : faint);
        painter.drawText(QRect(8, 0, 20, height()), Qt::AlignVCenter,
                         QString::fromLatin1(text.number));

        const QString localizedLabel = QCoreApplication::translate(
            "Nebulens", text.source);
        QFont labelFont = font();
        labelFont.setPixelSize(localizedLabel.size() > 6 ? 8 : 11);
        labelFont.setWeight(QFont::DemiBold);
        painter.setFont(labelFont);
        painter.setPen(primary);
        painter.drawText(QRect(27, 18, 46, 20),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         localizedLabel);

        QColor dot = StyleTokens::Colors::fromHex(
            StyleTokens::Colors::kTextFaint);
        if (m_warning) {
            dot = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kWarning);
        } else if (m_complete) {
            dot = StyleTokens::Colors::fromHex(StyleTokens::Colors::kOk);
        } else if (isChecked()) {
            dot = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kAccentStrong);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot);
        painter.drawEllipse(QRectF(width() - 10.0, 7.0, 4.0, 4.0));

        if (hasFocus() && m_keyboardFocus) {
            QColor focus = StyleTokens::Colors::fromHex(
                StyleTokens::Colors::kAccentStrong);
            focus.setAlpha(180);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(focus, 1.0));
            painter.drawRoundedRect(body.adjusted(1.0, 1.0, -1.0, -1.0),
                                    5.0, 5.0);
        }
    }

private:
    WorkflowStage m_stage;
    bool m_hovered = false;
    bool m_complete = false;
    bool m_warning = false;
    bool m_keyboardFocus = false;
};

WorkflowRail::WorkflowRail(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void WorkflowRail::setupUi() {
    setObjectName(QStringLiteral("workflowRail"));
    setFixedWidth(96);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 14, 10, 10);
    layout->setSpacing(7);

    auto* kicker = new QLabel(
        QCoreApplication::translate("Nebulens", "工作流"), this);
    kicker->setObjectName(QStringLiteral("workflowKicker"));
    kicker->setAlignment(Qt::AlignCenter);
    layout->addWidget(kicker);

    for (int index = 0; index < static_cast<int>(m_buttons.size()); ++index) {
        const auto stage = static_cast<WorkflowStage>(index);
        auto* button = new WorkflowStageButton(stage, this);
        m_buttons[index] = button;
        connect(button, &QAbstractButton::clicked, this, [this, stage]() {
            setCurrentStage(stage, true);
        });
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }
    layout->addStretch(1);
    setCurrentStage(m_currentStage);
}

WorkflowStageButton* WorkflowRail::buttonFor(WorkflowStage stage) const {
    return m_buttons[stageIndex(stage)];
}

void WorkflowRail::setCurrentStage(WorkflowStage stage, bool notify) {
    WorkflowStageButton* target = buttonFor(stage);
    if (!target || target->isHidden()) return;
    m_currentStage = stage;
    for (WorkflowStageButton* button : m_buttons) {
        if (button) button->setChecked(button == target);
    }
    if (notify) emit stageChanged(stage);
}

void WorkflowRail::setScene(ProcessingScene scene) {
    m_scene = scene;
    updateVisibility();
}

void WorkflowRail::updateVisibility() {
    const bool singleFrame = m_scene == ProcessingScene::SingleFrame;
    const bool starTrail = m_scene == ProcessingScene::StarTrail;
    const bool timelapse = m_scene == ProcessingScene::Timelapse;
    const bool sequence = starTrail || timelapse;
    buttonFor(WorkflowStage::Calibration)->setVisible(
        !singleFrame && !sequence);
    buttonFor(WorkflowStage::Alignment)->setVisible(
        !singleFrame && !sequence);
    buttonFor(WorkflowStage::Stacking)->setVisible(!singleFrame);
    buttonFor(WorkflowStage::Adjustment)->setVisible(!timelapse);
    if (singleFrame && (m_currentStage == WorkflowStage::Calibration ||
                        m_currentStage == WorkflowStage::Alignment ||
                        m_currentStage == WorkflowStage::Stacking)) {
        setCurrentStage(WorkflowStage::Adjustment, true);
    } else if (sequence &&
               (m_currentStage == WorkflowStage::Calibration ||
                m_currentStage == WorkflowStage::Alignment ||
                (timelapse &&
                 m_currentStage == WorkflowStage::Adjustment))) {
        setCurrentStage(WorkflowStage::Stacking, true);
    }
}

void WorkflowRail::setStageComplete(WorkflowStage stage, bool complete) {
    if (auto* button = buttonFor(stage)) button->setComplete(complete);
}

void WorkflowRail::setStageWarning(WorkflowStage stage, bool warning) {
    if (auto* button = buttonFor(stage)) button->setWarning(warning);
}
