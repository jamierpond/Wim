#pragma once

#include <Miro/Bridge.h>
#include <Miro/Reflect.h>

#include <cstdint>
#include <functional>
#include <string>

// One row in the Go palette: an open tab (tabId >= 0), a saved place
// (tabId < 0), or the pinned "Search Google for ..." action row (isSearch),
// which is appended whenever a query is typed so search is always one
// arrow-down away -- and the default when nothing matches.
struct GoItem
{
    std::int64_t tabId = -1;
    std::string title;
    std::string url;
    bool bookmarked = false;
    bool isSearch = false;

    MIRO_REFLECT(tabId, title, url, bookmarked, isSearch)
};

// The palette's whole render model. Filtering, ranking and selection are
// native; the web UI just draws this and forwards input. `generation` bumps
// each time the palette opens so the UI can reset its input field.
struct GoResults
{
    std::vector<GoItem> items;
    std::int64_t selected = 0;
    std::int64_t generation = 0;

    // The active tab's URL. The palette pre-fills its input with this
    // (selected, not filtering), so ⌘L → ⌘A → ⌘C copies the current URL.
    std::string currentUrl;

    MIRO_REFLECT(items, selected, generation, currentUrl)
};

struct GoQuery
{
    std::string text;

    MIRO_REFLECT(text)
};

struct GoDelta
{
    std::int64_t delta = 0;

    MIRO_REFLECT(delta)
};

namespace Api
{

// The palette's wire surface. The web UI is a dumb renderer: it draws
// `results` and forwards keystrokes as commands; all matching, selection and
// preview logic runs natively. Behaviour is injected by BrowserView through
// the std::function members -- the codegen executable default-constructs this
// class, so it must stay header-only and default-constructible.
class WimApi
{
public:
    void reflect(Miro::ApiReflector& r)
    {
        using T = WimApi;

        r.commands<&T::getResults,
                   &T::setQuery,
                   &T::moveSelection,
                   &T::activate,
                   &T::choose,
                   &T::cancel,
                   &T::closeItem,
                   &T::toggleBookmark>();
        r.events<&T::results>();
    }

    GoResults getResults() const { return results.snapshot(); }

    void setQuery(const GoQuery& query) { onSetQuery(query.text); }
    void moveSelection(const GoDelta& delta) { onMoveSelection((int) delta.delta); }
    void activate() { onActivate(); }
    void choose(const GoItem& item) { onChoose(item); }
    void cancel() { onCancel(); }
    void closeItem(const GoItem& item) { onCloseItem(item); }
    void toggleBookmark(const GoItem& item) { onToggleBookmark(item); }

    Miro::Event<GoResults> results;

    std::function<void(const std::string&)> onSetQuery = [](auto&) {};
    std::function<void(int)> onMoveSelection = [](int) {};
    std::function<void()> onActivate = [] {};
    std::function<void(const GoItem&)> onChoose = [](auto&) {};
    std::function<void()> onCancel = [] {};
    std::function<void(const GoItem&)> onCloseItem = [](auto&) {};
    std::function<void(const GoItem&)> onToggleBookmark = [](auto&) {};
};

} // namespace Api
