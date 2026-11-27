#include "core/PrintOrderDocument.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtEndian>

#include <cstring>
#include <iostream>

namespace {

constexpr char kGeneratorOrder[] =
    R"json({"format":"NebulensPrintOrder","version":1,"scene":"sky-ground","automaticStill":false,"lights":["relative.dng"],"reference":"relative.dng","selected":"relative.dng","params":{},"outputDirectory":"results","comparisonManifest":"comparison.json"})json";

int failures = 0;
int checks = 0;

bool check(bool condition, const char* message, const QString& detail = {}) {
    ++checks;
    if (condition) return true;
    ++failures;
    std::cerr << "[FAIL] " << message;
    if (!detail.isEmpty()) std::cerr << ": " << detail.toStdString();
    std::cerr << '\n';
    return false;
}

bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                 "Write test fixture", file.errorString());
}

QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!check(file.open(QIODevice::ReadOnly), "Read test fixture", file.errorString())) return {};
    return file.readAll();
}

QByteArray jsonBytes(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonObject generatorObject() {
    return QJsonDocument::fromJson(kGeneratorOrder).object();
}

bool sameMask(const QImage& a, const QImage& b) {
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    if (a.size() != b.size() || a.format() != QImage::Format_Grayscale8 ||
        b.format() != QImage::Format_Grayscale8) return false;
    for (int y = 0; y < a.height(); ++y) {
        if (std::memcmp(a.constScanLine(y), b.constScanLine(y), a.width()) != 0) return false;
    }
    return true;
}

bool sameOrder(const PrintOrderDocument& a, const PrintOrderDocument& b) {
    return a.scene == b.scene && a.automaticStill == b.automaticStill &&
           a.lights == b.lights && a.excluded == b.excluded && a.dark == b.dark &&
           a.flat == b.flat && a.bias == b.bias && a.darkFlat == b.darkFlat &&
           a.editedMaskSources == b.editedMaskSources && a.reference == b.reference &&
           a.selected == b.selected && a.masterDark == b.masterDark &&
           a.masterFlat == b.masterFlat && a.masterBias == b.masterBias &&
           a.masterDarkFlat == b.masterDarkFlat && a.userMask == b.userMask &&
           a.outputDirectory == b.outputDirectory && a.editedMaskReference == b.editedMaskReference &&
           a.comparisonManifest == b.comparisonManifest && a.params == b.params &&
           sameMask(a.editedMask, b.editedMask);
}

PrintOrderDocument populatedOrder(const QDir& root, const QString& sourceDir) {
    const QDir source(root.filePath(sourceDir));
    PrintOrderDocument document;
    document.scene = QStringLiteral("sky-ground");
    document.automaticStill = false;
    document.lights = {source.filePath(QStringLiteral("z/\u661F\u7A7A.dng")),
                       root.filePath(QStringLiteral("raw-backup/z/\u661F\u7A7A.dng")),
                       source.filePath(QStringLiteral("a/skip.dng"))};
    document.excluded = {document.lights.last()};
    document.reference = document.lights.first();
    document.selected = document.lights.last();
    document.dark = {source.filePath(QStringLiteral("cal/dark2.dng")),
                     source.filePath(QStringLiteral("cal/dark1.dng"))};
    document.flat = {source.filePath(QStringLiteral("cal/flat.dng"))};
    document.bias = {source.filePath(QStringLiteral("cal/bias.dng"))};
    document.darkFlat = {source.filePath(QStringLiteral("cal/dark-flat.dng"))};
    document.masterDark = source.filePath(QStringLiteral("cal/master-dark.master"));
    document.masterFlat = source.filePath(QStringLiteral("cal/master-flat.master"));
    document.masterBias = source.filePath(QStringLiteral("cal/master-bias.master"));
    document.masterDarkFlat = source.filePath(QStringLiteral("cal/master-dark-flat.master"));
    document.userMask = source.filePath(QStringLiteral("masks/input.png"));
    document.editedMaskReference = document.reference;
    document.editedMaskSources = {document.lights[0], document.lights[1]};
    document.editedMask = QImage(17, 9, QImage::Format_Grayscale8);
    document.editedMask.fill(121);
    document.outputDirectory = source.filePath(QStringLiteral("results"));
    document.comparisonManifest = source.filePath(QStringLiteral("comparison.json"));
    document.params = {{QStringLiteral("exposure"), -0.75},
                       {QStringLiteral("options"),
                        QJsonObject{{QStringLiteral("ordered"),
                                     QJsonArray{true, 4, QStringLiteral("quote \" \\ [,:}")}}}}};
    return document;
}

void testGeneratorAndDefaults() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create order fixture directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("generator.order"));
    if (!writeBytes(path, kGeneratorOrder)) return;

    QString error = QStringLiteral("stale error");
    PrintOrderDocument document = populatedOrder(root, QStringLiteral("raw"));
    if (!check(PrintOrderDocument::load(path, document, error), "Load exact generator JSON", error)) return;
    PrintOrderDocument expected;
    expected.scene = QStringLiteral("sky-ground");
    expected.automaticStill = false;
    expected.lights = {root.filePath(QStringLiteral("relative.dng"))};
    expected.reference = expected.selected = expected.lights.first();
    expected.outputDirectory = root.filePath(QStringLiteral("results"));
    expected.comparisonManifest = root.filePath(QStringLiteral("comparison.json"));
    check(error.isEmpty() && sameOrder(document, expected),
          "Resolve all generator paths and default all omitted fields");
    check(document.missingInputPaths() == expected.lights,
          "Missing RAW can load; missing output and comparison references are not inputs");

    const QStringList scenes{QStringLiteral("single"), QStringLiteral("nightscape"),
                             QStringLiteral("sky-ground"), QStringLiteral("deep-sky"),
                             QStringLiteral("star-trail"), QStringLiteral("timelapse")};
    for (const QString& scene : scenes) {
        const QJsonObject minimal{{QStringLiteral("format"), QStringLiteral("NebulensPrintOrder")},
                                  {QStringLiteral("version"), 1}, {QStringLiteral("scene"), scene}};
        if (!writeBytes(path, jsonBytes(minimal))) return;
        PrintOrderDocument defaults;
        defaults.scene = scene;
        check(PrintOrderDocument::load(path, document, error) && sameOrder(document, defaults),
              "Accept all v1 scenes with empty paths and default automaticStill", error);
    }
}

