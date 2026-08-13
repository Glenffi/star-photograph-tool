#include "MasterFrameIO.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace {

constexpr char kMagic[] = {'S', 'P', 'M', 'S', 'T', 'R', '0', '2'};
constexpr quint32 kSchemaVersion = 2;
constexpr qsizetype kDigestSize = 32;
constexpr size_t kChunkFloats = 16384;

bool validRole(quint8 value) {
    return value <= static_cast<quint8>(
        RawCalibrationEngine::MasterRole::DarkFlat);
}

bool validPattern(const std::array<uint8_t, 4>& pattern) {
    std::array<int, 4> counts = {};
    for (uint8_t colour : pattern) {
        if (colour > 3) return false;
        ++counts[colour];
    }
    return counts[0] == 1 && counts[2] == 1 &&
        counts[1] + counts[3] == 2;
}

bool readHeader(QFile& file, RawCalibrationEngine::MasterFrame& master,
                quint64& pixelCount, QByteArray& authenticatedHeader,
                QString& error) {
    char magic[sizeof(kMagic)] = {};
    if (file.read(magic, sizeof(magic)) != sizeof(magic) ||
        std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        error = QString::fromUtf8("不是有效的 StarProcessor Master 文件");
        return false;
    }

    const qint64 headerStart = file.pos();
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_2);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    quint32 schema = 0;
    quint8 role = 0;
    quint8 flags = 0;
    quint16 reserved = 0;
    qint32 width = 0;
    qint32 height = 0;
    qint32 rawWidth = 0;
    qint32 rawHeight = 0;
    qint32 topMargin = 0;
    qint32 leftMargin = 0;
    qint32 iso = 0;
    double exposure = 0.0;
    quint16 saturation = 0;
    quint8 cfa0 = 0;
    quint8 cfa1 = 0;
    quint8 cfa2 = 0;
    quint8 cfa3 = 0;
    quint32 cameraLength = 0;
    stream >> schema >> role >> flags >> reserved
           >> width >> height >> rawWidth >> rawHeight
           >> topMargin >> leftMargin >> iso >> exposure >> saturation
           >> cfa0 >> cfa1 >> cfa2 >> cfa3 >> cameraLength;
    Q_UNUSED(reserved)
    if (stream.status() != QDataStream::Ok || schema != kSchemaVersion ||
        !validRole(role) || (flags & ~0x03U) != 0 ||
        cameraLength == 0 || cameraLength > 4096 ||
        width <= 0 || height <= 0 || rawWidth <= 0 || rawHeight <= 0 ||
        topMargin < 0 || leftMargin < 0 || width > rawWidth ||
        height > rawHeight || leftMargin > rawWidth - width ||
        topMargin > rawHeight - height || iso <= 0 ||
        !std::isfinite(exposure) || exposure <= 0.0 || saturation == 0 ||
        width > std::numeric_limits<int>::max() / height ||
        !validPattern({cfa0, cfa1, cfa2, cfa3})) {
        error = QString::fromUtf8("Master 文件头损坏或版本不受支持");
        return false;
    }
    QByteArray camera;
    camera.resize(static_cast<qsizetype>(cameraLength));
    if (stream.readRawData(camera.data(), static_cast<qint64>(cameraLength)) !=
            static_cast<qint64>(cameraLength)) {
        error = QString::fromUtf8("Master 相机元数据不完整");
        return false;
    }
    stream >> pixelCount;
    if (stream.status() != QDataStream::Ok ||
        pixelCount != static_cast<quint64>(width) * height ||
        pixelCount > std::numeric_limits<size_t>::max() / sizeof(float)) {
        error = QString::fromUtf8("Master 文件头损坏或版本不受支持");
        return false;
    }
    const qint64 headerEnd = file.pos();
    if (headerEnd < headerStart || !file.seek(headerStart)) {
        error = QString::fromUtf8("无法校验 Master 元数据");
        return false;
    }
    const QByteArray serializedHeader = file.read(headerEnd - headerStart);
    if (serializedHeader.size() != headerEnd - headerStart ||
        !file.seek(headerEnd)) {
        error = QString::fromUtf8("Master 元数据不完整");
        return false;
    }
    authenticatedHeader = QByteArray(kMagic, sizeof(kMagic)) +
        serializedHeader;
    const QByteArray storedHeaderDigest = file.read(kDigestSize);
    if (storedHeaderDigest.size() != kDigestSize ||
        storedHeaderDigest != QCryptographicHash::hash(
            authenticatedHeader, QCryptographicHash::Sha256)) {
        error = QString::fromUtf8(
            "Master 元数据校验失败，文件可能已损坏");
        return false;
    }
    if (pixelCount >
        static_cast<quint64>(std::numeric_limits<qint64>::max() / 4) ||
        file.size() - file.pos() !=
            static_cast<qint64>(pixelCount * 4 + kDigestSize)) {
        error = QString::fromUtf8("Master 文件长度与声明的图像尺寸不一致");
        return false;
    }

    RawCalibrationEngine::MasterFrame candidate;
    candidate.role = static_cast<RawCalibrationEngine::MasterRole>(role);
    candidate.width = width;
    candidate.height = height;
    candidate.rawWidth = rawWidth;
    candidate.rawHeight = rawHeight;
    candidate.topMargin = topMargin;
    candidate.leftMargin = leftMargin;
    candidate.iso = iso;
    candidate.exposureTime = exposure;
    candidate.cameraModel = camera.toStdString();
    candidate.cfaPattern = {cfa0, cfa1, cfa2, cfa3};
    candidate.saturation = saturation;
    candidate.normalizedFlat = (flags & 0x01U) != 0;
    candidate.darkIncludesBiasPedestal = (flags & 0x02U) != 0;
    master = std::move(candidate);
    return true;
}

