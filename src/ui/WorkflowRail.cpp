#include "WorkflowRail.h"

#include "StyleTokens.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>

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

constexpr int kStageInset = 8;
constexpr int kStageNumberWidth = 18;
constexpr int kStageMarkWidth = 12;

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
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        const StageText& text = kStageText[stageIndex(stage)];
        const QString label = QCoreApplication::translate(
            "Nebulens", text.source);
        setText(label);
        setAccessibleName(label);
        updateStatus();
    }

    QSize sizeHint() const override {
        return {QFontMetrics(labelFont()).horizontalAdvance(text())
                    + 2 * kStageInset + kStageNumberWidth + kStageMarkWidth,
                StyleTokens::Layout::kWorkflowIndexHeight - 3};
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

    void setComplete(bool complete) {
        if (m_complete == complete) return;
        m_complete = complete;
        updateStatus();
    }

    void setWarning(bool warning) {
        if (m_warning == warning) return;
        m_warning = warning;
        updateStatus();
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

    void changeEvent(QEvent* event) override {
        QAbstractButton::changeEvent(event);
        if (event->type() == QEvent::FontChange) updateGeometry();
        update();
    }

    void paintEvent(QPaintEvent*) override {
        using namespace StyleTokens::Colors;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor ink = fromHex(!isEnabled() ? kTextDisabled
                                  : isChecked() ? kAccentStrong : kTextSecondary);
        if (isEnabled() && (underMouse() || isDown())) {
            painter.fillRect(rect(), fromHex(kBackgroundOverlay));
        }
        if (isChecked()) {
            painter.fillRect(QRect(kStageInset, height() - 2,
                                   width() - 2 * kStageInset, 2), ink);
        }

        const StageText& text = kStageText[stageIndex(m_stage)];
        QFont numberFont = font();
        QString monoFamily = StyleTokens::Typography::monoFontFamily();
        monoFamily.remove(QLatin1Char('"'));
        numberFont.setFamily(monoFamily);
        numberFont.setPixelSize(StyleTokens::Typography::kCaptionSize);
        painter.setFont(numberFont);
        painter.setPen(isChecked() ? ink : fromHex(kTextFaint));
        painter.drawText(QRect(kStageInset, 0, kStageNumberWidth, height()), Qt::AlignVCenter,
                         QString::fromLatin1(text.number));

        const int labelStart = kStageInset + kStageNumberWidth;
        const QRect labelRect = rect().adjusted(labelStart, 0, -kStageInset - kStageMarkWidth, 0);
        painter.setFont(labelFont());
        painter.setPen(ink);
        painter.drawText(labelRect, Qt::AlignLeading | Qt::AlignVCenter, this->text());

        if (m_warning || m_complete) {
            const qreal x = width() - kStageInset - 3.0;
            const qreal y = height() / 2.0;
            painter.setPen(QPen(fromHex(!isEnabled() ? kTextDisabled
                                        : m_warning ? kWarning : kOk),
                               1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            if (m_warning) {
                painter.drawLine(QPointF(x, y - 4), QPointF(x, y));
                painter.drawPoint(QPointF(x, y + 3));
            } else {
                QPainterPath mark(QPointF(x - 5, y));
                mark.lineTo(x - 2, y + 3);
                mark.lineTo(x + 3, y - 3);
                painter.drawPath(mark);
            }
        }

        if (hasFocus() && m_keyboardFocus) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(fromHex(kFocus), 1.5));
            painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -3), 3, 3);
        }
    }

private:
    QFont labelFont() const {
        QFont result = font();
        result.setPixelSize(StyleTokens::Typography::kBodySize);
        result.setWeight(QFont::DemiBold);
        return result;
    }

    void updateStatus() {
        QString status;
        if (m_warning) status = QCoreApplication::translate("Nebulens", "来源不完整");
        else if (m_complete) status = QCoreApplication::translate("Nebulens", "已就绪");
        setAccessibleDescription(status);
        setToolTip(status.isEmpty() ? text() : text() + QStringLiteral("\n") + status);
        update();
    }

    WorkflowStage m_stage;
    bool m_complete = false;
    bool m_warning = false;
    bool m_keyboardFocus = false;
};

WorkflowRail::WorkflowRail(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void WorkflowRail::setupUi() {
    setObjectName(QStringLiteral("workflowRail"));
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(StyleTokens::Layout::kWorkflowIndexHeight);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 1, 0, 2);
    layout->setSpacing(StyleTokens::Spacing::kMicro);

    auto* kicker = new QLabel(
        QCoreApplication::translate("Nebulens", "工作流"), this);
    kicker->setObjectName(QStringLiteral("workflowKicker"));
    kicker->setProperty(StyleTokens::Properties::kTextRole, StyleTokens::Properties::kCaption);
    kicker->setContentsMargins(0, 0, StyleTokens::Spacing::kBase, 0);
    layout->addWidget(kicker);

    for (int index = 0; index < static_cast<int>(m_buttons.size()); ++index) {
        const auto stage = static_cast<WorkflowStage>(index);
        auto* button = new WorkflowStageButton(stage, this);
        button->installEventFilter(this);
        m_buttons[index] = button;
        connect(button, &QAbstractButton::clicked, this, [this, stage]() {
            setCurrentStage(stage, true);
        });
        layout->addWidget(button);
    }
    layout->addStretch(1);
    setCurrentStage(m_currentStage);
}

bool WorkflowRail::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const int step = key->key() == Qt::Key_Left ? -1 : key->key() == Qt::Key_Right ? 1 : 0;
        const auto it = std::find(m_buttons.cbegin(), m_buttons.cend(), watched);
        if (step != 0 && key->modifiers() == Qt::NoModifier && it != m_buttons.cend()) {
            const int count = static_cast<int>(m_buttons.size());
            const auto start = static_cast<int>(it - m_buttons.cbegin());
            for (int offset = 1; offset < count; ++offset) {
                const int next = (start + step * offset + count) % count;
                auto* button = m_buttons[next];
                if (button->isHidden() || !button->isEnabled()) continue;
                button->setFocus(Qt::ShortcutFocusReason);
                setCurrentStage(static_cast<WorkflowStage>(next), true);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
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
