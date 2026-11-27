#include "PrintOrderDocument.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

#include <cmath>
#include <utility>

namespace {

constexpr char kFormat[] = "NebulensPrintOrder";
constexpr int kVersion = 1;
constexpr qsizetype kMiB = 1024 * 1024;
constexpr qsizetype kMaxJsonBytes = 48 * kMiB;
constexpr qsizetype kMaxJsonPunctuation = 250'000;
constexpr int kMaxJsonDepth = 64;
constexpr qsizetype kMaxPathEntries = 100'000;
constexpr qsizetype kMaxPathLength = 4096;
constexpr qsizetype kMaxPathText = 16 * kMiB;
constexpr qsizetype kMaxParamEntries = 16'384;
constexpr qsizetype kMaxParamText = kMiB;
constexpr int kMaxMaskSide = 8192;
constexpr qint64 kMaxMaskPixels = 32 * kMiB;
constexpr qsizetype kMaxPngBytes = kMaxMaskPixels + kMiB;
constexpr qsizetype kMaxMaskBase64Bytes = (kMaxPngBytes + 2) / 3 * 4;

#ifdef Q_OS_WIN
constexpr auto kPathCase = Qt::CaseInsensitive;
#else
constexpr auto kPathCase = Qt::CaseSensitive;
#endif

struct PathListField {
    const char* name;
    QStringList PrintOrderDocument::* member;
};

constexpr PathListField kPathLists[] = {
    {"lights", &PrintOrderDocument::lights},
    {"excluded", &PrintOrderDocument::excluded},
    {"dark", &PrintOrderDocument::dark},
    {"flat", &PrintOrderDocument::flat},
    {"bias", &PrintOrderDocument::bias},
    {"darkFlat", &PrintOrderDocument::darkFlat},
    {"editedMaskSources", &PrintOrderDocument::editedMaskSources},
};

struct PathField {
    const char* name;
    QString PrintOrderDocument::* member;
    bool input = true;
};

constexpr PathField kPaths[] = {
    {"reference", &PrintOrderDocument::reference},
    {"selected", &PrintOrderDocument::selected},
    {"masterDark", &PrintOrderDocument::masterDark},
    {"masterFlat", &PrintOrderDocument::masterFlat},
    {"masterBias", &PrintOrderDocument::masterBias},
    {"masterDarkFlat", &PrintOrderDocument::masterDarkFlat},
    {"userMask", &PrintOrderDocument::userMask},
    {"outputDirectory", &PrintOrderDocument::outputDirectory, false},
    {"editedMaskReference", &PrintOrderDocument::editedMaskReference},
    {"comparisonManifest", &PrintOrderDocument::comparisonManifest, false},
};

bool fail(QString& error, const QString& message) {
    error = message;
    return false;
}

bool validScene(const QString& scene) {
    return scene == QLatin1String("single") || scene == QLatin1String("nightscape") ||
           scene == QLatin1String("sky-ground") || scene == QLatin1String("deep-sky") ||
           scene == QLatin1String("star-trail") || scene == QLatin1String("timelapse");
}

QString absolutePath(const QDir& directory, const QString& path) {
    if (path.isEmpty() || path.size() > kMaxPathLength ||
        path.contains(QChar(u'\0')) || path.startsWith(QLatin1Char(':'))) {
        return {};
    }
    const QString native = QDir::fromNativeSeparators(path);
#ifdef Q_OS_WIN
    // Drive-relative and volume-relative paths depend on the process state.
    if ((native.size() >= 2 && native[1] == QLatin1Char(':') &&
         (native.size() < 3 || native[2] != QLatin1Char('/'))) ||
        (native.startsWith(QLatin1Char('/')) && !native.startsWith(QLatin1String("//")))) {
        return {};
    }
#endif
    const QString absolute = QDir::cleanPath(directory.absoluteFilePath(native));
    return absolute.size() <= kMaxPathLength && QDir::isAbsolutePath(absolute)
        ? absolute : QString();
}

QString pathKey(const QString& absolute) {
    return kPathCase == Qt::CaseInsensitive ? absolute.toCaseFolded() : absolute;
}

QString storedPath(const QDir& directory, const QString& absolute) {
    QString relative = directory.relativeFilePath(absolute);
    if (relative.isEmpty()) relative = QStringLiteral(".");
    if (relative.startsWith(QLatin1Char(':'))) relative.prepend(QStringLiteral("./"));
    return absolutePath(directory, relative).compare(absolute, kPathCase) == 0
        ? relative : absolute;
}

template <typename Document, typename Visitor>
bool visitInputs(Document& document, Visitor visit) {
    for (const auto& field : kPathLists) {
        for (auto& path : document.*field.member) {
            if (!path.isEmpty() && !visit(path)) return false;
        }
    }
    for (const auto& field : kPaths) {
        auto& path = document.*field.member;
        if (field.input && !path.isEmpty() && !visit(path)) return false;
    }
    return true;
}

// Bound parser allocations before parsing: an embedded PNG is one large string,
// while many small JSON values can cost considerably more than their byte size.
bool boundedJsonShape(const QByteArray& bytes) {
    qsizetype punctuation = 0;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (const char c : bytes) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxJsonDepth || ++punctuation > kMaxJsonPunctuation) return false;
        } else if (c == ']' || c == '}') {
            if (--depth < 0) return false;
        } else if (c == ',' || c == ':') {
            if (++punctuation > kMaxJsonPunctuation) return false;
        }
    }
    return true;
}