void testRoundTrip() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create round-trip directory")) return;
    const QDir root(temporary.path());
    if (!check(root.mkdir(QStringLiteral("documents")), "Create nested order directory")) return;
    const QString path = root.filePath(QStringLiteral("documents/active.order"));
    PrintOrderDocument document = populatedOrder(root, QStringLiteral("raw"));
    document.outputDirectory = root.filePath(QStringLiteral("documents"));
    document.editedMask = QImage(2400, 1600, QImage::Format_Grayscale8);
    if (!check(!document.editedMask.isNull(), "Allocate whole-preview grayscale mask")) return;
    quint32 random = 17;
    for (int y = 0; y < document.editedMask.height(); ++y) {
        auto* row = document.editedMask.scanLine(y);
        for (int x = 0; x < document.editedMask.width(); ++x) {
            random = random * 1664525u + 1013904223u;
            row[x] = static_cast<uchar>(random >> 24);
        }
    }
    document.editedMask.setText(QStringLiteral("Cache"), QStringLiteral("not a mask pixel"));

    QString error;
    if (!check(document.save(path, error), "Save whole-preview order", error)) return;
    const auto stored = QJsonDocument::fromJson(readBytes(path)).object();
    const QJsonArray storedLights{QStringLiteral("../raw/z/\u661F\u7A7A.dng"),
                                  QStringLiteral("../raw-backup/z/\u661F\u7A7A.dng"),
                                  QStringLiteral("../raw/a/skip.dng")};
    check(stored.value(QStringLiteral("format")).toString() == QStringLiteral("NebulensPrintOrder") &&
          stored.value(QStringLiteral("version")).toInt() == 1 &&
          stored.value(QStringLiteral("lights")).toArray() == storedLights &&
          stored.value(QStringLiteral("comparisonManifest")).toString() ==
              QStringLiteral("../raw/comparison.json") &&
          stored.value(QStringLiteral("outputDirectory")).toString() == QStringLiteral("."),
          "Write version 1 and prefer relative paths without sorting sources");
    PrintOrderDocument loaded;
    check(PrintOrderDocument::load(path, loaded, error) && sameOrder(loaded, document) &&
          loaded.editedMask.textKeys().isEmpty(),
          "Round-trip every field and pixel without image metadata", error);
    check(root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) ==
              QStringList{QStringLiteral("documents")} &&
          QDir(root.filePath(QStringLiteral("documents"))).entryList(
              QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) ==
              QStringList{QStringLiteral("active.order")},
          "Save only the order, not RAW, sidecars, cache, or receipts");

    const QString saveAs = root.filePath(QStringLiteral("renamed.order"));
    if (!check(loaded.save(saveAs, error), "Save absolute references in another directory", error)) return;
    const auto rebased = QJsonDocument::fromJson(readBytes(saveAs)).object();
    check(rebased.value(QStringLiteral("reference")).toString() == QStringLiteral("raw/z/\u661F\u7A7A.dng") &&
          rebased.value(QStringLiteral("outputDirectory")).toString() == QStringLiteral("documents") &&
          PrintOrderDocument::load(saveAs, loaded, error) && sameOrder(loaded, document),
          "Save As rebases relative paths without changing their absolute targets or pixels", error);
}

