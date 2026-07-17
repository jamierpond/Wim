#pragma once

#include "History.h"
#include "Places.h"

#include <Types.h>

#include <functional>
#include <string>
#include <vector>

namespace wim
{
using LastUsedFn = std::function<std::int64_t(const std::string& normalizedUrl)>;

// The palette's base list when browsing (no query): open tabs plus places
// that aren't already open, most-recently-used first. `tabs` come in tab
// order with tabId/title/url set; their bookmarked flag is derived from
// `places` here.
std::vector<GoItem> mergePaletteItems(std::vector<GoItem> tabs,
                                      const std::vector<Place>& places,
                                      const LastUsedFn& lastUsed);

// The list for the current query. Empty query: `all` unchanged. Otherwise
// fuzzy-rank `all` plus history rows (skipping URLs already present), keep
// real hits only, and pin the search row at the bottom so search is always
// one arrow-down away -- and the default when nothing matches.
std::vector<GoItem> rankPalette(const std::vector<GoItem>& all,
                                const std::vector<HistoryEntry>& history,
                                const std::string& query);

GoItem searchRow(const std::string& query);
} // namespace wim
