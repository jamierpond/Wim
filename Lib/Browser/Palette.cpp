#include "Palette.h"
#include "FuzzyMatch.h"
#include "Url.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace wim
{
namespace
{
bool containsURL(const std::vector<GoItem>& items, const std::string& normalizedUrl)
{
    return std::any_of(items.begin(),
                       items.end(),
                       [&](const GoItem& item)
                       { return normalizeURL(item.url) == normalizedUrl; });
}
} // namespace

std::vector<GoItem> mergePaletteItems(std::vector<GoItem> tabs,
                                      const std::vector<Place>& places,
                                      const LastUsedFn& lastUsed)
{
    auto placeUrls = std::unordered_set<std::string> {};

    for (auto& place: places)
        placeUrls.insert(normalizeURL(place.url));

    auto items = std::move(tabs);

    for (auto& item: items)
        item.bookmarked = placeUrls.contains(normalizeURL(item.url));

    for (auto& place: places)
        if (!containsURL(items, normalizeURL(place.url)))
            items.push_back({-1, place.title, place.url, true});

    // MRU is the palette's base order: whatever you used last is on top. The
    // fuzzy re-rank in rankPalette is stable, so equal scores keep this
    // order too.
    std::stable_sort(
        items.begin(),
        items.end(),
        [&](const GoItem& a, const GoItem& b)
        { return lastUsed(normalizeURL(a.url)) > lastUsed(normalizeURL(b.url)); });

    return items;
}

std::vector<GoItem> rankPalette(const std::vector<GoItem>& all,
                                const std::vector<HistoryEntry>& history,
                                const std::string& query)
{
    if (query.empty())
        return all;

    auto candidates = all;

    for (auto& entry: history)
        if (!containsURL(candidates, normalizeURL(entry.url)))
            candidates.push_back(
                {-1, entry.title.empty() ? entry.url : entry.title, entry.url});

    auto scored = std::vector<std::pair<int, const GoItem*>> {};

    // score > 0 is the "really hit" bar: full-subsequence matches spread
    // thin across a long title/URL go negative on gap penalties and
    // shouldn't outrank the search row.
    for (auto& item: candidates)
    {
        auto score = fuzzyScore(query, item.title + " " + item.url);

        if (score && *score > 0)
            scored.push_back({*score, &item});
    }

    std::stable_sort(scored.begin(),
                     scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    auto ranked = std::vector<GoItem> {};

    // Dead pages (failed navigations) must not outrank search -- they sink
    // below the search row, appended at the very end.
    for (auto& [score, item]: scored)
        if (!item->failed)
            ranked.push_back(*item);

    auto dead = std::vector<GoItem> {};

    for (auto& [score, item]: scored)
        if (item->failed)
            dead.push_back(*item);

    auto finish = [&](std::vector<GoItem> list)
    {
        list.insert(list.end(), dead.begin(), dead.end());
        return list;
    };

    auto row = searchRow(query);

    // A URL-shaped query means "take me to this URL": an EXACT normalized
    // match still wins (typing github.com picks the GitHub tab), but a mere
    // fuzzy hit must not steal Enter from it -- google.com opens google.com,
    // not the mail.google.com tab.
    if (looksLikeURL(query))
    {
        auto target = normalizeURL(query);
        auto exact = std::find_if(ranked.begin(),
                                  ranked.end(),
                                  [&](const GoItem& item)
                                  { return normalizeURL(item.url) == target; });

        if (exact == ranked.end())
        {
            ranked.insert(ranked.begin(), std::move(row));
            return ranked;
        }

        std::rotate(ranked.begin(), exact, exact + 1);
    }

    ranked.push_back(std::move(row));
    return ranked;
}

GoItem searchRow(const std::string& query)
{
    auto row = GoItem {};
    row.title =
        looksLikeURL(query) ? "Open " + query : "Search Google for “" + query + "”";
    row.url = navigationURL(query);
    row.isSearch = true;
    return row;
}
} // namespace wim