void testMissingAndRelocation() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create relocation directories")) return;
    const QDir root(temporary.path());
    PrintOrderDocument document = populatedOrder(root, QStringLiteral("raw"));
    if (!check(root.mkpath(QStringLiteral("raw/z")) &&
               root.mkpath(QStringLiteral("raw/cal/dark2.dng")), "Create one input and a directory") ||
        !writeBytes(document.lights.first(), "placeholder file")) return;
    const QStringList expectedMissing{
        document.lights[1], document.lights[2], document.dark[0], document.dark[1],
        document.flat[0], document.bias[0], document.darkFlat[0], document.masterDark,
        document.masterFlat, document.masterBias, document.masterDarkFlat, document.userMask};
    check(document.missingInputPaths() == expectedMissing,
          "Report missing inputs once in order, including calibration, masks, excluded and wrong file type");

    PrintOrderDocument expected = populatedOrder(root, QStringLiteral("moved"));
    expected.outputDirectory = document.outputDirectory;
    expected.comparisonManifest = document.comparisonManifest;
    check(document.relocateDirectory(root.filePath(QStringLiteral("raw/./")),
                                     root.filePath(QStringLiteral("moved"))) &&
          sameOrder(document, expected),
          "Relocate a complete directory prefix and suffixes, not same-name or sibling inputs");
    check(!document.relocateDirectory(root.filePath(QStringLiteral("raw")), root.path()) &&
          !document.relocateDirectory({}, root.path()) &&
          !document.relocateDirectory(root.filePath(QStringLiteral("moved")),
                                      root.filePath(QStringLiteral("moved"))) &&
          sameOrder(document, expected),
          "No match or invalid directory leaves the whole order unchanged");
}

void testRelocationCollisions() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create relocation collision fixture")) return;
    const QDir root(temporary.path());
    const QString from = root.filePath(QStringLiteral("old"));
    const QString to = root.filePath(QStringLiteral("new"));
    PrintOrderDocument document;
    document.scene = QStringLiteral("nightscape");
    document.lights = {root.filePath(QStringLiteral("old/a.dng")),
                       root.filePath(QStringLiteral("old/nested/shared.dng")),
                       root.filePath(QStringLiteral("new/nested/shared.dng"))};
    document.reference = document.lights[0];
    document.selected = document.lights[1];
    document.excluded = {document.lights[2]};
    auto before = document;
    check(!document.relocateDirectory(from, to) && sameOrder(before, document),
          "Reject colliding input paths without a partial move or merging a binding");

    document.dark = document.excluded;
    document.excluded.clear();
    document.lights.removeLast();
    before = document;
    check(!document.relocateDirectory(from, to) && sameOrder(before, document),
          "Check relocation collisions across input roles");
}

void expectLoadFailure(const QString& path, const PrintOrderDocument& before, const char* message,
                       const QString& expectedError = {}) {
    PrintOrderDocument document = before;
    QString error;
    const bool loaded = PrintOrderDocument::load(path, document, error);
    check(!loaded && !error.isEmpty() && sameOrder(before, document) &&
          (expectedError.isEmpty() || error.contains(expectedError)), message, error);
}

