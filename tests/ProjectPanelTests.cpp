#include "ui/ProjectPanel.h"

#include <QApplication>
#include <QString>

#include <cstdlib>
#include <iostream>

namespace {

void check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    ProjectPanel panel;
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

    const QStringList lockedPaths = panel.filePaths();
    panel.setEditingEnabled(false);
    panel.removeSelected();
    check(panel.filePaths() == lockedPaths,
          "Processing lock should preserve browsing while blocking edits");

    std::cout << "Project panel tests passed\n";
    return 0;
}