bool readFile(const QString& path, RawCalibrationEngine::MasterFrame& master,
              QString& error, bool includePixels) {
    error.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString::fromUtf8("无法打开 Master 文件：%1")
                    .arg(file.errorString());
        return false;
    }
    RawCalibrationEngine::MasterFrame candidate;
    quint64 pixelCount = 0;
    QByteArray authenticatedHeader;
    if (!readHeader(file, candidate, pixelCount, authenticatedHeader,
                    error)) return false;
    if (!includePixels) {
        if (candidate.role == RawCalibrationEngine::MasterRole::Flat &&
            !candidate.normalizedFlat) {
            error = QString::fromUtf8(
                "Master Flat 尚未完成偏移校准和 CFA 分相归一化");
            return false;
        }
        master = std::move(candidate);
        return true;
    }

    try {
        candidate.data.resize(static_cast<size_t>(pixelCount));
    } catch (const std::bad_alloc&) {
        error = QString::fromUtf8("内存不足，无法加载 Master 像素");
        return false;
    }
    QCryptographicHash fileHash(QCryptographicHash::Sha256);
    fileHash.addData(authenticatedHeader);
    size_t offset = 0;
    while (offset < candidate.data.size()) {
        const size_t count = std::min(kChunkFloats,
                                      candidate.data.size() - offset);
        const qint64 bytes = static_cast<qint64>(count * sizeof(quint32));
        const QByteArray chunk = file.read(bytes);
        if (chunk.size() != bytes) {
            error = QString::fromUtf8("Master 像素数据不完整");
            return false;
        }
        fileHash.addData(chunk);
        for (size_t i = 0; i < count; ++i) {
            const auto* bytesAt = reinterpret_cast<const uchar*>(
                chunk.constData() + static_cast<qsizetype>(i * 4));
            const quint32 bits = qFromLittleEndian<quint32>(bytesAt);
            std::memcpy(&candidate.data[offset + i], &bits, sizeof(bits));
        }
        offset += count;
    }
    const QByteArray storedDigest = file.read(kDigestSize);
    if (storedDigest.size() != kDigestSize ||
        storedDigest != fileHash.result()) {
        error = QString::fromUtf8("Master 完整性校验失败，文件可能已损坏");
        return false;
    }
    if (!candidate.complete() ||
        (candidate.role == RawCalibrationEngine::MasterRole::Flat &&
         !candidate.normalizedFlat)) {
        error = QString::fromUtf8("Master 元数据或像素值无效");
        return false;
    }
    master = std::move(candidate);
    return true;
}

} // namespace

