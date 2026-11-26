#include "ui/ProjectPanel.h"
#include "ui/StyleTokens.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QLabel>
#include <QScrollArea>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void checkSourceSignature() {
    QTemporaryDir directory;
    check(directory.isValid(), "Source signature fixture needs a writable directory");
    const QString path = directory.filePath(QStringLiteral("light.dng"));
    const auto modify = [&path](const QByteArray& content, const QDateTime& time) {
        QFile file(path);
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                  file.write(content) == content.size() && file.flush() &&
                  file.setFileTime(time, QFileDevice::FileModificationTime),
              "Source signature fixture must support setting size and mtime");
    };
    const QDateTime firstTime = QDateTime::fromMSecsSinceEpoch(1500000000000LL);
    const QDateTime secondTime = firstTime.addSecs(10);
    modify("a", firstTime);
    ProjectPanel panel;
    panel.addFiles({path});
    const QString first = panel.processingSourceSignature();
    modify("b", secondTime);
    const QString modified = panel.processingSourceSignature();
    check(modified != first, "Same-path replacement with a new mtime must invalidate the signature");
    modify("bb", secondTime);
    check(panel.processingSourceSignature() != modified,
          "Same-path replacement with a new size must invalidate the signature");
    panel.toggleSelectedExclusion();
    const QString excluded = panel.processingSourceSignature();
    modify("changed again", secondTime.addSecs(10));
    check(panel.processingSourceSignature() == excluded,
          "Changes to excluded paths must not invalidate the signature");
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ProjectPanel panel;
    panel.resize(1000, StyleTokens::Layout::kFilmstripHeight);
    check(panel.height() == StyleTokens::Layout::kFilmstripHeight,
          "ProjectPanel should render as a fixed-height filmstrip");
    auto* filmstrip = panel.findChild<QScrollArea*>(
        QStringLiteral("filmstripScroll"));
    check(filmstrip &&
              filmstrip->horizontalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
              filmstrip->verticalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
          "Filmstrip should scroll horizontally without a vertical bar");
    panel.setScene(ProcessingScene::SingleFrame);
    check(panel.currentScene() == ProcessingScene::SingleFrame,
          "Scene selector should round-trip through ProjectPanel");

    const QString path = QStringLiteral("/tmp/starprocessor-undo-test.ARW");
    panel.addFiles({path});
    check(panel.filePaths() == QStringList{path},
          "ProjectPanel should add one source");

    panel.setReferenceFrame(path);
    const QString originalSignature = panel.processingSourceSignature();
    panel.toggleSelectedExclusion();
    check(panel.includedFilePaths() == QStringList{path},
          "Reference frame must not be excluded by the Space action");

    bool undoAvailable = false;
    int referenceChanges = 0;
    QObject::connect(&panel, &ProjectPanel::undoAvailabilityChanged,
                     [&](bool available, const QString&) {
                         undoAvailable = available;
                     });
    QObject::connect(&panel, &ProjectPanel::referenceFrameChanged,
                     [&]() { ++referenceChanges; });
    panel.removeSelected();
    check(panel.filePaths().isEmpty() && undoAvailable &&
              referenceChanges == 1,
          "Removing a source should expose the five-second undo state");
    check(panel.undoLastRemoval() && panel.filePaths() == QStringList{path} &&
              !undoAvailable && referenceChanges == 2,
          "Undo should restore the removed source and clear the prompt");
    check(panel.processingSourceSignature() == originalSignature,
          "Undo should restore the processing-source signature");

    const QString secondPath = QStringLiteral("/tmp/starprocessor-second.ARW");
    panel.addFiles({secondPath});
    check(panel.processingSourceSignature() != originalSignature,
          "Adding a source must invalidate the processing-source signature");

    panel.setReferenceFrame(path);
    panel.removeSelected();
    panel.setReferenceFrame(secondPath);
    check(panel.undoLastRemoval() && panel.referenceFramePath() == path,
          "Undoing a removed reference should replace the interim reference");
    panel.removeSelected();
    check(panel.referenceFramePath().isEmpty(),
          "Restoring a reference must not leave a second hidden reference");
    check(panel.undoLastRemoval() && panel.referenceFramePath() == path,
          "The reference should remain unique across repeated undo");

    QStringList changeOrder;
    QObject::connect(&panel, &ProjectPanel::filesChanged, &panel,
                     [&] { changeOrder.append(QStringLiteral("files")); });
    QObject::connect(&panel, &ProjectPanel::referenceFrameChanged, &panel,
                     [&] { changeOrder.append(QStringLiteral("reference")); });
    panel.setReferenceFrame(QString());
    check(panel.referenceFramePath().isEmpty() &&
              changeOrder == QStringList{QStringLiteral("reference")},
          "Automatic reference must unpin the frame without changing participation");
    panel.setReferenceFrame(QString());
    check(changeOrder.size() == 1, "Automatic reference should be idempotent");

    panel.toggleSelectedExclusion();
    check(!panel.includedFilePaths().contains(path),
          "An unpinned frame should be excludable again");
    changeOrder.clear();
    panel.setReferenceFrame(path);
    check(panel.referenceFramePath() == path && panel.includedFilePaths().contains(path) &&
              changeOrder == QStringList{QStringLiteral("files"), QStringLiteral("reference")},
          "Pinning an excluded reference should publish inclusion before the reference");
    panel.setReferenceFrame(QStringLiteral("/tmp/unknown-reference.dng"));
    check(panel.referenceFramePath() == path && changeOrder.size() == 2,
          "A reference path outside the project must not unpin the current one");

    const QStringList lockedPaths = panel.filePaths();
    panel.setEditingEnabled(false);
    panel.removeSelected();
    check(panel.filePaths() == lockedPaths,
          "Processing lock should preserve browsing while blocking edits");
    panel.setEditingEnabled(true);
    changeOrder.clear();
    panel.clearFiles();
    check(panel.referenceFramePath().isEmpty() && panel.totalFileCount() == 0 &&
              changeOrder == QStringList{QStringLiteral("files"), QStringLiteral("reference")},
          "Clearing the project should publish the automatic reference once");

    app.setStyleSheet(StyleTokens::appStyleSheet());
    FileItem item;
    item.fileName = QStringLiteral("example-with-a-very-long-file-name.dng");
    item.isExcluded = true;
    FileCard card(item);
    card.show();
    QApplication::processEvents();
    auto* name = card.findChild<QLabel*>(QStringLiteral("filmstripFileName"));
    auto* status = card.findChild<QLabel*>(QStringLiteral("filmstripStatus"));
    check(!card.graphicsEffect() && name && status && status->isVisible() &&
              name->font().pixelSize() >= StyleTokens::Typography::kCaptionSize &&
              status->font().pixelSize() >= StyleTokens::Typography::kCaptionSize &&
              status->sizeHint().width() <= status->width() &&
              card.toolTip().contains(item.fileName) && name->text() != item.fileName,
          "An excluded card should keep legible labels and disclose the full filename");

    checkSourceSignature();

    std::cout << "Project panel tests passed\n";
    return 0;
}
