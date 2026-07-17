#include <Palette.h>

#include <NanoTest/NanoTest.h>

#include <map>

using namespace nano;
using namespace wim;

namespace
{
std::vector<GoItem> someTabs()
{
    return {{0, "GitHub", "https://github.com"},
            {1, "Hacker News", "https://news.ycombinator.com"}};
}

std::vector<Place> somePlaces()
{
    return {{"GitHub", "https://github.com"}, {"Linear", "https://linear.app"}};
}

LastUsedFn lastUsedFrom(std::map<std::string, std::int64_t> stamps)
{
    return [stamps = std::move(stamps)](const std::string& url)
    {
        auto it = stamps.find(url);
        return it != stamps.end() ? it->second : 0;
    };
}
} // namespace

auto tOpenPlacesAreNotDuplicated =
    test("Palette/a place that's already an open tab appears once") = []
{
    auto items = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));

    auto githubRows = 0;

    for (auto& item: items)
        if (item.url.find("github.com") != std::string::npos)
            githubRows++;

    check(githubRows == 1);
    check(items.size() == 3); // two tabs + linear
};

auto tTabsInheritBookmarkFlag =
    test("Palette/an open tab of a saved place shows as bookmarked") = []
{
    auto items = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));

    for (auto& item: items)
    {
        if (item.tabId == 0)
            check(item.bookmarked);

        if (item.tabId == 1)
            check(!item.bookmarked);
    }
};

auto tMruOrdersBrowseList = test("Palette/most recently used comes first") = []
{
    auto items = mergePaletteItems(
        someTabs(),
        somePlaces(),
        lastUsedFrom({{"linear.app", 300}, {"news.ycombinator.com", 200}}));

    check(items[0].url == "https://linear.app");
    check(items[1].url == "https://news.ycombinator.com");
};

auto tEmptyQueryKeepsBrowseList =
    test("Palette/an empty query returns the browse list untouched") = []
{
    auto all = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));
    auto ranked = rankPalette(all, {}, "");

    check(ranked.size() == all.size());
};

auto tQueryFiltersAndAppendsSearch =
    test("Palette/a query filters and pins the search row last") = []
{
    auto all = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));
    auto ranked = rankPalette(all, {}, "github");

    check(ranked.size() == 2);
    check(ranked.front().url == "https://github.com");
    check(ranked.back().isSearch);
};

auto tNoMatchLeavesOnlySearch =
    test("Palette/nothing matching leaves just the search row") = []
{
    auto all = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));
    auto ranked = rankPalette(all, {}, "zzzzqqqq");

    check(ranked.size() == 1);
    check(ranked.front().isSearch);
};

auto tHistoryJoinsTheHunt = test("Palette/history rows join query results") = []
{
    auto history = std::vector<HistoryEntry> {
        {"https://webkit.org/blog", "WebKit Blog", 100, 3}};

    auto all = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));
    auto ranked = rankPalette(all, history, "webkit");

    check(ranked.size() == 2);
    check(ranked.front().url == "https://webkit.org/blog");
    check(ranked.front().tabId == -1);
};

auto tHistoryNeverDuplicatesOpenRows =
    test("Palette/history of an open tab or place is skipped") = []
{
    auto history =
        std::vector<HistoryEntry> {{"https://github.com/", "GitHub", 100, 5}};

    auto all = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));
    auto ranked = rankPalette(all, history, "github");

    check(ranked.size() == 2); // the open tab + the search row
    check(ranked.front().tabId == 0);
};

auto tSearchRowStyle = test("Palette/the search row mirrors the query kind") = []
{
    check(searchRow("github.com").title == "Open github.com");
    check(searchRow("hello world").title.find("Search Google") != std::string::npos);
};

auto tURLQueryWithoutExactMatchOpensTheURL =
    test("Palette/a URL query beats fuzzy hits when nothing matches exactly") = []
{
    auto all =
        std::vector<GoItem> {{0, "Inbox - Tamber Mail", "https://mail.google.com/"}};
    auto ranked = rankPalette(all, {}, "google.com");

    check(ranked.front().isSearch);
    check(ranked.front().url == "https://google.com");
    check(ranked.back().url == "https://mail.google.com/");
};

auto tURLQueryPrefersTheExactItem =
    test("Palette/a URL query picks the exactly-matching item first") = []
{
    auto all = mergePaletteItems(someTabs(), somePlaces(), lastUsedFrom({}));
    auto ranked = rankPalette(all, {}, "github.com");

    check(ranked.front().tabId == 0);
    check(ranked.back().isSearch);
};