void testPlatformPaths() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create path portability directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("paths.order"));
    auto object = generatorObject();
    const QString localPath = QStringLiteral("native/album/frame.dng");
    object.insert(QStringLiteral("lights"), QJsonArray{QDir::toNativeSeparators(localPath)});
    object.insert(QStringLiteral("reference"), QDir::toNativeSeparators(localPath));
    object.insert(QStringLiteral("selected"), QDir::toNativeSeparators(localPath));
    if (!writeBytes(path, jsonBytes(object))) return;
    PrintOrderDocument document;
    QString error;
    if (!check(PrintOrderDocument::load(QDir::toNativeSeparators(path), document, error),
               "Accept the platform's native path separators", error)) return;
    check(document.reference == root.filePath(localPath) &&
          document.relocateDirectory(QDir::toNativeSeparators(root.filePath(QStringLiteral("native"))),
                                     QDir::toNativeSeparators(root.filePath(QStringLiteral("moved")))) &&
          document.reference == root.filePath(QStringLiteral("moved/album/frame.dng")) &&
          document.selected == document.reference,
          "Resolve and relocate native paths without changing nested suffixes");
    if (!check(document.save(QDir::toNativeSeparators(path), error),
               "Save to a native order path", error)) return;
    auto stored = QJsonDocument::fromJson(readBytes(path)).object();
    check(stored.value(QStringLiteral("reference")).toString() == QStringLiteral("moved/album/frame.dng"),
          "Store portable forward-slash relative paths");

    stored.insert(QStringLiteral("reference"), QStringLiteral("MOVED/album/FRAME.dng"));
    stored.insert(QStringLiteral("selected"), QStringLiteral("moved/ALBUM/frame.dng"));
    if (!writeBytes(path, jsonBytes(stored))) return;
    const QString uncased = root.filePath(QStringLiteral("moved/album/frame.dng"));
#ifdef Q_OS_WIN
    check(PrintOrderDocument::load(path, document, error) && document.reference == uncased &&
          document.selected == uncased, "Windows binds to the light's original case", error);
    const auto before = document;
    check(document.relocateDirectory(root.filePath(QStringLiteral("MOVED")),
                                     root.filePath(QStringLiteral("MovedAgain"))) &&
          document.reference == root.filePath(QStringLiteral("MovedAgain/album/frame.dng")),
          "Windows directory matching ignores case but keeps the nested suffix");

    stored.insert(QStringLiteral("lights"), QJsonArray{QStringLiteral("moved/album/frame.dng"),
                                                       QStringLiteral("MOVED/album/FRAME.dng")});
    if (writeBytes(path, jsonBytes(stored))) {
        expectLoadFailure(path, before, "Reject Windows case-folded duplicate lights",
                          QStringLiteral("Duplicate light"));
    }

    document = before;
    const QChar drive = root.absolutePath().startsWith(QLatin1String("Z:"), Qt::CaseInsensitive)
        ? QLatin1Char('Y') : QLatin1Char('Z');
    document.masterDark = drive + QStringLiteral(":/missing/master-dark.master");
    if (check(document.save(path, error), "Save a reference on another Windows drive", error)) {
        const auto absolute = QJsonDocument::fromJson(readBytes(path)).object();
        PrintOrderDocument reloaded;
        check(absolute.value(QStringLiteral("masterDark")).toString() == document.masterDark &&
              PrintOrderDocument::load(path, reloaded, error) && sameOrder(document, reloaded),
              "Fall back to an absolute cross-drive reference", error);
    }

    for (const QString& partial : {QStringLiteral("C:relative.dng"), QStringLiteral("\\relative.dng")}) {
        auto ambiguous = generatorObject();
        ambiguous.insert(QStringLiteral("masterDark"), partial);
        if (writeBytes(path, jsonBytes(ambiguous))) {
            expectLoadFailure(path, before, "Reject Windows paths relative to the process drive");
        }
    }
#else
    expectLoadFailure(path, document, "Unix lexical path identity preserves case",
                      QStringLiteral("reference"));
    document.lights.append(root.filePath(QStringLiteral("MOVED/album/FRAME.dng")));
    document.userMask = root.filePath(QStringLiteral("./:/asset.png"));
    document.userMask = QDir::cleanPath(document.userMask);
    document.comparisonManifest = root.filePath(QStringLiteral("name\\with-backslash.json"));
    if (check(document.save(path, error), "Save Unix paths with literal colon and backslash", error)) {
        const auto unixPaths = QJsonDocument::fromJson(readBytes(path)).object();
        PrintOrderDocument reloaded;
        check(unixPaths.value(QStringLiteral("userMask")).toString() == QStringLiteral("./:/asset.png") &&
              unixPaths.value(QStringLiteral("comparisonManifest")).toString() ==
                  QStringLiteral("name\\with-backslash.json") &&
              PrintOrderDocument::load(path, reloaded, error) && sameOrder(document, reloaded),
              "Protect a leading colon from Qt resource syntax and preserve Unix backslashes", error);
    }
    check(document.reference == uncased &&
          !document.relocateDirectory(root.filePath(QStringLiteral("Moved")), root.path()),
          "Unix relocation matches the exact directory spelling");
