#include "DeepSkyCalibrationPreflight.h"
#include "MasterFrameIO.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

struct FindingGroup {
    DeepSkyCalibrationPreflight::Severity severity =
        DeepSkyCalibrationPreflight::Severity::Error;
    QString message;
    QStringList examples;
    int count = 0;
};

QString pathIdentity(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

std::vector<FindingGroup> groupFindings(
    const std::vector<DeepSkyCalibrationPreflight::Finding>& findings,
    bool errors, bool warnings) {
    QHash<QString, int> indexByKey;
    std::vector<FindingGroup> groups;
    for (const auto& finding : findings) {
        const bool include =
            (errors && finding.severity ==
                           DeepSkyCalibrationPreflight::Severity::Error) ||
            (warnings && finding.severity ==
                             DeepSkyCalibrationPreflight::Severity::Warning);
        if (!include) continue;
        const QString key = QString::number(
            static_cast<int>(finding.severity)) + QLatin1Char('|') +
            finding.groupKey;
        auto position = indexByKey.constFind(key);
        if (position == indexByKey.constEnd()) {
            const int index = static_cast<int>(groups.size());
            indexByKey.insert(key, index);
            groups.push_back({finding.severity, finding.message, {}, 0});
            position = indexByKey.constFind(key);
        }
        FindingGroup& group = groups[static_cast<size_t>(position.value())];
        ++group.count;
        if (!finding.path.isEmpty() && group.examples.size() < 2) {
            group.examples.append(QFileInfo(finding.path).fileName());
        }
    }
    std::stable_sort(
        groups.begin(), groups.end(),
        [](const FindingGroup& first, const FindingGroup& second) {
            return first.severity == second.severity
                ? first.count > second.count
                : first.severity ==
                      DeepSkyCalibrationPreflight::Severity::Error;
        });
    return groups;
}

QString describeGroup(const FindingGroup& group, bool includeSeverity) {
    QString text;
    if (includeSeverity) {
        text = group.severity ==
                       DeepSkyCalibrationPreflight::Severity::Error
            ? QString::fromUtf8("[必须修复] ")
            : QString::fromUtf8("[建议] ");
    }
    text += group.message;
    if (!group.examples.isEmpty()) {
        text += QString::fromUtf8("（%1 张，例如 %2）")
                    .arg(group.count)
                    .arg(group.examples.join(QString::fromUtf8("、")));
    }
    return text;
}

} // namespace

bool DeepSkyCalibrationPreflight::Report::hasErrors() const {
    return std::any_of(
        findings.begin(), findings.end(), [](const Finding& finding) {
            return finding.severity == Severity::Error;
        });
}

QString DeepSkyCalibrationPreflight::Report::userMessage(
    int maximumGroups) const {
    maximumGroups = std::max(1, maximumGroups);
    QString message = QString::fromUtf8(
        "校准素材预检%1。\n"
        "Light %2 张 · Dark %3 张 · Flat %4 张 · Bias %5 张")
        .arg(hasErrors() ? QString::fromUtf8("未通过")
                         : QString::fromUtf8("通过"))
        .arg(lightCount)
        .arg(darkCount)
        .arg(flatCount)
        .arg(biasCount);
    if (!referenceCamera.isEmpty()) {
        message += QString::fromUtf8(
            "\n基准 Light：%1 · ISO %2 · %3 秒 · %4×%5")
            .arg(referenceCamera)
            .arg(referenceIso)
            .arg(referenceExposure, 0, 'g', 6)
            .arg(referenceWidth)
            .arg(referenceHeight);
    }
    if (darkFlatCount > 0 || masterDarkCount > 0 || masterFlatCount > 0 ||
        masterBiasCount > 0 || masterDarkFlatCount > 0) {
        message += QString::fromUtf8(
            "\nDark Flat %1 张 · Master Dark/Flat/Bias/Dark Flat %2/%3/%4/%5")
            .arg(darkFlatCount)
            .arg(masterDarkCount)
            .arg(masterFlatCount)
            .arg(masterBiasCount)
            .arg(masterDarkFlatCount);
    }

    const std::vector<FindingGroup> groups =
        groupFindings(findings, true, true);
    if (groups.empty()) {
        return message + QString::fromUtf8(
            "\n\n元数据规则均符合，可以继续解包 Bayer 并检查 Flat 亮度。");
    }
    message += QString::fromUtf8("\n\n检查结果：");
    const int shown = std::min<int>(maximumGroups, groups.size());
    for (int i = 0; i < shown; ++i) {
        message += QString::fromUtf8("\n• ") +
            describeGroup(groups[static_cast<size_t>(i)], true);
    }
    if (shown < static_cast<int>(groups.size())) {
        message += QString::fromUtf8("\n• 另有 %1 类问题未展开")
                       .arg(static_cast<int>(groups.size()) - shown);
    }
    message += QString::fromUtf8(
        "\n\n预检只读取 RAW 头信息；CFA 布局和 Flat 实际亮度会在正式校准时继续检查。");
    return message;
}