struct ParamsBudget {
    qsizetype entries = kMaxParamEntries;
    qsizetype text = kMaxParamText;

    bool accepts(const QJsonValue& value, int depth = 2) {
        if (--entries < 0 || depth > kMaxJsonDepth) return false;
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                if ((text -= it.key().size()) < 0 || !accepts(it.value(), depth + 1)) return false;
            }
        } else if (value.isArray()) {
            const auto array = value.toArray();
            for (const QJsonValue item : array) {
                if (!accepts(item, depth + 1)) return false;
            }
        } else if (value.isString()) {
            return (text -= value.toString().size()) >= 0;
        } else if (value.isDouble()) {
            return std::isfinite(value.toDouble());
        }
        return true;
    }
};

bool readPaths(const QJsonObject& json, PrintOrderDocument& document, QString& error) {
    qsizetype entries = kMaxPathEntries;
    for (const auto& field : kPathLists) {
        const auto value = json.value(QLatin1String(field.name));
        if (value.isUndefined()) continue;
        if (!value.isArray()) {
            return fail(error, QStringLiteral("%1 must be a path array.").arg(QLatin1String(field.name)));
        }
        const QJsonArray paths = value.toArray();
        if ((entries -= paths.size()) < 0) {
            return fail(error, QStringLiteral("Print order has too many path entries."));
        }
        auto& list = document.*field.member;
        list.reserve(paths.size());
        for (const QJsonValue item : paths) {
            if (!item.isString()) {
                return fail(error, QStringLiteral("%1 contains a non-string path.").arg(QLatin1String(field.name)));
            }
            list.append(item.toString());
        }
    }
    for (const auto& field : kPaths) {
        const auto value = json.value(QLatin1String(field.name));
        if (value.isUndefined()) continue;
        if (!value.isString()) {
            return fail(error, QStringLiteral("%1 must be a path string.").arg(QLatin1String(field.name)));
        }
        document.*field.member = value.toString();
    }
    return true;
}

bool normalizePaths(PrintOrderDocument& document, const QDir& directory, QString& error) {
    qsizetype entries = kMaxPathEntries;
    qsizetype text = kMaxPathText;
    const auto normalize = [&](QString& path, const char* field) {
        const QString absolute = absolutePath(directory, path);
        if (absolute.isEmpty()) {
            return fail(error, QStringLiteral("%1 contains an invalid path.").arg(QLatin1String(field)));
        }
        if ((text -= absolute.size()) < 0) {
            return fail(error, QStringLiteral("Print order path data is too large."));
        }
        path = absolute;
        return true;
    };
    for (const auto& field : kPathLists) {
        auto& list = document.*field.member;
        if ((entries -= list.size()) < 0) {
            return fail(error, QStringLiteral("Print order has too many path entries."));
        }
        for (QString& path : list) {
            if (!normalize(path, field.name)) return false;
        }
    }
    for (const auto& field : kPaths) {
        auto& path = document.*field.member;
        if (!path.isEmpty() && !normalize(path, field.name)) return false;
    }
    return true;
}

