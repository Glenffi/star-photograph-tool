#pragma once

#include <QStringList>
#include <QWidget>
#include "core/AutoOptimizeEngine.h"
#include "core/PresetManager.h"
#include "core/SkyGroundMask.h"
#include "ui/ProcessingScene.h"
#include "ui/WorkflowRail.h"

class QScrollArea;
class QVBoxLayout;
class QComboBox;
class QSlider;
class QCheckBox;
class QGroupBox;
class QPushButton;
class QLabel;
class QTimer;
class QLineEdit;
class QTabWidget;
class QListWidget;
class QPropertyAnimation;
class QToolButton;

class ParamsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamsPanel(QWidget* parent = nullptr);

    QString stackMethod() const;
    double kappaValue() const;
    bool autoRejectLowQualityFrames() const;
    bool photometricNormalizationEnabled() const;
    bool dewarpEnabled() const;
    int dewarpStrength() const;
    bool noiseReductionEnabled() const;
    int noiseReductionStrength() const;
    bool modifiedCameraColorEnabled() const;
    int modifiedCameraColorStrength() const;
    ModifiedCameraNeutralMode modifiedCameraColorMode() const;
    bool hasModifiedCameraGrayPoint() const;
    double modifiedCameraGrayPointX() const;
    double modifiedCameraGrayPointY() const;
    void setModifiedCameraGrayPoint(double normalizedX, double normalizedY);
    void clearModifiedCameraGrayPoint();
    bool stretchEnabled() const;
    BasicAdjustmentOptions basicAdjustmentOptions() const;
    bool starDefringeEnabled() const;
    int starDefringeStrength() const;
    bool starReduceEnabled() const;
    int starReduceStrength() const;
    QString outputFormat() const;
    void setOutputFormat(const QString& format);  // tiff16 or png8
    QString outputPath() const;
    QString selectedReferenceFrame() const;

    void clearProjectSources();
    void updateRefFrameList(const QStringList& fileNames);
    void setSelectedReferenceFrame(const QString& filePath);
    void recommendStackMethod(int frameCount);
    void saveCurrentSettings();
    void setOutputPath(const QString& path);
    void setAssetSummary(int total, int available,
                         const QString& reference, bool ready);

    bool skyGroundSeparationEnabled() const;
    SkyGroundMask::Mode skyGroundMode() const;
    QString userMaskPath() const;
    int featherRadius() const;
    QString groundStackMethod() const;
    int groundDetailStrength() const;
    int timelapseWindowSize() const;
    int timelapseStrength() const;
    int timelapseMotionProtection() const;
    bool timelapseProtectGround() const;
    int starTrailCometStrength() const;
    bool starTrailReverse() const;
    bool starTrailProtectGround() const;
    QStringList darkFramePaths() const;
    QStringList flatFramePaths() const;
    QStringList biasFramePaths() const;
    QStringList darkFlatFramePaths() const;
    QString masterDarkPath() const;
    QString masterFlatPath() const;
    QString masterBiasPath() const;
    QString masterDarkFlatPath() const;
    bool deepSkyCalibrationInputsComplete() const;
    QString calibrationHint() const;
    bool saveGeneratedMasters() const;
    QString upstreamSignature() const;
    QString finishingSignature() const;
    QString processingSignature() const;
    void setMaskPreview(const std::vector<uint8_t>& mask, int w, int h);
    void setDetectMaskEnabled(bool enabled);
    void applySceneProfile(ProcessingScene scene,
                           bool resetParameters = true);
    void showCalibrationSettings();
    void showOutputSettings();
    void showWorkflowStage(WorkflowStage stage);
    void setProcessingReport(const QString& report);
    void refreshRecentResults();

signals:
    void paramsChanged();  // 参数发生任何变化时触发
    void referenceChoiceChanged(const QString& filePath);  // User activation; empty is automatic.
    void maskPreviewRequested(); // 用户点击"检测地景"时发射
    void modifiedCameraGrayPointRequested();
    void recentResultRequested(const QString& path);
    void revealResultRequested(const QString& path);

private slots:
    void onGroupToggled(bool checked);
    void onSliderValueChanged(int value);
    void onSliderReleased();
    void onComboChanged(int index);
    void onCheckChanged(int state);
    void onRestoreDefaults();
    void onSavePreset();
    void onPresetChanged(int index);
    void emitParamsChanged();