QStringList DeepSkyCalibrationPreflight::Report::warningMessages(
    int maximumGroups) const {
    maximumGroups = std::max(1, maximumGroups);
    const std::vector<FindingGroup> groups =
        groupFindings(findings, false, true);
    QStringList messages;
    const int shown = std::min<int>(maximumGroups, groups.size());
    for (int i = 0; i < shown; ++i) {
        messages.append(describeGroup(groups[static_cast<size_t>(i)], false));
    }
    if (shown < static_cast<int>(groups.size())) {
        messages.append(QString::fromUtf8("另有 %1 类建议未展开")
                            .arg(static_cast<int>(groups.size()) - shown));
    }
    return messages;
}

DeepSkyCalibrationPreflight::Report DeepSkyCalibrationPreflight::inspect(
    RawImageLoader& loader, const Inputs& inputs) {
    std::vector<FrameRecord> frames;
    const qsizetype totalCount = inputs.lightPaths.size() +
        inputs.darkPaths.size() + inputs.flatPaths.size() +
        inputs.biasPaths.size() + inputs.darkFlatPaths.size() +
        inputs.masterDarkPaths.size() + inputs.masterFlatPaths.size() +
        inputs.masterBiasPaths.size() + inputs.masterDarkFlatPaths.size();
    frames.reserve(static_cast<size_t>(totalCount));
    auto append = [&](const QStringList& paths, Role role) {
        for (const QString& path : paths) {
            FrameRecord frame;
            frame.path = path;
            frame.role = role;
            frame.readable = loader.loadMetadata(path, frame.metadata);
            frames.push_back(std::move(frame));
        }
    };
    auto appendMasters = [&](const QStringList& paths, Role role) {
        for (const QString& path : paths) {
            FrameRecord frame;
            frame.path = path;
            frame.role = role;
            RawCalibrationEngine::MasterFrame master;
            QString error;
            frame.readable = MasterFrameIO::loadHeader(path, master, error);
            const RawCalibrationEngine::MasterRole expectedRole =
                role == Role::MasterDark
                    ? RawCalibrationEngine::MasterRole::Dark
                    : role == Role::MasterFlat
                        ? RawCalibrationEngine::MasterRole::Flat
                        : role == Role::MasterBias
                            ? RawCalibrationEngine::MasterRole::Bias
                            : RawCalibrationEngine::MasterRole::DarkFlat;
            const bool correctRole = frame.readable &&
                master.role == expectedRole &&
                (role != Role::MasterFlat || master.normalizedFlat);
            frame.readable = correctRole;
            if (frame.readable) {
                frame.metadata.width = master.width;
                frame.metadata.height = master.height;
                frame.metadata.iso = master.iso;
                frame.metadata.exposureTime = master.exposureTime;
                frame.metadata.cameraModel = master.cameraModel;
                frame.masterDarkIncludesBiasPedestal =
                    master.darkIncludesBiasPedestal;
            }
            frames.push_back(std::move(frame));
        }
    };
    append(inputs.lightPaths, Role::Light);
    append(inputs.darkPaths, Role::Dark);
    append(inputs.flatPaths, Role::Flat);
    append(inputs.biasPaths, Role::Bias);
    append(inputs.darkFlatPaths, Role::DarkFlat);
    appendMasters(inputs.masterDarkPaths, Role::MasterDark);
    appendMasters(inputs.masterFlatPaths, Role::MasterFlat);
    appendMasters(inputs.masterBiasPaths, Role::MasterBias);
    appendMasters(inputs.masterDarkFlatPaths, Role::MasterDarkFlat);
    return validate(frames);
}

