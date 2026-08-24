#pragma once

#include "ProcessingScene.h"

#include <QWidget>

#include <array>

enum class WorkflowStage {
    Assets = 0,
    Calibration,
    Alignment,
    Stacking,
    Adjustment,
    Output
};
class WorkflowStageButton;

class WorkflowRail : public QWidget {
    Q_OBJECT
public:
    explicit WorkflowRail(QWidget* parent = nullptr);

    WorkflowStage currentStage() const { return m_currentStage; }
    void setCurrentStage(WorkflowStage stage, bool notify = false);
    void setScene(ProcessingScene scene);
    void setStageComplete(WorkflowStage stage, bool complete);
    void setStageWarning(WorkflowStage stage, bool warning);

signals:
    void stageChanged(WorkflowStage stage);

private:
    void setupUi();
    void updateVisibility();
    WorkflowStageButton* buttonFor(WorkflowStage stage) const;

    std::array<WorkflowStageButton*, 6> m_buttons{};
    WorkflowStage m_currentStage = WorkflowStage::Adjustment;
    ProcessingScene m_scene = ProcessingScene::Nightscape;
};