#endif
}

void testInvalidDocuments() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create invalid order fixture directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("invalid.order"));
    const auto before = populatedOrder(root, QStringLiteral("raw"));
    const struct Case { const char* name; QJsonValue value; const char* message; } cases[] = {
        {"format", 7, "Require the Nebulens format string"},
        {"version", 2, "Reject unsupported versions"},
        {"version", 1.25, "Reject fractional versions"},
        {"version", QStringLiteral("1"), "Reject a string version"},
        {"scene", QStringLiteral("unknown"), "Reject an unknown scene"},
        {"scene", false, "Reject a non-string scene"},
        {"automaticStill", 0, "Reject a non-boolean automaticStill"},
        {"params", QJsonArray{}, "Reject non-object params"},
        {"darkFlat", QJsonObject{}, "Reject a non-array source list"},
        {"flat", QJsonArray{QStringLiteral("a.dng"), true}, "Reject non-string array entries"},
        {"bias", QJsonArray{QString()}, "Reject empty array paths"},
        {"masterDark", false, "Reject a non-string optional path"},
        {"userMask", QString(1, QChar(u'\0')), "Reject NUL in paths"},
        {"userMask", QStringLiteral(":/resource.png"), "Reject non-file Qt resource paths"},
        {"comparisonManifest", QJsonValue::Null, "Reject null optional fields"},
        {"lights", QJsonArray{QStringLiteral("./relative.dng"), QStringLiteral("x/../relative.dng")},
         "Reject duplicate normalized lights"},
        {"reference", QStringLiteral("elsewhere/relative.dng"), "Require exact reference membership"},
        {"selected", QStringLiteral("elsewhere/relative.dng"), "Require exact selected membership"},
        {"excluded", QJsonArray{QStringLiteral("elsewhere/relative.dng")}, "Require excluded membership"},
        {"excluded", QJsonArray{QStringLiteral("relative.dng")}, "Reject an excluded processing reference"},
        {"editedMaskSources", QJsonArray{QStringLiteral("relative.dng")}, "Reject stale mask sources"},
        {"editedMaskReference", QStringLiteral("relative.dng"), "Reject a stale mask reference"},
    };
    for (const auto& entry : cases) {
        auto object = generatorObject();
        object.insert(QLatin1String(entry.name), entry.value);
        if (writeBytes(path, jsonBytes(object))) expectLoadFailure(path, before, entry.message);
    }
    if (writeBytes(path, "{invalid")) expectLoadFailure(path, before, "Reject malformed JSON");
    if (writeBytes(path, "[{}]")) expectLoadFailure(path, before, "Require a JSON object root");
}

