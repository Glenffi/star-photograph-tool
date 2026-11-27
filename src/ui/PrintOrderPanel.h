#pragma once

#include "ProcessingScene.h"
#include "WorkflowRail.h"

#include <QImage>
#include <QString>
#include <QWidget>

#include <memory>

class PrintOrderPanel : public QWidget {
    Q_OBJECT
public:
    explicit PrintOrderPanel(QWidget* parent = nullptr);
    ~PrintOrderPanel() override;

    void setScene(ProcessingScene scene);
    void setProject(int included, int total, const QString& sourceName,
                    bool calibrationReady, const QString& calibrationHint);
    void setOutput(const QString& format, const QString& directory);
    void setProof(const QImage& image, const QString& caption);
    QImage proofImage() const;
    void setProofInspectionActive(bool active);
    void setTask(bool busy, int progress, const QString& status);
    // 本次产物的文件或序列目录；frameCount 是实际交付张数。
    void setReceipt(const QString& path, int frameCount);
    void clearReceipt();
    void setRidgeComparisonAvailable(bool available);
    // 在同步重开单据的素材后调用，不保留上一单的回执。
    void showReopenedOrder();
    void resetOrder();

signals:
    void openOrderRequested();
    void saveOrderRequested();
    void ridgeComparisonRequested();
    void productRequested(ProcessingScene scene);
    void importFilesRequested();
    void importFolderRequested();
    void advancedRequested(WorkflowStage stage);
    void proofRequested();
    void formatRequested(const QString& format);
    void directoryRequested();
    // 任务运行时同一个信号表示取消，由主窗口解释。
    void processRequested();
    // 原样传回回执路径，由主窗口打开序列目录或成片所在目录。
    void revealRequested(const QString& path);
    void newOrderRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    struct Ui;
    std::unique_ptr<Ui> m_ui;
};
