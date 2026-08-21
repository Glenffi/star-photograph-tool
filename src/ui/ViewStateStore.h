#pragma once

#include <QHash>
#include <QString>
#include <QtGlobal>

#include <optional>

// 保存预览内容在当前会话中的画布状态。内容键由调用方负责生成：
// 素材使用规范路径，结果和快速预览分别使用 result:/quick: 前缀。
class ViewStateStore final {
public:
    enum class ZoomMode {
        Fit,
        Manual
    };

    enum class ComparisonMode {
        Before = 0,
        After = 1,
        Split = 2
    };

    struct ViewState {
        ZoomMode zoomMode = ZoomMode::Fit;
        double zoom = 1.0;
        int horizontalScroll = 0;
        int verticalScroll = 0;
        ComparisonMode comparisonMode = ComparisonMode::After;
        bool maskOverlayVisible = false;
    };

    static constexpr qsizetype DefaultCapacity = 50;

    // 非法容量会被限制到 [1, 10000]，默认保存最近使用的 50 个内容。
    explicit ViewStateStore(qsizetype capacity = DefaultCapacity);

    // 空键不会被保存。状态会先经过校验，再写入或覆盖原记录。
    void save(const QString& contentKey, const ViewState& state);

    // 命中记录时会刷新其 LRU 顺序；未命中返回 std::nullopt。
    [[nodiscard]] std::optional<ViewState> stateFor(const QString& contentKey);
    [[nodiscard]] bool contains(const QString& contentKey) const;

    // remove 清除单个内容，clear 清除本次会话的全部视图状态。
    bool remove(const QString& contentKey);
    void clear();

    [[nodiscard]] qsizetype size() const;
    [[nodiscard]] qsizetype capacity() const;
    [[nodiscard]] bool isEmpty() const;

private:
    struct Entry {
        ViewState state;
        quint64 lastAccess = 0;
    };

    static QString normalizedKey(const QString& contentKey);
    static ViewState sanitized(const ViewState& state);
    void touch(Entry& entry);
    void evictLeastRecentlyUsed();

    QHash<QString, Entry> m_entries;
    qsizetype m_capacity = DefaultCapacity;
    quint64 m_accessCounter = 0;
};