private:
    void setupUI();
    void loadPreset();
    void applyPreset(const Preset& preset);
    void loadCustomPresets();
    QGroupBox* createCollapsibleGroup(const QString& title, bool expanded = true);
    QSlider* createSlider(int min, int max, int value, const QString& suffix = QString());

    void updateSkyGroundControls();
    void updateModifiedCameraColorControls();
    void updateStackMethodDescription();
    void markPresetCustom();
    void importCalibrationFrames(QStringList& paths, QLabel* countLabel,
                                 QPushButton* clearButton,
                                 const QString& dialogTitle);
    void importCalibrationMaster(QString& masterPath,
                                 QStringList& rawPaths,
                                 QLabel* sourceLabel,
                                 QPushButton* clearButton,
                                 const QString& roleName);
    void clearCalibrationSource(QStringList& paths, QString& masterPath,
                                QLabel* sourceLabel,
                                QPushButton* clearButton);
    void clearCalibrationSources();
    void clearUserMask();
    void updateCalibrationStatus();
    QString* masterPathForRawGroup(const QStringList& paths);
    QString calibrationPathIdentity(const QString& path) const;
    bool calibrationPathUsedElsewhere(const QString& path,
                                      const QStringList* ignoredRaw,
                                      const QString* ignoredMaster) const;

    // 预设
    QWidget* m_presetBar = nullptr;
    QWidget* m_footerBar = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_stageKicker = nullptr;
    QTabWidget* m_tabs = nullptr;
    WorkflowStage m_activeWorkflowStage = WorkflowStage::Adjustment;
    ProcessingScene m_scene = ProcessingScene::Nightscape;
    QPropertyAnimation* m_stageAnimation = nullptr;
    QWidget* m_animatedPage = nullptr;

    // Read-only stage summaries make the six-stage rail meaningful without
    // duplicating the project model inside the inspector.
    QGroupBox* m_assetsGroup = nullptr;
    QGroupBox* m_lightCalibrationGroup = nullptr;
    QLabel* m_assetCountValue = nullptr;
    QLabel* m_assetThresholdValue = nullptr;
    QLabel* m_assetReferenceValue = nullptr;

    // 对齐组
    QGroupBox* m_alignGroup = nullptr;
    QComboBox* m_refFrame = nullptr;
    QString m_referenceChoice;
    QStringList m_referenceFiles;

    // 堆栈组
    QGroupBox* m_stackGroup = nullptr;
    QComboBox* m_stackAlgorithm = nullptr;
    QLabel* m_stackMethodDescription = nullptr;
    QLabel* m_kappaNameLabel = nullptr;
    QSlider* m_kappaSlider = nullptr;
    QLabel* m_kappaLabel = nullptr;
    QCheckBox* m_autoRejectQualityCheck = nullptr;
    QCheckBox* m_photometricCheck = nullptr;

    // 延时序列组
    QGroupBox* m_timelapseGroup = nullptr;
    QComboBox* m_timelapseWindow = nullptr;
    QSlider* m_timelapseStrengthSlider = nullptr;
    QLabel* m_timelapseStrengthLabel = nullptr;
    QSlider* m_timelapseMotionProtectionSlider = nullptr;
    QLabel* m_timelapseMotionProtectionLabel = nullptr;
    QCheckBox* m_timelapseProtectGroundCheck = nullptr;

    // 星轨合成组。星轨保持相机坐标，不参与普通的星点对齐和堆栈。
    QGroupBox* m_starTrailGroup = nullptr;
    QSlider* m_starTrailCometSlider = nullptr;
    QLabel* m_starTrailCometLabel = nullptr;
    QCheckBox* m_starTrailReverseCheck = nullptr;
    QCheckBox* m_starTrailProtectGroundCheck = nullptr;

    // 深空校准帧仅在当前会话内保存，避免下次启动引用已经移动的文件。
    QGroupBox* m_calibrationGroup = nullptr;
    QLabel* m_darkFrameCount = nullptr;
    QLabel* m_flatFrameCount = nullptr;
    QLabel* m_biasFrameCount = nullptr;
    QLabel* m_darkFlatFrameCount = nullptr;
    QLabel* m_calibrationStatus = nullptr;
    QWidget* m_biasFrameRow = nullptr;
    QWidget* m_darkFlatFrameRow = nullptr;
    QToolButton* m_calibrationDetailsButton = nullptr;
    QLabel* m_calibrationDetails = nullptr;
    QPushButton* m_darkFrameClear = nullptr;
    QPushButton* m_flatFrameClear = nullptr;
    QPushButton* m_biasFrameClear = nullptr;
    QPushButton* m_darkFlatFrameClear = nullptr;
    QCheckBox* m_saveGeneratedMastersCheck = nullptr;
    QStringList m_darkFramePaths;
    QStringList m_flatFramePaths;
    QStringList m_biasFramePaths;
    QStringList m_darkFlatFramePaths;
    QString m_masterDarkPath;
    QString m_masterFlatPath;
    QString m_masterBiasPath;
    QString m_masterDarkFlatPath;

    // 自动优化组
    QGroupBox* m_optimizeGroup = nullptr;
    QCheckBox* m_dewarpCheck = nullptr;
    QSlider* m_dewarpSlider = nullptr;
    QLabel* m_dewarpLabel = nullptr;
    QCheckBox* m_noiseReductionCheck = nullptr;
    QSlider* m_noiseReductionSlider = nullptr;
    QLabel* m_noiseReductionLabel = nullptr;
    QCheckBox* m_modifiedCameraColorCheck = nullptr;
    QSlider* m_modifiedCameraColorStrengthSlider = nullptr;
    QLabel* m_modifiedCameraColorStrengthLabel = nullptr;
    QComboBox* m_modifiedCameraColorMode = nullptr;
    QPushButton* m_modifiedCameraGrayPointButton = nullptr;
    QLabel* m_modifiedCameraGrayPointStatus = nullptr;
    double m_modifiedCameraGrayPointX = -1.0;
    double m_modifiedCameraGrayPointY = -1.0;
    QCheckBox* m_stretchCheck = nullptr;

    // ACR-style relative adjustments. Exposure is stored in tenths of an EV;
    // the remaining signed sliders use the common -100..100 scale.
    QGroupBox* m_basicAdjustGroup = nullptr;
    QSlider* m_temperatureSlider = nullptr;
    QLabel* m_temperatureLabel = nullptr;
    QSlider* m_tintSlider = nullptr;
    QLabel* m_tintLabel = nullptr;
    QSlider* m_exposureSlider = nullptr;
    QLabel* m_exposureLabel = nullptr;
    QSlider* m_contrastSlider = nullptr;
    QLabel* m_contrastLabel = nullptr;
    QSlider* m_highlightsSlider = nullptr;
    QLabel* m_highlightsLabel = nullptr;
    QSlider* m_shadowsSlider = nullptr;
    QLabel* m_shadowsLabel = nullptr;
    QSlider* m_whitesSlider = nullptr;
    QLabel* m_whitesLabel = nullptr;
    QSlider* m_blacksSlider = nullptr;
    QLabel* m_blacksLabel = nullptr;
    QSlider* m_vibranceSlider = nullptr;
    QLabel* m_vibranceLabel = nullptr;
    QSlider* m_saturationSlider = nullptr;
    QLabel* m_saturationLabel = nullptr;
    QSlider* m_sharpeningSlider = nullptr;
    QLabel* m_sharpeningLabel = nullptr;

    // 星点修饰组：去色边可以独立使用，不要求同时缩星。
    QWidget* m_starReduceGroup = nullptr;
    QCheckBox* m_starDefringeCheck = nullptr;
    QSlider* m_starDefringeSlider = nullptr;
    QLabel* m_starDefringeLabel = nullptr;
    QCheckBox* m_starReduceCheck = nullptr;
    QSlider* m_starReduceSlider = nullptr;
    QLabel* m_starReduceLabel = nullptr;

    // 输出组
    QWidget* m_outputGroup = nullptr;
    QWidget* m_processingReportGroup = nullptr;
    QLabel* m_processingReportLabel = nullptr;
    QWidget* m_recentResultsGroup = nullptr;
    QListWidget* m_recentResultsList = nullptr;
    QPushButton* m_loadRecentResultBtn = nullptr;
    QPushButton* m_revealRecentResultBtn = nullptr;
    QComboBox* m_outputFormat = nullptr;
    QLineEdit* m_outputPath = nullptr;

    // 底部按钮
    QPushButton* m_restoreBtn = nullptr;
    QPushButton* m_savePresetBtn = nullptr;

    // 天地分离组
    QCheckBox* m_skyGroundCheck = nullptr;
    QComboBox* m_skyGroundMode = nullptr;
    QLabel* m_skyGroundModeLabel = nullptr;
    QPushButton* m_detectMaskBtn = nullptr;
    QPushButton* m_importMaskBtn = nullptr;
    QLabel* m_maskPathLabel = nullptr;
    QSlider* m_featherSlider = nullptr;
    QLabel* m_featherLabel = nullptr;
    QLabel* m_featherNameLabel = nullptr;
    QComboBox* m_groundStackMethod = nullptr;
    QLabel* m_groundStackNameLabel = nullptr;
    QSlider* m_groundDetailSlider = nullptr;
    QLabel* m_groundDetailLabel = nullptr;
    QLabel* m_groundDetailNameLabel = nullptr;
    QString m_userMaskPath;

    // Debounce 定时器
    QTimer* m_debounceTimer = nullptr;
    
    // 用户是否手动修改过堆栈算法（避免智能推荐覆盖用户选择）
    bool m_userChangedStackMethod = false;
};
