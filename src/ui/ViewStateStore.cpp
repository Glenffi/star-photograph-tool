#include "ViewStateStore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr qsizetype kMaximumCapacity = 10000;
constexpr double kMinimumZoom = 0.01;
constexpr double kMaximumZoom = 64.0;

} // namespace

ViewStateStore::ViewStateStore(qsizetype capacity)
    : m_capacity(std::clamp(capacity, qsizetype{1}, kMaximumCapacity))
{
    m_entries.reserve(m_capacity);
}

void ViewStateStore::save(const QString& contentKey, const ViewState& state)
{
    const QString key = normalizedKey(contentKey);
    if (key.isEmpty()) {
        return;
    }

    auto existing = m_entries.find(key);
    if (existing != m_entries.end()) {
        existing->state = sanitized(state);
        touch(existing.value());
        return;
    }

    if (m_entries.size() >= m_capacity) {
        evictLeastRecentlyUsed();
    }

    Entry entry;
    entry.state = sanitized(state);
    touch(entry);
    m_entries.insert(key, entry);
}

std::optional<ViewStateStore::ViewState> ViewStateStore::stateFor(const QString& contentKey)
{
    const QString key = normalizedKey(contentKey);
    if (key.isEmpty()) {
        return std::nullopt;
    }

    auto entry = m_entries.find(key);
    if (entry == m_entries.end()) {
        return std::nullopt;
    }

    touch(entry.value());
    return entry->state;
}

bool ViewStateStore::contains(const QString& contentKey) const
{
    const QString key = normalizedKey(contentKey);
    return !key.isEmpty() && m_entries.contains(key);
}

bool ViewStateStore::remove(const QString& contentKey)
{
    const QString key = normalizedKey(contentKey);
    return !key.isEmpty() && m_entries.remove(key) > 0;
}

void ViewStateStore::clear()
{
    m_entries.clear();
    m_accessCounter = 0;
}

qsizetype ViewStateStore::size() const
{
    return m_entries.size();
}

qsizetype ViewStateStore::capacity() const
{
    return m_capacity;
}

bool ViewStateStore::isEmpty() const
{
    return m_entries.isEmpty();
}

QString ViewStateStore::normalizedKey(const QString& contentKey)
{
    // 路径首尾的空格可能是合法文件名，只将纯空白键视为无效。
    return contentKey.trimmed().isEmpty() ? QString{} : contentKey;
}

ViewStateStore::ViewState ViewStateStore::sanitized(const ViewState& state)
{
    ViewState result = state;

    switch (result.zoomMode) {
    case ZoomMode::Fit:
    case ZoomMode::Manual:
        break;
    default:
        result.zoomMode = ZoomMode::Fit;
        break;
    }

    if (!std::isfinite(result.zoom)) {
        result.zoom = 1.0;
    }
    result.zoom = std::clamp(result.zoom, kMinimumZoom, kMaximumZoom);
    result.horizontalScroll = std::max(0, result.horizontalScroll);
    result.verticalScroll = std::max(0, result.verticalScroll);

    switch (result.comparisonMode) {
    case ComparisonMode::Before:
    case ComparisonMode::After:
    case ComparisonMode::Split:
        break;
    default:
        result.comparisonMode = ComparisonMode::After;
        break;
    }

    return result;
}

void ViewStateStore::touch(Entry& entry)
{
    // 极端长会话下重排序号，避免无符号计数器回绕破坏 LRU 次序。
    if (m_accessCounter == std::numeric_limits<quint64>::max()) {
        std::vector<Entry*> orderedEntries;
        orderedEntries.reserve(static_cast<std::size_t>(m_entries.size()));
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            orderedEntries.push_back(&it.value());
        }

        std::sort(orderedEntries.begin(), orderedEntries.end(),
                  [](const Entry* left, const Entry* right) {
                      return left->lastAccess < right->lastAccess;
                  });

        m_accessCounter = 0;
        for (Entry* storedEntry : orderedEntries) {
            storedEntry->lastAccess = ++m_accessCounter;
        }
    }

    entry.lastAccess = ++m_accessCounter;
}

void ViewStateStore::evictLeastRecentlyUsed()
{
    if (m_entries.isEmpty()) {
        return;
    }

    auto oldest = m_entries.begin();
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->lastAccess < oldest->lastAccess) {
            oldest = it;
        }
    }
    m_entries.erase(oldest);
}
