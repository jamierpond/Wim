#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wim
{
struct HistoryEntry
{
    std::string url;
    std::string title;
    std::int64_t lastVisit = 0;
    std::int64_t visits = 0;

    MIRO_REFLECT(url, title, lastVisit, visits)
};

// The global visit log, keyed by normalized URL; it feeds the palette's
// suggestions when a query is typed. Like MruStore it's written on every
// navigation, so give it an Atomic database.
struct HistoryStore
{
    explicit HistoryStore(const emberstore::Database& db);

    void recordVisit(const std::string& url,
                     const std::string& title,
                     std::int64_t atMs);

    // Titles usually arrive after the navigation that recorded the visit.
    void updateTitle(const std::string& url, const std::string& title);

    // Visits are recorded when a navigation starts; a failed one wasn't a
    // real visit, so it gets dropped again.
    void forget(const std::string& url);

    std::vector<HistoryEntry> all();

    // Drops the oldest entries once the log outgrows maxEntries.
    void prune();

    static constexpr std::size_t maxEntries = 2000;

    emberstore::Collection<HistoryEntry> entries;
};
} // namespace wim