bool MasterFrameIO::save(
    const QString& path, const RawCalibrationEngine::MasterFrame& master,
    QString& error) {
    error.clear();
    if (!master.complete() ||
        (master.role == RawCalibrationEngine::MasterRole::Flat &&
         !master.normalizedFlat)) {
        error = QString::fromUtf8("Master 数据不完整，无法保存");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = QString::fromUtf8("无法创建 Master 文件：%1")
                    .arg(file.errorString());
        return false;
    }
    QByteArray serializedHeader;
    QDataStream stream(&serializedHeader, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_2);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
    const quint8 flags = static_cast<quint8>(
        (master.normalizedFlat ? 0x01U : 0U) |
        (master.darkIncludesBiasPedestal ? 0x02U : 0U));
    const QByteArray camera = QByteArray::fromStdString(master.cameraModel);
    if (camera.isEmpty() || camera.size() > 4096) {
        error = QString::fromUtf8("Master 相机名称长度无效");
        return false;
    }
    stream << kSchemaVersion
           << static_cast<quint8>(master.role) << flags << quint16{0}
           << static_cast<qint32>(master.width)
           << static_cast<qint32>(master.height)
           << static_cast<qint32>(master.rawWidth)
           << static_cast<qint32>(master.rawHeight)
           << static_cast<qint32>(master.topMargin)
           << static_cast<qint32>(master.leftMargin)
           << static_cast<qint32>(master.iso)
           << master.exposureTime << static_cast<quint16>(master.saturation)
           << static_cast<quint8>(master.cfaPattern[0])
           << static_cast<quint8>(master.cfaPattern[1])
           << static_cast<quint8>(master.cfaPattern[2])
           << static_cast<quint8>(master.cfaPattern[3])
           << static_cast<quint32>(camera.size());
    if (stream.writeRawData(camera.constData(), camera.size()) !=
            camera.size()) {
        error = QString::fromUtf8("写入 Master 相机元数据失败");
        return false;
    }
    stream << static_cast<quint64>(master.data.size());
    if (stream.status() != QDataStream::Ok) {
        error = QString::fromUtf8("写入 Master 元数据失败");
        return false;
    }

    const QByteArray authenticatedHeader =
        QByteArray(kMagic, sizeof(kMagic)) + serializedHeader;
    const QByteArray headerDigest = QCryptographicHash::hash(
        authenticatedHeader, QCryptographicHash::Sha256);
    if (file.write(authenticatedHeader) != authenticatedHeader.size() ||
        file.write(headerDigest) != headerDigest.size()) {
        error = QString::fromUtf8("写入 Master 文件头失败");
        return false;
    }

    QCryptographicHash fileHash(QCryptographicHash::Sha256);
    fileHash.addData(authenticatedHeader);
    QByteArray chunk;
    chunk.resize(static_cast<qsizetype>(kChunkFloats * 4));
    size_t offset = 0;
    while (offset < master.data.size()) {
        const size_t count = std::min(kChunkFloats,
                                      master.data.size() - offset);
        chunk.resize(static_cast<qsizetype>(count * 4));
        for (size_t i = 0; i < count; ++i) {
            quint32 bits = 0;
            std::memcpy(&bits, &master.data[offset + i], sizeof(bits));
            qToLittleEndian<quint32>(
                bits, reinterpret_cast<uchar*>(
                    chunk.data() + static_cast<qsizetype>(i * 4)));
        }
        fileHash.addData(chunk);
        if (file.write(chunk) != chunk.size()) {
            error = QString::fromUtf8("写入 Master 像素失败");
            return false;
        }
        offset += count;
    }
    const QByteArray digest = fileHash.result();
    if (file.write(digest) != digest.size() || !file.commit()) {
        error = QString::fromUtf8("提交 Master 文件失败：%1")
                    .arg(file.errorString());
        return false;
    }
    return true;
}

bool MasterFrameIO::load(
    const QString& path, RawCalibrationEngine::MasterFrame& master,
    QString& error) {
    return readFile(path, master, error, true);
}

bool MasterFrameIO::loadHeader(
    const QString& path, RawCalibrationEngine::MasterFrame& master,
    QString& error) {
    return readFile(path, master, error, false);
}