DeepSkyCalibrationPreflight::Report DeepSkyCalibrationPreflight::inspect(
    RawImageLoader& loader,
    const QStringList& lightPaths,
    const QStringList& darkPaths,
    const QStringList& flatPaths,
    const QStringList& biasPaths) {
    Inputs inputs;
    inputs.lightPaths = lightPaths;
    inputs.darkPaths = darkPaths;
    inputs.flatPaths = flatPaths;
    inputs.biasPaths = biasPaths;
    return inspect(loader, inputs);
}

DeepSkyCalibrationPreflight::Report DeepSkyCalibrationPreflight::validate(
    const std::vector<FrameRecord>& frames) {
    Report report;
    auto addFinding = [&](Severity severity, Role role,
                          const QString& key, const QString& message,
                          const QString& path = QString()) {
        report.findings.push_back({severity, role, key, message, path});
    };

    QHash<int, int> counts;
    QSet<QString> paths;
    const FrameRecord* reference = nullptr;
    for (const FrameRecord& frame : frames) {
        ++counts[static_cast<int>(frame.role)];
        if (frame.role == Role::Light && frame.readable && !reference) {
            reference = &frame;
        }
        const QString identity = pathIdentity(frame.path);
        if (paths.contains(identity)) {
            addFinding(
                Severity::Error, frame.role, QStringLiteral("duplicate-path"),
                QString::fromUtf8(
                    "同一个文件被重复导入，或同时分配给多个素材角色。"),
                frame.path);
        } else {
            paths.insert(identity);
        }
        if (!frame.readable) {
            const bool masterRole = frame.role == Role::MasterDark ||
                frame.role == Role::MasterFlat ||
                frame.role == Role::MasterBias ||
                frame.role == Role::MasterDarkFlat;
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("unreadable-") +
                    QString::number(static_cast<int>(frame.role)),
                masterRole
                    ? QString::fromUtf8(
                          "%1 文件无效、已损坏或角色不匹配。")
                          .arg(roleName(frame.role))
                    : QString::fromUtf8(
                          "%1 中存在无法读取 RAW 头信息的文件。")
                          .arg(roleName(frame.role)),
                frame.path);
        }
    }
    report.lightCount = counts.value(static_cast<int>(Role::Light));
    report.darkCount = counts.value(static_cast<int>(Role::Dark));
    report.flatCount = counts.value(static_cast<int>(Role::Flat));
    report.biasCount = counts.value(static_cast<int>(Role::Bias));
    report.darkFlatCount = counts.value(static_cast<int>(Role::DarkFlat));
    report.masterDarkCount = counts.value(
        static_cast<int>(Role::MasterDark));
    report.masterFlatCount = counts.value(
        static_cast<int>(Role::MasterFlat));
    report.masterBiasCount = counts.value(
        static_cast<int>(Role::MasterBias));
    report.masterDarkFlatCount = counts.value(
        static_cast<int>(Role::MasterDarkFlat));

    auto checkRawCount = [&](Role role, int count, bool required) {
        if (!required && count == 0) return;
        constexpr int minimum = 3;
        if (count < minimum) {
            addFinding(
                Severity::Error, role,
                QStringLiteral("count-") +
                    QString::number(static_cast<int>(role)),
                QString::fromUtf8("%1 数量不足：当前 %2 张，至少需要 %3 张。")
                    .arg(roleName(role))
                    .arg(count)
                    .arg(minimum));
        } else if (count < 10) {
            addFinding(
                Severity::Warning, role,
                QStringLiteral("recommended-count-") +
                    QString::number(static_cast<int>(role)),
                QString::fromUtf8(
                    "%1 当前为 %2 张；可以处理，但建议 10–20 张以降低 Master 自身噪声。")
                    .arg(roleName(role))
                    .arg(count));
        }
    };
    if (report.lightCount < 2) {
        addFinding(
            Severity::Error, Role::Light, QStringLiteral("count-light"),
            QString::fromUtf8("Light 数量不足：当前 %1 张，至少需要 2 张。")
                .arg(report.lightCount));
    }

    auto checkMasterChoice = [&](Role rawRole, int rawCount,
                                 Role masterRole, int masterCount,
                                 bool required) {
        if (masterCount > 1) {
            addFinding(
                Severity::Error, masterRole,
                QStringLiteral("multiple-master-") +
                    QString::number(static_cast<int>(masterRole)),
                QString::fromUtf8("%1 只能导入 1 个，当前为 %2 个。")
                    .arg(roleName(masterRole))
                    .arg(masterCount));
        }
        if (rawCount > 0 && masterCount > 0) {
            addFinding(
                Severity::Error, masterRole,
                QStringLiteral("raw-and-master-") +
                    QString::number(static_cast<int>(rawRole)),
                QString::fromUtf8("%1 原片组和 %2 不能同时使用，请二选一。")
                    .arg(roleName(rawRole), roleName(masterRole)));
        }
        const bool rawRequired = required && masterCount == 0;
        checkRawCount(rawRole, rawCount, rawRequired);
    };

    checkMasterChoice(Role::Dark, report.darkCount, Role::MasterDark,
                      report.masterDarkCount, true);
    checkMasterChoice(Role::Flat, report.flatCount, Role::MasterFlat,
                      report.masterFlatCount, true);

    const bool buildsFlat = report.masterFlatCount == 0;
    const int biasSources = (report.biasCount > 0 ? 1 : 0) +
        (report.masterBiasCount > 0 ? 1 : 0);
    const int darkFlatSources = (report.darkFlatCount > 0 ? 1 : 0) +
        (report.masterDarkFlatCount > 0 ? 1 : 0);
    checkMasterChoice(Role::Bias, report.biasCount, Role::MasterBias,
                      report.masterBiasCount, false);
    checkMasterChoice(Role::DarkFlat, report.darkFlatCount,
                      Role::MasterDarkFlat, report.masterDarkFlatCount, false);
    const bool hasBiasCorrectedMasterDark = std::any_of(
        frames.begin(), frames.end(), [](const FrameRecord& frame) {
            return frame.role == Role::MasterDark && frame.readable &&
                !frame.masterDarkIncludesBiasPedestal;
        });
    if (buildsFlat && biasSources == 0 && darkFlatSources == 0) {
        addFinding(
            Severity::Error, Role::Flat,
            QStringLiteral("flat-offset-missing"),
            QString::fromUtf8(
                "生成 Master Flat 需要 Bias 或与 Flat 匹配的 Dark Flat，至少提供一种。"));
    }
    if (buildsFlat && biasSources > 0 && darkFlatSources > 0 &&
        !hasBiasCorrectedMasterDark) {
        addFinding(
            Severity::Error, Role::DarkFlat,
            QStringLiteral("flat-offset-ambiguous"),
            QString::fromUtf8(
                "Flat 校准同时提供了 Bias 与 Dark Flat；请选择其中一种偏移校准方式。"
                "只有已减 Bias 的 Master Dark 需要额外 Bias 时，两者才可并存。"));
    } else if (!buildsFlat &&
               (darkFlatSources > 0 ||
                (biasSources > 0 && !hasBiasCorrectedMasterDark))) {
        addFinding(
            Severity::Error, Role::MasterFlat,
            QStringLiteral("unused-flat-offset"),
            QString::fromUtf8(
                "Master Flat 已完成偏移校准和归一化，不应再导入 Bias 或 Dark Flat。"));
    }
    if (hasBiasCorrectedMasterDark && biasSources == 0) {
        addFinding(
            Severity::Error, Role::MasterDark,
            QStringLiteral("corrected-dark-needs-bias"),
            QString::fromUtf8(
                "已减 Bias 的 Master Dark 必须同时提供 Bias 或 Master Bias，才能完整校准 Light。"));
    }

    if (!reference) {
        addFinding(
            Severity::Error, Role::Light, QStringLiteral("no-reference"),
            QString::fromUtf8("没有可读取的 Light，无法建立校准基准。"));
        return report;
    }

    const auto& baseline = reference->metadata;
    report.referenceCamera = QString::fromStdString(baseline.cameraModel);
    report.referenceIso = baseline.iso;
    report.referenceExposure = baseline.exposureTime;
    report.referenceWidth = baseline.width;
    report.referenceHeight = baseline.height;
    if (report.referenceCamera.isEmpty() || baseline.iso <= 0 ||
        baseline.width <= 0 || baseline.height <= 0 ||
        !std::isfinite(baseline.exposureTime) ||
        baseline.exposureTime <= 0.0) {
        addFinding(
            Severity::Error, Role::Light, QStringLiteral("invalid-reference"),
            QString::fromUtf8(
                "基准 Light 缺少相机、ISO、尺寸或曝光信息，无法可靠匹配校准帧。"),
            reference->path);
        return report;
    }

    const double maximumBiasExposure =
        std::min(0.1, baseline.exposureTime * 0.01);
    const FrameRecord* flatReference = nullptr;
    for (const FrameRecord& frame : frames) {
        if (frame.role == Role::Flat && frame.readable) {
            flatReference = &frame;
            break;
        }
    }
    const bool usesDarkFlat = buildsFlat && darkFlatSources > 0;
    for (const FrameRecord& frame : frames) {
        if (!frame.readable) continue;
        const auto& metadata = frame.metadata;
        const QString role = roleName(frame.role);
        const QString camera = QString::fromStdString(metadata.cameraModel);
        if (camera != report.referenceCamera) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("camera-%1-%2")
                    .arg(static_cast<int>(frame.role))
                    .arg(camera),
                QString::fromUtf8("%1 相机不匹配：当前为 %2，基准 Light 为 %3。")
                    .arg(role, camera.isEmpty() ? QString::fromUtf8("未知") : camera,
                         report.referenceCamera),
                frame.path);
        }
        if (metadata.width != baseline.width ||
            metadata.height != baseline.height) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("geometry-%1-%2-%3")
                    .arg(static_cast<int>(frame.role))
                    .arg(metadata.width)
                    .arg(metadata.height),
                QString::fromUtf8("%1 尺寸不匹配：当前 %2×%3，基准 Light 为 %4×%5。")
                    .arg(role)
                    .arg(metadata.width)
                    .arg(metadata.height)
                    .arg(baseline.width)
                    .arg(baseline.height),
                frame.path);
        }
        if (metadata.iso != baseline.iso) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("iso-%1-%2")
                    .arg(static_cast<int>(frame.role))
                    .arg(metadata.iso),
                QString::fromUtf8("%1 ISO 不匹配：当前 ISO %2，基准 Light 为 ISO %3。")
                    .arg(role)
                    .arg(metadata.iso)
                    .arg(baseline.iso),
                frame.path);
        }
        if (!std::isfinite(metadata.exposureTime) ||
            metadata.exposureTime <= 0.0) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("missing-exposure-") +
                    QString::number(static_cast<int>(frame.role)),
                QString::fromUtf8("%1 缺少有效曝光时间。").arg(role),
                frame.path);
            continue;
        }
        if (frame.role == Role::Light &&
            !exposureMatches(metadata.exposureTime, baseline.exposureTime)) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("light-exposure-%1")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                QString::fromUtf8("Light 曝光不一致：当前 %1 秒，基准为 %2 秒。")
                    .arg(metadata.exposureTime, 0, 'g', 6)
                    .arg(baseline.exposureTime, 0, 'g', 6),
                frame.path);
        } else if ((frame.role == Role::Dark ||
                    frame.role == Role::MasterDark) &&
                   !exposureMatches(metadata.exposureTime,
                                    baseline.exposureTime)) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("dark-exposure-%1")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                QString::fromUtf8("%1 曝光不匹配：当前 %2 秒，Light 为 %3 秒。")
                    .arg(role)
                    .arg(metadata.exposureTime, 0, 'g', 6)
                    .arg(baseline.exposureTime, 0, 'g', 6),
                frame.path);
        } else if ((frame.role == Role::Bias ||
                    frame.role == Role::MasterBias) &&
                   metadata.exposureTime > maximumBiasExposure) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("bias-exposure-%1")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                QString::fromUtf8(
                    "%1 曝光过长：当前 %2 秒，本组必须不超过 %3 秒并使用相机最短曝光。")
                    .arg(role)
                    .arg(metadata.exposureTime, 0, 'g', 6)
                    .arg(maximumBiasExposure, 0, 'g', 6),
                frame.path);
        } else if ((frame.role == Role::DarkFlat ||
                    frame.role == Role::MasterDarkFlat) &&
                   flatReference &&
                   !exposureMatches(metadata.exposureTime,
                                    flatReference->metadata.exposureTime)) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("dark-flat-exposure-%1")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                QString::fromUtf8(
                    "%1 曝光不匹配：当前 %2 秒，Flat 为 %3 秒。")
                    .arg(role)
                    .arg(metadata.exposureTime, 0, 'g', 6)
                    .arg(flatReference->metadata.exposureTime, 0, 'g', 6),
                frame.path);
        } else if (frame.role == Role::Flat && usesDarkFlat &&
                   flatReference &&
                   !exposureMatches(metadata.exposureTime,
                                    flatReference->metadata.exposureTime)) {
            addFinding(
                Severity::Error, frame.role,
                QStringLiteral("flat-exposure-%1")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                QString::fromUtf8(
                    "Dark Flat 模式要求所有 Flat 曝光一致：当前 %1 秒，基准为 %2 秒。")
                    .arg(metadata.exposureTime, 0, 'g', 6)
                    .arg(flatReference->metadata.exposureTime, 0, 'g', 6),
                frame.path);
        } else if (frame.role == Role::Flat &&
                   metadata.exposureTime > 1.0 && !usesDarkFlat) {
            addFinding(
                Severity::Warning, frame.role,
                QStringLiteral("long-flat-%1")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                QString::fromUtf8(
                    "Flat 曝光为 %1 秒；当前选择 Bias 校准，长曝光平场建议改用匹配曝光的 Dark Flat。")
                    .arg(metadata.exposureTime, 0, 'g', 6),
                frame.path);
        }
    }
    return report;
}

bool DeepSkyCalibrationPreflight::exposureMatches(
    double first, double second) {
    if (!std::isfinite(first) || !std::isfinite(second) ||
        first <= 0.0 || second <= 0.0) {
        return false;
    }
    const double tolerance = std::max(
        0.01, std::max(first, second) * 0.01);
    return std::abs(first - second) <= tolerance;
}

QString DeepSkyCalibrationPreflight::roleName(Role role) {
    switch (role) {
    case Role::Light:
        return QStringLiteral("Light");
    case Role::Dark:
        return QStringLiteral("Dark");
    case Role::Flat:
        return QStringLiteral("Flat");
    case Role::Bias:
        return QStringLiteral("Bias");
    case Role::DarkFlat:
        return QStringLiteral("Dark Flat");
    case Role::MasterDark:
        return QStringLiteral("Master Dark");
    case Role::MasterFlat:
        return QStringLiteral("Master Flat");
    case Role::MasterBias:
        return QStringLiteral("Master Bias");
    case Role::MasterDarkFlat:
        return QStringLiteral("Master Dark Flat");
    }
    return QString::fromUtf8("未知素材");
}
