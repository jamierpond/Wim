#include "History.h"
#include "Url.h"

#include <algorithm>
#include <utility>

namespace wim
{
HistoryStore::HistoryStore(const emberstore::Database& db)
    : entries(db.collection<HistoryEntry>("history"))
{
    prune();
}

void HistoryStore::recordVisit(const std::string& url,
                               const std::string& title,
                               std::int64_t atMs)
{
    auto ref = entries.doc(normalizeURL(url));
    auto entry = ref.get().value_or(HistoryEntry {});

    entry.url = url;

    if (!title.empty())
        entry.title = title;

    entry.lastVisit = atMs;
    entry.visits++;
    ref.set(entry);
}

void HistoryStore::updateTitle(const std::string& url, const std::string& title)
{
    if (title.empty())
        return;

    auto ref = entries.doc(normalizeURL(url));

    if (auto entry = ref.get())
    {
        entry->title = title;
        ref.set(*entry);
    }
}

void HistoryStore::forget(const std::string& url)
{
    entries.remove(normalizeURL(url));
}

std::vector<HistoryEntry> HistoryStore::all()
{
    return entries.values();
}

void HistoryStore::prune()
{
    if (entries.size() <= maxEntries)
        return;

    auto aged = std::vector<std::pair<std::int64_t, std::string>> {};

    for (auto& id: entries.ids())
    {
        auto entry = entries.get(id);
        aged.push_back({entry ? entry->lastVisit : 0, id});
    }

    std::sort(aged.begin(), aged.end());

    auto excess = aged.size() - maxEntries;

    for (std::size_t i = 0; i < excess; ++i)
        entries.remove(aged[i].second);
}
} // namespace wim