bool validateBindings(PrintOrderDocument& document, bool hasEditedMask, QString& error) {
    QHash<QString, QString> lights;
    lights.reserve(document.lights.size());
    for (const auto& path : document.lights) {
        const QString key = pathKey(path);
        if (lights.contains(key)) return fail(error, QStringLiteral("Duplicate light: %1").arg(path));
        lights.insert(key, path);
    }

    const auto bindLight = [&](QString& path, const char* field) {
        if (path.isEmpty()) return true;
        const auto light = lights.constFind(pathKey(path));
        if (light == lights.cend()) {
            return fail(error, QStringLiteral("%1 does not refer to a light: %2").arg(QLatin1String(field), path));
        }
        // Consumers use the exact spelling from the light list, even on Windows.
        path = light.value();
        return true;
    };
    if (!bindLight(document.reference, "reference") ||
        !bindLight(document.selected, "selected")) return false;
    QSet<QString> excluded;
    excluded.reserve(document.excluded.size());
    for (auto& path : document.excluded) {
        if (!bindLight(path, "excluded")) return false;
        if (path == document.reference) {
            return fail(error, QStringLiteral("reference names an excluded light."));
        }
        excluded.insert(path);
    }

    if (!hasEditedMask) {
        if (!document.editedMaskSources.isEmpty() || !document.editedMaskReference.isEmpty()) {
            return fail(error, QStringLiteral("Edited mask bindings require editedMask pixels."));
        }
        return true;
    }
    if (document.editedMaskReference.isEmpty()) {
        return fail(error, QStringLiteral("editedMask requires editedMaskReference."));
    }
    if (!bindLight(document.editedMaskReference, "editedMaskReference")) return false;
    if (excluded.contains(document.editedMaskReference) ||
        (!document.reference.isEmpty() && document.reference != document.editedMaskReference)) {
        return fail(error, QStringLiteral("editedMaskReference must be an active light matching reference, if set."));
    }
    for (auto& path : document.editedMaskSources) {
        if (!bindLight(path, "editedMaskSources")) return false;
    }
    QStringList activeLights;
    activeLights.reserve(document.lights.size() - excluded.size());
    for (const auto& light : document.lights) {
        if (!excluded.contains(light)) activeLights.append(light);
    }
    if (document.editedMaskSources != activeLights) {
        return fail(error, QStringLiteral("editedMaskSources must match the ordered active lights."));
    }
    return true;
}

bool validMaskSize(qint64 width, qint64 height) {
    return width > 0 && height > 0 && width <= kMaxMaskSide && height <= kMaxMaskSide &&
           width * height <= kMaxMaskPixels;
}

bool validSmallPngChunkCrc(const uchar* type, int size) {
    quint32 crc = 0xffffffffu;
    for (int i = 0; i < size; ++i) {
        crc ^= type[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
        }
    }
    return ~crc == qFromBigEndian<quint32>(type + size);
}

// Accept a single, non-interlaced gray8 PNG with no ancillary payloads except
// Qt's fixed-size pixel density chunk. Check dimensions and framing before libpng.
QSize maskPngSize(const QByteArray& png) {
    if (png.size() < 45 || png.size() > kMaxPngBytes ||
        !png.startsWith("\x89PNG\r\n\x1a\n")) return {};
    const auto* bytes = reinterpret_cast<const uchar*>(png.constData());
    if (qFromBigEndian<quint32>(bytes + 8) != 13 ||
        QByteArrayView(png.constData() + 12, 4) != "IHDR" ||
        bytes[24] != 8 || bytes[25] != 0 || bytes[26] != 0 || bytes[27] != 0 || bytes[28] != 0) {
        return {};
    }
    const quint32 width = qFromBigEndian<quint32>(bytes + 16);
    const quint32 height = qFromBigEndian<quint32>(bytes + 20);
    if (!validMaskSize(width, height) || !validSmallPngChunkCrc(bytes + 12, 17)) return {};

    bool density = false;
    bool idat = false;
    qsizetype offset = 33;
    for (int chunks = 0; chunks < 16'384 && png.size() - offset >= 12; ++chunks) {
        const quint32 length = qFromBigEndian<quint32>(bytes + offset);
        if (length > png.size() - offset - 12) return {};
        const QByteArrayView type(png.constData() + offset + 4, 4);
        if (type == "IDAT") {
            idat = true;
        } else if (type == "IEND") {
            if (idat && length == 0 && png.size() - offset == 12 &&
                validSmallPngChunkCrc(bytes + offset + 4, 4)) {
                return QSize(static_cast<int>(width), static_cast<int>(height));
            }
            return {};
        } else if (type == "pHYs" && !density && !idat && length == 9) {
            if (!validSmallPngChunkCrc(bytes + offset + 4, 13)) return {};
            density = true;
        } else {
            return {};
        }
        offset += 12 + length;
    }
    return {};
}

