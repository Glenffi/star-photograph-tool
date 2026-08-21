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
    panel.toggleSelectedExclusion();
    check(panel.includedFilePaths() == QStringList{path},
          "Reference frame must not be excluded by the Space action");

    bool undoAvailable = false;
    QObject::connect(&panel, &ProjectPanel::undoAvailabilityChanged,
                     [&](bool available, const QString&) {
                         undoAvailable = available;
                     });
    panel.removeSelected();
    check(panel.filePaths().isEmpty() && undoAvailable,
          "Removing a source should expose the five-second undo state");
    check(panel.undoLastRemoval() && panel.filePaths() == QStringList{path} &&
              !undoAvailable,
          "Undo should restore the removed source and clear the prompt");

    panel.setEditingEnabled(false);
    panel.removeSelected();
    check(panel.filePaths() == QStringList{path},
          "Processing lock should preserve browsing while blocking edits");

    std::cout << "Project panel tests passed\n";
    return 0;
}