void testMaskBindings() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create mask binding fixture directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("mask-binding.order"));
    auto document = populatedOrder(root, QStringLiteral("raw"));
    QString error;
    if (!check(document.save(path, error), "Save an edited mask bound to the active lights", error)) return;
    const auto stored = QJsonDocument::fromJson(readBytes(path)).object();
    const auto active = stored.value(QStringLiteral("editedMaskSources")).toArray();
    const auto all = stored.value(QStringLiteral("lights")).toArray();
    const struct Case { const char* name; QJsonValue value; const char* message; } cases[] = {
        {"editedMaskSources", QJsonArray{active.at(1), active.at(0)}, "Reject reordered mask sources"},
        {"editedMaskSources", QJsonArray{active.at(0)}, "Reject a subset of active mask sources"},
        {"editedMaskSources", all, "Reject excluded frames in mask sources"},
        {"editedMaskSources", QJsonValue::Undefined, "Require the ordered mask sources for saved pixels"},
        {"editedMaskReference", QJsonValue::Undefined, "Require a mask reference for saved pixels"},
        {"editedMaskReference", QStringLiteral("elsewhere/skip.dng"), "Require mask reference membership"},
        {"editedMaskSources", QJsonArray{active.at(0), QStringLiteral("stray.dng")},
         "Require mask source membership"},
        {"reference", active.at(1), "Require the edited mask to match the explicit reference"},
    };
    for (const auto& entry : cases) {
        auto object = stored;
        object.insert(QLatin1String(entry.name), entry.value);
        if (writeBytes(path, jsonBytes(object))) expectLoadFailure(path, document, entry.message);
    }

    auto automatic = stored;
    automatic.remove(QStringLiteral("reference"));
    auto excludedReference = automatic;
    excludedReference.insert(QStringLiteral("editedMaskReference"), all.at(2));
    if (writeBytes(path, jsonBytes(excludedReference))) {
        expectLoadFailure(path, document, "An automatic reference still requires the mask's frame to be active");
    }
    document.reference.clear();
    PrintOrderDocument loaded;
    if (writeBytes(path, jsonBytes(automatic))) {
        check(PrintOrderDocument::load(path, loaded, error) && sameOrder(loaded, document),
              "Allow a valid mask reference when the processing reference is automatic", error);
    }
}

void testResourceLimits() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create limit fixture directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("limits.order"));
    const auto before = populatedOrder(root, QStringLiteral("raw"));
    QFile oversized(path);
    if (check(oversized.open(QIODevice::WriteOnly) && oversized.resize(48 * 1024 * 1024 + 1),
              "Create oversized input without decoding it")) {
        oversized.close();
        expectLoadFailure(path, before, "Reject orders larger than the JSON byte limit",
                          QStringLiteral("JSON size"));
    }

    auto object = generatorObject();
    QJsonArray entries;
    for (int i = 0; i < 50'000; ++i) entries.append(QStringLiteral("cal.dng"));
    object.insert(QStringLiteral("dark"), entries);
    object.insert(QStringLiteral("flat"), entries);
    if (writeBytes(path, jsonBytes(object))) {
        expectLoadFailure(path, before, "Enforce the aggregate path entry limit across lists");
    }
    object = generatorObject();
    object.insert(QStringLiteral("userMask"), QString(4097, QLatin1Char('x')));
    if (writeBytes(path, jsonBytes(object))) expectLoadFailure(path, before, "Reject overlong paths");

    object = generatorObject();
    QJsonValue nested = true;
    for (int i = 0; i < 70; ++i) nested = QJsonArray{nested};
    object.insert(QStringLiteral("params"), QJsonObject{{QStringLiteral("nested"), nested}});
    if (writeBytes(path, jsonBytes(object))) expectLoadFailure(path, before, "Reject excessive JSON nesting");
}

quint32 pngCrc(const char* data, int size) {
    quint32 crc = 0xffffffffu;
    for (int i = 0; i < size; ++i) {
        crc ^= static_cast<uchar>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
        }
    }
    return ~crc;
}

