#include "MainWindow.h"

#include "core/PrintOrderDocument.h"
#include "ui/FileDialogUtils.h"
#include "ui/ParamsPanel.h"
#include "ui/PreviewPanel.h"
#include "ui/PrintOrderPanel.h"
#include "ui/ProjectPanel.h"

#include <QAction>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QStandardPaths>
#include <QStatusBar>
#include <algorithm>
#include <array>

namespace {
constexpr std::array<std::pair<ProcessingScene, const char*>, 6> sceneKeys{{
    {ProcessingScene::SingleFrame, "single"}, {ProcessingScene::Nightscape, "nightscape"},
    {ProcessingScene::DeepSky, "deep-sky"}, {ProcessingScene::SkyGround, "sky-ground"},
    {ProcessingScene::StarTrail, "star-trail"}, {ProcessingScene::Timelapse, "timelapse"}
}};

QString orderFilter() {
    return QCoreApplication::translate("Nebulens", "Nebulens 冲印单 (*.nebprint)");
}

void documentError(QWidget* parent, const QString& title, const QString& message,
                   const QString& detail) {
    QMessageBox box(QMessageBox::Warning, title, message, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.setDetailedText(detail);
    box.exec();
}
}

void MainWindow::addDocumentActions(QMenu* menu) {
    m_openOrderAction = menu->addAction(QCoreApplication::translate("Nebulens", "打开冲印单…"));
    m_openOrderAction->setObjectName(QStringLiteral("actionOpenPrintOrder"));
    m_openOrderAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+O")));
    connect(m_openOrderAction, &QAction::triggered, this, [this] { requestOpenPrintOrder(); });
    m_saveOrderAction = menu->addAction(QCoreApplication::translate("Nebulens", "保存冲印单"));
    m_saveOrderAction->setObjectName(QStringLiteral("actionSavePrintOrder"));
    m_saveOrderAction->setShortcut(QKeySequence::Save);
    connect(m_saveOrderAction, &QAction::triggered, this, [this] { requestSavePrintOrder(); });
    m_saveOrderAsAction = menu->addAction(QCoreApplication::translate("Nebulens", "冲印单另存为…"));
    m_saveOrderAsAction->setShortcut(QKeySequence::SaveAs);
    connect(m_saveOrderAsAction, &QAction::triggered, this, [this] { requestSavePrintOrder(true); });
}

void MainWindow::capturePrintOrder(PrintOrderDocument& doc) const {
    for (const auto& [scene, key] : sceneKeys)
        if (m_scene == scene) doc.scene = QString::fromLatin1(key);
    doc.automaticStill = m_automaticStill;
    doc.lights = m_projectPanel->allFilePaths();
    doc.excluded = m_projectPanel->excludedFilePaths();
    doc.selected = m_projectPanel->currentFilePath();
    doc.reference = m_projectPanel->referenceFramePath();
    m_paramsPanel->captureOrderSettings(doc);
    doc.comparisonManifest = m_comparisonManifest;
}

QString MainWindow::orderEditSignature() const {
    // 只比较编辑意图。NAS 是否在线、缩略图何时就绪都不是用户修改。
    PrintOrderDocument doc;
    capturePrintOrder(doc);
    QByteArray bytes;
    QDataStream fields(&bytes, QIODevice::WriteOnly);
    fields << doc.scene << doc.automaticStill << doc.lights << doc.excluded << doc.selected
           << doc.reference << doc.dark << doc.flat << doc.bias << doc.darkFlat << doc.masterDark
           << doc.masterFlat << doc.masterBias << doc.masterDarkFlat << doc.userMask
           << doc.outputDirectory << QJsonDocument(doc.params).toJson(QJsonDocument::Compact)
           << quint64(m_editedMaskRevision);
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool MainWindow::savePrintOrderTo(const QString& path, QString& error) {
    error.clear();
    if (processingActive() || !path.endsWith(QStringLiteral(".nebprint"), Qt::CaseInsensitive)) {
        error = QCoreApplication::translate("Nebulens", "请在任务结束后保存为 .nebprint 文件。");
        return false;
    }
    PrintOrderDocument doc;
    capturePrintOrder(doc);
    if (!m_editedSkyGroundMask.empty()) {
        doc.editedMask = QImage(m_editedSkyGroundMask.data(), m_editedSkyGroundMaskWidth,
            m_editedSkyGroundMaskHeight, m_editedSkyGroundMaskWidth, QImage::Format_Grayscale8).copy();
        doc.editedMaskSources = m_editedMaskProjectFiles;
        doc.editedMaskReference = m_editedMaskSourceFile;
    }
    if (!doc.save(path, error)) return false;
    m_orderFilePath = QFileInfo(path).absoluteFilePath();
    m_orderFileBaseline = orderEditSignature();
    setWindowFilePath(m_orderFilePath);
    return true;
}

bool MainWindow::openPrintOrderFrom(const QString& path, QString& error,
                                    const LocateOrderDirectory& locate) {
    error.clear();
    if (processingActive()) {
        error = QCoreApplication::translate("Nebulens", "任务进行中，无法替换冲印单。");
        return false;
    }
    PrintOrderDocument doc;
    if (!PrintOrderDocument::load(path, doc, error) || !ParamsPanel::validateOrderSettings(doc, error))
        return false;
    const auto target = std::find_if(sceneKeys.begin(), sceneKeys.end(),
        [&doc](const auto& entry) { return doc.scene == QLatin1String(entry.second); });
    if (target == sceneKeys.end()) {
        error = QCoreApplication::translate("Nebulens", "无法识别这份冲印单的场景。");
        return false;
    }
    QStringList missing = doc.missingInputPaths();
    const bool relocated = !missing.isEmpty();
    while (!missing.isEmpty()) {
        const QString first = missing.first();
        if (!locate) {
            error = QCoreApplication::translate("Nebulens", "找不到素材：%1").arg(first);
            return false;
        }
        const QString dir = locate(first);
        if (dir.isEmpty()) return false;
        if (!QFileInfo(QDir(dir).filePath(QFileInfo(first).fileName())).isFile() ||
            !doc.relocateDirectory(QFileInfo(first).absolutePath(), dir)) {
            error = QCoreApplication::translate("Nebulens", "所选目录不能重新关联素材：%1").arg(first);
            return false;
        }
        const auto remaining = doc.missingInputPaths();
        if (remaining.size() >= missing.size()) {
            error = QCoreApplication::translate("Nebulens", "找不到素材：%1").arg(first);
            return false;
        }
        missing = remaining;
    }
    if (!ParamsPanel::validateOrderSettings(doc, error)) return false;

    // 在临时文档完成解析、校验与重定位后，才释放旧会话的像素和回执。
    resetProject();
    {
        QScopedValueRollback<bool> restoring(m_resettingProject, true);
        m_automaticStill = doc.automaticStill;
        activateScene(target->first);
        m_projectPanel->restoreOrderFiles(doc.lights, doc.excluded, doc.selected, doc.reference);
        m_paramsPanel->updateRefFrameList(m_projectPanel->includedFilePaths());
        m_paramsPanel->setSelectedReferenceFrame(doc.reference);
        m_paramsPanel->restoreOrderSettings(doc);
        if (!doc.editedMask.isNull()) {
            const QImage mask = doc.editedMask.convertToFormat(QImage::Format_Grayscale8);
            m_editedSkyGroundMaskWidth = mask.width();
            m_editedSkyGroundMaskHeight = mask.height();
            m_editedSkyGroundMask.resize(size_t(mask.width()) * mask.height());
            for (int y = 0; y < mask.height(); ++y)
                std::copy_n(mask.constScanLine(y), mask.width(),
                            m_editedSkyGroundMask.data() + size_t(y) * mask.width());
            m_editedMaskSourceFile = doc.editedMaskReference;
            m_editedMaskProjectFiles = doc.editedMaskSources;
            ++m_editedMaskRevision;
        }
        m_comparisonManifest = doc.comparisonManifest;
    }
    m_printOrder->setOutput(m_paramsPanel->outputFormat(), m_paramsPanel->outputPath());
    m_printOrder->setRidgeComparisonAvailable(!m_comparisonManifest.isEmpty());
    showPrintOrder();
    m_printOrder->showReopenedOrder();
    // 批量恢复片列屏蔽了逐张选中信号；只为最终选片发起一次预览。
    const QString selected = m_projectPanel->currentFilePath();
    if (!selected.isEmpty()) m_previewPanel->loadImage(selected);
    m_orderFilePath = QFileInfo(path).absoluteFilePath();
    setWindowFilePath(m_orderFilePath);
    // 文件在磁盘上仍指向旧位置；退出前应能保存本次重定位。
    m_orderFileBaseline = relocated ? QString() : orderEditSignature();
    return true;
}

bool MainWindow::requestSavePrintOrder(bool saveAs) {
    if (processingActive()) return false;
    QString path = m_orderFilePath;
    if (saveAs || path.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        path = FileDialogUtils::getSaveFileName(this, QCoreApplication::translate("Nebulens", "保存冲印单"),
            path.isEmpty() ? QDir(dir).filePath(QStringLiteral("Nebulens.nebprint")) : path,
            orderFilter(), QStringLiteral("nebprint"));
        if (path.isEmpty()) return false;
    }
    QString error;
    if (savePrintOrderTo(path, error)) return true;
    documentError(this, QCoreApplication::translate("Nebulens", "保存冲印单"),
        QCoreApplication::translate("Nebulens", "保存失败，原文件未被替换。"), error);
    return false;
}

bool MainWindow::confirmOrderReplacement() {
    if (m_orderFilePath.isEmpty() ? m_projectPanel->totalFileCount() == 0
                                  : m_orderFileBaseline == orderEditSignature()) return true;
    const auto choice = QMessageBox::question(this, QCoreApplication::translate("Nebulens", "冲印单"),
        QCoreApplication::translate("Nebulens", "要保存当前冲印单的修改吗？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return choice == QMessageBox::Discard || (choice == QMessageBox::Save && requestSavePrintOrder());
}

void MainWindow::requestOpenPrintOrder(const QString& preferred) {
    if (processingActive()) return;
    const QString title = QCoreApplication::translate("Nebulens", "打开冲印单…");
    const QString path = preferred.isEmpty()
        ? FileDialogUtils::getOpenFileName(this, title, m_orderFilePath, orderFilter()) : preferred;
    if (path.isEmpty() || !confirmOrderReplacement()) return;
    QString error;
    const bool opened = openPrintOrderFrom(path, error, [this, &path](const QString& missing) {
        return FileDialogUtils::getExistingDirectory(this,
            QCoreApplication::translate("Nebulens", "重新定位包含 %1 的文件夹").arg(QFileInfo(missing).fileName()),
            QFileInfo(path).absolutePath());
    });
    if (!opened && !error.isEmpty())
        documentError(this, title, QCoreApplication::translate("Nebulens", "未能打开冲印单，当前工作已保留。"), error);
}