bool readMask(const QJsonValue& value, QImage& mask, QString& error) {
    if (value.isUndefined()) return true;
    if (!value.isString()) {
        return fail(error, QStringLiteral("editedMask must be a bounded base64 PNG."));
    }
    const QString payload = value.toString();
    if (payload.isEmpty() || payload.size() > kMaxMaskBase64Bytes) {
        return fail(error, QStringLiteral("editedMask must be a bounded base64 PNG."));
    }
    auto png = QByteArray::fromBase64Encoding(payload.toLatin1(),
                                              QByteArray::AbortOnBase64DecodingErrors);
    const QSize size = png ? maskPngSize(png.decoded) : QSize();
    if (size.isEmpty()) {
        return fail(error, QStringLiteral("editedMask is not a valid bounded grayscale PNG."));
    }
    QBuffer buffer(&png.decoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "png");
    reader.setAutoDetectImageFormat(false);
    QImage image = reader.read();
    if (image.size() != size || image.format() != QImage::Format_Grayscale8) {
        return fail(error, QStringLiteral("Cannot decode editedMask as grayscale PNG."));
    }
    mask = std::move(image);
    return true;
}

bool writeMask(const QImage& mask, QJsonObject& json, QString& error) {
    if (mask.isNull()) return true;
    if (mask.format() != QImage::Format_Grayscale8 || !validMaskSize(mask.width(), mask.height())) {
        return fail(error, QStringLiteral("editedMask must be a bounded 8-bit grayscale image."));
    }
    // Wrap the pixels without copying them or serializing image metadata.
    const QImage pixels(mask.constBits(), mask.width(), mask.height(),
                        mask.bytesPerLine(), QImage::Format_Grayscale8);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "png");
    if (!writer.write(pixels) || maskPngSize(png) != mask.size()) {
        return fail(error, QStringLiteral("Cannot encode editedMask as a bounded grayscale PNG."));
    }
    json.insert(QStringLiteral("editedMask"), QString::fromLatin1(png.toBase64()));
    return true;
}

} // namespace

bool PrintOrderDocument::load(const QString& path, PrintOrderDocument& result, QString& error) {
    error.clear();
    const QFileInfo info(absolutePath(QDir::current(), path));
    QFile file(info.filePath());
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
        return fail(error, QStringLiteral("Cannot open print order: %1").arg(path));
    }
    if (file.size() > kMaxJsonBytes) {
        return fail(error, QStringLiteral("Print order exceeds the JSON size limit."));
    }
    const QByteArray bytes = file.read(kMaxJsonBytes + 1);
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    if (bytes.size() > kMaxJsonBytes || !boundedJsonShape(bytes)) {
        return fail(error, QStringLiteral("Print order exceeds the JSON size or structure limit."));
    }

    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(error, QStringLiteral("Invalid print order JSON: %1").arg(parseError.errorString()));
    }
    if (!json.isObject()) return fail(error, QStringLiteral("Print order JSON must be an object."));
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)) {
        return fail(error, QStringLiteral("Not a Nebulens print order."));
    }
    const auto version = root.value(QStringLiteral("version"));
    if (!version.isDouble() || version.toDouble() != kVersion) {
        return fail(error, QStringLiteral("Unsupported print order version."));
    }

    PrintOrderDocument candidate;
    candidate.scene = root.value(QStringLiteral("scene")).toString();
    if (!validScene(candidate.scene)) return fail(error, QStringLiteral("Invalid print order scene."));
    const auto automatic = root.value(QStringLiteral("automaticStill"));
    if (!automatic.isUndefined()) {
        if (!automatic.isBool()) return fail(error, QStringLiteral("automaticStill must be a boolean."));
        candidate.automaticStill = automatic.toBool();
    }
    const auto paramsValue = root.value(QStringLiteral("params"));
    if (!paramsValue.isUndefined()) {
        if (!paramsValue.isObject() || !ParamsBudget().accepts(paramsValue)) {
            return fail(error, QStringLiteral("params must be a bounded JSON object."));
        }
        candidate.params = paramsValue.toObject();
    }
    const auto maskValue = root.value(QStringLiteral("editedMask"));
    if (!readPaths(root, candidate, error) ||
        !normalizePaths(candidate, info.absoluteDir(), error) ||
        !validateBindings(candidate, !maskValue.isUndefined(), error) ||
        !readMask(maskValue, candidate.editedMask, error)) {
        return false;
    }
    result = std::move(candidate);
    return true;
}