void testInvalidMask() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create invalid mask fixture directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("mask.order"));
    const auto before = populatedOrder(root, QStringLiteral("raw"));
    auto object = generatorObject();
    object.insert(QStringLiteral("editedMaskSources"), object.value(QStringLiteral("lights")));
    object.insert(QStringLiteral("editedMaskReference"), object.value(QStringLiteral("reference")));
    const auto reject = [&](const QJsonValue& value, const char* message) {
        object.insert(QStringLiteral("editedMask"), value);
        if (writeBytes(path, jsonBytes(object))) expectLoadFailure(path, before, message);
    };
    const auto rejectPng = [&](const QByteArray& png, const char* message) {
        reject(QString::fromLatin1(png.toBase64()), message);
    };
    reject(10, "Reject a non-string mask payload");
    reject(QStringLiteral("!invalid-base64!"), "Reject malformed base64");
    rejectPng("not a PNG", "Reject a non-PNG payload");

    QImage mask(17, 9, QImage::Format_Grayscale8);
    mask.fill(121);
    QByteArray png;
    QBuffer buffer(&png);
    if (!check(buffer.open(QIODevice::WriteOnly) && mask.save(&buffer, "png"),
               "Prepare valid grayscale PNG fixture")) return;
    object.insert(QStringLiteral("editedMask"), QString::fromLatin1(png.toBase64()));
    if (writeBytes(path, jsonBytes(object))) {
        QString error;
        PrintOrderDocument loaded;
        check(PrintOrderDocument::load(path, loaded, error) && sameMask(mask, loaded.editedMask),
              "Read the pixel-exact PNG fixture with padded scanlines", error);
    }
    rejectPng(png.chopped(12), "Reject PNG missing its end chunk");
    rejectPng(png + "extra", "Reject trailing data beyond PNG end");
    const qsizetype density = png.indexOf("pHYs");
    if (density >= 0) {
        QByteArray corrupt = png;
        corrupt[density + 4] = static_cast<char>(corrupt.at(density + 4) ^ 1);
        rejectPng(corrupt, "Reject an invalid PNG density checksum");
    }
    const qsizetype idat = png.indexOf("IDAT");
    if (check(idat >= 0, "Find PNG compressed pixels")) {
        QByteArray corrupt = png;
        corrupt[idat + 4] = '\0';
        rejectPng(corrupt, "Reject corrupted PNG pixel data");
    }
    const auto rejectSize = [&](quint32 width, quint32 height, const char* message) {
        QByteArray badSize = png;
        qToBigEndian(width, badSize.data() + 16);
        qToBigEndian(height, badSize.data() + 20);
        qToBigEndian(pngCrc(badSize.constData() + 12, 17), badSize.data() + 29);
        rejectPng(badSize, message);
    };
    rejectSize(8192, 8192, "Reject total pixels over budget even when both sides fit");
    rejectSize(0xffffffffu, 0x80000000u, "Reject overflowing PNG dimensions before decode");
    rejectSize(0, 9, "Reject zero-width PNG");
}

void testFailedSaveKeepsTarget() {
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "Create atomicity fixture directory")) return;
    const QDir root(temporary.path());
    const QString path = root.filePath(QStringLiteral("target.order"));
    if (!writeBytes(path, kGeneratorOrder)) return;
    const PrintOrderDocument valid = populatedOrder(root, QStringLiteral("raw"));
    const auto reject = [&](const PrintOrderDocument& candidate, const char* message) {
        QString error;
        const bool saved = candidate.save(path, error);
        check(!saved && !error.isEmpty() && readBytes(path) == kGeneratorOrder, message, error);
    };

    auto candidate = valid;
    candidate.scene = QStringLiteral("invalid");
    reject(candidate, "An invalid scene cannot replace an existing order");
    candidate = valid;
    candidate.lights.append(root.filePath(QStringLiteral("raw/z/../z/\u661F\u7A7A.dng")));
    reject(candidate, "Duplicate lights cannot replace an existing order");
    candidate = valid;
    candidate.selected = root.filePath(QStringLiteral("other/skip.dng"));
    reject(candidate, "Dangling bindings cannot replace an existing order");
    candidate = valid;
    candidate.dark.append(QString());
    reject(candidate, "A bad input path cannot replace an existing order");
    candidate = valid;
    candidate.editedMask = QImage(12, 12, QImage::Format_RGB32);
    reject(candidate, "A non-grayscale mask cannot replace an existing order");
    candidate = valid;
    candidate.editedMask = QImage(8193, 1, QImage::Format_Grayscale8);
    reject(candidate, "An oversized mask cannot replace an existing order");
    candidate = valid;
    candidate.editedMask = {};
    reject(candidate, "Missing mask pixels with old bindings cannot replace an existing order");
    candidate = valid;
    candidate.editedMaskSources.swapItemsAt(0, 1);
    reject(candidate, "A mask with reordered sources cannot replace an existing order");
    candidate = valid;
    candidate.editedMaskReference = candidate.lights.at(1);
    reject(candidate, "A mask with a different reference cannot replace an existing order");
    check(root.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) ==
              QStringList{QStringLiteral("target.order")},
          "Failed saves leave no temporary files");

    QString error = QStringLiteral("stale error");
    check(valid.save(path, error) && error.isEmpty() && readBytes(path) != kGeneratorOrder,
          "A validated order can atomically replace the target", error);
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testGeneratorAndDefaults();
    testRoundTrip();
    testMissingAndRelocation();
    testRelocationCollisions();
    testPlatformPaths();
    testInvalidDocuments();
    testMaskBindings();
    testResourceLimits();
    testInvalidMask();
    testFailedSaveKeepsTarget();
    std::cout << (checks - failures) << '/' << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