bool PrintOrderDocument::save(const QString& path, QString& error) const {
    error.clear();
    const QString absolute = absolutePath(QDir::current(), path);
    if (absolute.isEmpty()) return fail(error, QStringLiteral("Invalid print order destination."));
    if (!validScene(scene)) return fail(error, QStringLiteral("Invalid print order scene."));
    if (!ParamsBudget().accepts(params)) {
        return fail(error, QStringLiteral("params must be a bounded JSON object."));
    }
    const QDir directory = QFileInfo(absolute).absoluteDir();
    PrintOrderDocument normalized = *this;
    if (!normalizePaths(normalized, directory, error) ||
        !validateBindings(normalized, !editedMask.isNull(), error)) {
        return false;
    }

    QJsonObject root{
        {QStringLiteral("format"), QLatin1String(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("scene"), scene},
        {QStringLiteral("automaticStill"), automaticStill},
        {QStringLiteral("params"), params},
    };
    for (const auto& field : kPathLists) {
        const auto& list = normalized.*field.member;
        if (list.isEmpty() && field.member != &PrintOrderDocument::lights) continue;
        QJsonArray array;
        for (const auto& item : list) array.append(storedPath(directory, item));
        root.insert(QLatin1String(field.name), array);
    }
    for (const auto& field : kPaths) {
        const auto& item = normalized.*field.member;
        if (!item.isEmpty()) root.insert(QLatin1String(field.name), storedPath(directory, item));
    }
    if (!writeMask(editedMask, root, error)) return false;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaxJsonBytes) {
        return fail(error, QStringLiteral("Print order exceeds the JSON size limit."));
    }

    QSaveFile file(absolute);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return fail(error, QStringLiteral("Cannot save print order: %1").arg(file.errorString()));
    }
    return true;
}

QStringList PrintOrderDocument::missingInputPaths() const {
    const QDir directory = QDir::current();
    QStringList missing;
    QSet<QString> seen;
    visitInputs(*this, [&](const QString& path) {
        const QString absolute = absolutePath(directory, path);
        const QString key = pathKey(absolute.isEmpty() ? path : absolute);
        if (!seen.contains(key)) {
            seen.insert(key);
            if (absolute.isEmpty() || !QFileInfo(absolute).isFile()) {
                missing.append(absolute.isEmpty() ? path : absolute);
            }
        }
        return true;
    });
    return missing;
}

bool PrintOrderDocument::relocateDirectory(const QString& oldDir, const QString& newDir) {
    const QDir directory = QDir::current();
    QString oldPrefix = absolutePath(directory, oldDir);
    QString newPrefix = absolutePath(directory, newDir);
    if (oldPrefix.isEmpty() || newPrefix.isEmpty() || oldPrefix == newPrefix) return false;
    if (!oldPrefix.endsWith(QLatin1Char('/'))) oldPrefix += QLatin1Char('/');
    if (!newPrefix.endsWith(QLatin1Char('/'))) newPrefix += QLatin1Char('/');

    PrintOrderDocument candidate = *this;
    QHash<QString, QString> sourcesByTarget;
    bool changed = false;
    const bool unambiguous = visitInputs(candidate, [&](QString& path) {
        const QString source = absolutePath(directory, path);
        if (source.isEmpty()) return false;
        const QString target = source.startsWith(oldPrefix, kPathCase)
            ? newPrefix + source.mid(oldPrefix.size()) : source;
        if (target.size() > kMaxPathLength) return false;
        const QString targetKey = pathKey(target);
        const QString sourceKey = pathKey(source);
        const auto existing = sourcesByTarget.constFind(targetKey);
        if (existing == sourcesByTarget.cend()) {
            sourcesByTarget.insert(targetKey, sourceKey);
        } else if (existing.value() != sourceKey) {
            return false;
        }
        if (target != source) {
            path = target;
            changed = true;
        }
        return true;
    });
    if (!unambiguous || !changed) return false;
    *this = std::move(candidate);
    return true;
}
