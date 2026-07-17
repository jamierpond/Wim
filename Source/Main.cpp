#include "FuzzyMatch.h"
#include "Types.h"

#include <eacp/WebView/WebView.h>
#include <emberstore/AppDatabase.h>
#include <emberstore/Emberstore.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

using namespace eacp;
using namespace Graphics;

// Vimium-style bindings, injected into every page at document start. Add new
// bindings to the `bindings` map below -- keys are keypress sequences ('gg'
// works, as long as no binding is a strict prefix of another), values run when
// the sequence completes. `post(command)` sends a command string to the native
// side, handled in BrowserView::handleCommand.
static constexpr auto vimiumScript = R"JS(
(() => {
    if (window.__wimInstalled)
        return;
    window.__wimInstalled = true;

    const post = command =>
        window.webkit?.messageHandlers?.wim?.postMessage(command);

    const step = 60;
    const half = () => window.innerHeight / 2;

    const bindings = {
        'j': () => scrollBy(0, step),
        'k': () => scrollBy(0, -step),
        'h': () => scrollBy(-step, 0),
        'l': () => scrollBy(step, 0),
        'd': () => scrollBy(0, half()),
        'u': () => scrollBy(0, -half()),
        'gg': () => scrollTo(0, 0),
        'G': () => scrollTo(0, document.documentElement.scrollHeight),
        'H': () => history.back(),
        'L': () => history.forward(),
        'r': () => location.reload(),
        'o': () => post('openPalette'),
        't': () => post('openPalette'),
        'b': () => post('toggleBookmark'),
        'x': () => post('closeTab'),
        'J': () => post('prevTab'),
        'K': () => post('nextTab'),
        'f': () => enterHintMode(),
    };

    // --- Link hints ('f') ------------------------------------------------

    const hintChars = 'asdfghjkl';
    let hintState = null;

    function hintLabels(count) {
        let length = 1;
        while (Math.pow(hintChars.length, length) < count)
            length++;

        return Array.from({length: count}, (_, i) => {
            let n = i, label = '';
            for (let d = 0; d < length; d++) {
                label = hintChars[n % hintChars.length] + label;
                n = Math.floor(n / hintChars.length);
            }
            return label;
        });
    }

    function clickables() {
        const selector = 'a[href], button, input, select, textarea, summary, '
            + '[onclick], [role="button"], [role="link"], [tabindex]';
        return [...document.querySelectorAll(selector)].filter(el => {
            const r = el.getBoundingClientRect();
            return r.width > 0 && r.height > 0
                && r.bottom > 0 && r.top < innerHeight
                && r.right > 0 && r.left < innerWidth
                && getComputedStyle(el).visibility !== 'hidden';
        });
    }

    function enterHintMode() {
        const els = clickables();
        if (!els.length)
            return;

        const labels = hintLabels(els.length);
        const overlay = document.createElement('div');
        overlay.style.cssText =
            'position:fixed;inset:0;z-index:2147483647;pointer-events:none;';

        els.forEach((el, i) => {
            const r = el.getBoundingClientRect();
            const tag = document.createElement('span');
            tag.textContent = labels[i].toUpperCase();
            tag.dataset.label = labels[i];
            tag.style.cssText =
                `position:fixed;left:${Math.max(0, r.left)}px;`
                + `top:${Math.max(0, r.top)}px;`
                + 'background:#ffd76e;color:#302505;'
                + 'font:bold 11px/1.3 -apple-system,sans-serif;'
                + 'padding:1px 3px;border:1px solid #c38a22;border-radius:3px;'
                + 'box-shadow:0 1px 2px rgba(0,0,0,.4);';
            overlay.appendChild(tag);
        });

        document.documentElement.appendChild(overlay);
        hintState = {els, labels, typed: '', overlay};
    }

    function exitHintMode() {
        hintState?.overlay.remove();
        hintState = null;
    }

    function activate(el) {
        el.focus();
        const editable = el.matches(
            'textarea, select, [contenteditable], input:not([type=button])'
            + ':not([type=submit]):not([type=checkbox]):not([type=radio])');
        if (!editable)
            el.click();
    }

    function handleHintKey(event) {
        if (event.key === 'Escape' || !hintChars.includes(event.key)) {
            exitHintMode();
            return;
        }

        hintState.typed += event.key;
        const matches = hintState.labels.filter(
            label => label.startsWith(hintState.typed));

        if (matches.length === 1 && matches[0] === hintState.typed) {
            const el = hintState.els[hintState.labels.indexOf(matches[0])];
            exitHintMode();
            activate(el);
            return;
        }

        if (!matches.length) {
            exitHintMode();
            return;
        }

        [...hintState.overlay.children].forEach(tag => {
            tag.style.display =
                tag.dataset.label.startsWith(hintState.typed) ? '' : 'none';
        });
    }

    // --- Key dispatch -----------------------------------------------------

    let pending = '';
    let pendingTimer = 0;

    function isEditable(el) {
        return el && (el.isContentEditable
            || ['INPUT', 'TEXTAREA', 'SELECT'].includes(el.tagName));
    }

    addEventListener('keydown', event => {
        if (event.metaKey && !event.ctrlKey && !event.altKey
            && event.key === 'l') {
            event.preventDefault();
            event.stopImmediatePropagation();
            post('openPalette');
            return;
        }

        if (event.metaKey || event.ctrlKey || event.altKey)
            return;

        if (hintState) {
            event.preventDefault();
            event.stopImmediatePropagation();
            handleHintKey(event);
            return;
        }

        if (isEditable(document.activeElement)) {
            if (event.key === 'Escape')
                document.activeElement.blur();
            return;
        }

        if (event.key.length !== 1) {
            pending = '';
            return;
        }

        const candidate = pending + event.key;
        clearTimeout(pendingTimer);

        if (bindings[candidate]) {
            pending = '';
            event.preventDefault();
            event.stopImmediatePropagation();
            bindings[candidate]();
            return;
        }

        const isPrefix = Object.keys(bindings)
            .some(seq => seq.startsWith(candidate));

        if (isPrefix) {
            pending = candidate;
            event.preventDefault();
            event.stopImmediatePropagation();
            pendingTimer = setTimeout(() => { pending = ''; }, 800);
            return;
        }

        pending = '';
    }, true);
})();
)JS";

struct Tab
{
    Tab(std::int64_t idToUse, WebView::Options options)
        : id(idToUse)
        , webView(new WebView(std::move(options)))
    {
    }

    Tab(std::int64_t idToUse, EA::OwningPointer<WebView> adopted)
        : id(idToUse)
        , webView(std::move(adopted))
    {
    }

    WebView& view() { return *webView; }

    std::int64_t id;
    EA::OwningPointer<WebView> webView;
    std::string title;
    std::string url;
};

struct Place
{
    std::string title;
    std::string url;

    MIRO_REFLECT(title, url)
};

struct PlacesFile
{
    std::vector<Place> places;

    MIRO_REFLECT(places)
};

// The user's saved places, an emberstore collection keyed by normalized URL
// (<app data>/pond/Wim/places.json). First run imports the legacy
// ~/.wim/places.json if present, else seeds a starter set.
struct PlacesStore
{
    explicit PlacesStore(const emberstore::Database& db)
        : collection(db.collection<Place>("places"))
    {
        if (!collection.empty())
            return;

        if (!importLegacy())
            seedDefaults();
    }

    bool contains(const std::string& normalizedUrl)
    {
        return collection.contains(normalizedUrl);
    }

    void toggle(const std::string& title, const std::string& url)
    {
        auto ref = collection.doc(normalize(url));

        if (ref.exists())
        {
            ref.remove();
            return;
        }

        ref.set({title.empty() ? url : title, url});
    }

    std::vector<Place> all() { return collection.values(); }

    static std::string normalize(std::string url)
    {
        for (auto* prefix: {"https://", "http://"})
        {
            auto view = std::string_view(prefix);

            if (url.starts_with(view))
            {
                url = url.substr(view.size());
                break;
            }
        }

        if (url.starts_with("www."))
            url = url.substr(4);

        while (!url.empty() && url.back() == '/')
            url.pop_back();

        return url;
    }

    bool importLegacy()
    {
        auto* home = std::getenv("HOME");
        auto legacyPath = std::filesystem::path(home != nullptr ? home : ".")
                          / ".wim" / "places.json";
        auto file = std::ifstream(legacyPath);

        if (!file)
            return false;

        auto text = std::string(std::istreambuf_iterator<char>(file), {});
        auto legacy = PlacesFile {};
        Miro::fromJSONString(legacy, text);

        for (auto& place: legacy.places)
            collection.doc(normalize(place.url)).set(place);

        return !collection.empty();
    }

    void seedDefaults()
    {
        auto seeds = std::vector<Place> {{"GitHub", "https://github.com"},
                                         {"Gmail", "https://mail.google.com"},
                                         {"Linear", "https://linear.app"},
                                         {"Wikipedia", "https://www.wikipedia.org"}};

        for (auto& place: seeds)
            collection.doc(normalize(place.url)).set(place);
    }

    emberstore::Collection<Place> collection;
};

struct Stamp
{
    std::int64_t at = 0;

    MIRO_REFLECT(at)
};

// Recency stamps for palette ordering, keyed by normalized URL. Written on
// every real use (palette pick, tab cycle, active-tab navigation), so Atomic
// not Durable -- losing the last stamps to a power cut is harmless.
struct MruStore
{
    explicit MruStore(const emberstore::Database& db)
        : stamps(db.collection<Stamp>("mru"))
    {
        prune();
    }

    void touch(const std::string& normalizedUrl)
    {
        stamps.doc(normalizedUrl).set({nowMs()});
    }

    std::int64_t lastUsed(const std::string& normalizedUrl)
    {
        if (auto stamp = stamps.get(normalizedUrl))
            return stamp->at;

        return 0;
    }

    void prune()
    {
        if (stamps.size() <= maxEntries)
            return;

        auto aged = std::vector<std::pair<std::int64_t, std::string>> {};

        for (auto& id: stamps.ids())
            aged.push_back({lastUsed(id), id});

        std::sort(aged.begin(), aged.end());

        auto excess = aged.size() - maxEntries;

        for (std::size_t i = 0; i < excess; ++i)
            stamps.remove(aged[i].second);
    }

    static std::int64_t nowMs()
    {
        using namespace std::chrono;
        return (std::int64_t) duration_cast<milliseconds>(
                   system_clock::now().time_since_epoch())
            .count();
    }

    static constexpr std::size_t maxEntries = 500;

    emberstore::Collection<Stamp> stamps;
};

struct BrowserView final : View
{
    BrowserView()
    {
        api.onSetQuery = [this](const std::string& text) { setPaletteQuery(text); };
        api.onMoveSelection = [this](int delta) { movePaletteSelection(delta); };
        api.onActivate = [this]
        { Threads::callAsync([this] { activatePalette(); }); };
        api.onChoose = [this](const GoItem& item)
        {
            auto chosen = item;
            Threads::callAsync([this, chosen] { chooseItem(chosen); });
        };
        api.onCancel = [this] { Threads::callAsync([this] { cancelPalette(); }); };
        api.onCloseItem = [this](const GoItem& item)
        {
            auto id = item.tabId;
            Threads::callAsync([this, id] { closeTabById(id); });
        };
        api.onToggleBookmark = [this](const GoItem& item)
        { toggleBookmark(item.title, item.url); };

        transport.getBridge().use(api);

        addChildren({content});
        openTab(homePage);
    }

    // Tab webviews live inside `content`; the palette is a permanently later
    // sibling of it. Switching tabs only touches content's children, so the
    // palette's key focus survives live previews.
    void resized() override
    {
        auto bounds = getLocalBounds();
        content.setBounds(bounds);

        if (activeTab != nullptr)
            activeTab->view().setBounds(content.getLocalBounds());

        if (paletteOpen)
            paletteWeb.setBounds(getLocalBounds());
    }

    // --- Tabs -----------------------------------------------------------------

    Tab& createTab(const std::string& url)
    {
        auto& tab = tabs.createNew(nextTabId++, getWebViewOptions());
        tab.view().addUserScript(vimiumScript);
        tab.view().addScriptMessageHandler(
            "wim", [this](const std::string& command) { handleCommand(command); });

        wireTab(tab);
        tab.view().loadURL(url);
        return tab;
    }

    void openTab(const std::string& url)
    {
        switchTo(createTab(url));
        syncPalette();
    }

    // A page-initiated popup (target=_blank, window.open) adopted as a tab. It
    // shares the opener's web-process configuration, so the vimium user script
    // and the "wim" message handler are already installed -- re-adding them
    // would collide on the shared user-content controller.
    void adoptPopup(EA::OwningPointer<WebView> popup)
    {
        auto& tab = tabs.createNew(nextTabId++, std::move(popup));
        wireTab(tab);
        switchTo(tab);
        syncPalette();
    }

    void wireTab(Tab& tab)
    {
        auto* t = &tab;
        auto& view = tab.view();

        view.onNavigationStarted = [this, t](const std::string& url)
        {
            t->url = url;

            if (t == activeTab)
                mru.touch(PlacesStore::normalize(url));

            syncPalette();
        };

        view.onTitleChanged = [this, t](const std::string& title)
        {
            t->title = title;
            syncPalette();
        };

        view.onNavigationFinished = [this, t](const std::string&)
        {
            if (t == activeTab && !paletteOpen)
                t->view().focusContent();
        };

        view.onNewWindowRequested =
            [this](EA::OwningPointer<WebView> popup, const std::string&)
        {
            adoptPopup(std::move(popup));
            return true;
        };

        view.onClose = [this, t] { Threads::callAsync([this, t] { closeTab(t); }); };
    }

    void switchTo(Tab& tab)
    {
        if (activeTab == &tab)
            return;

        if (activeTab != nullptr)
            activeTab->view().removeFromParent();

        activeTab = &tab;
        content.addSubview(tab.view());
        tab.view().setBounds(content.getLocalBounds());

        if (!paletteOpen)
            tab.view().focusContent();
    }

    void closeTab(Tab* tab)
    {
        auto index = tabs.getIndexOfItem(tab);

        if (index < 0)
            return;

        auto wasActive = tab == activeTab;

        if (tab == originTab)
            originTab = nullptr;

        if (wasActive)
        {
            tab->view().removeFromParent();
            activeTab = nullptr;
        }

        tabs.removeAt(index);

        if (tabs.empty())
        {
            openTab(homePage);
            return;
        }

        if (wasActive)
            switchTo(*tabs[std::min(index, tabs.size() - 1)]);

        syncPalette();
    }

    void closeTabById(std::int64_t id)
    {
        if (auto* tab = findTab(id))
            closeTab(tab);
    }

    Tab* findTab(std::int64_t id)
    {
        for (auto& tab: tabs)
            if (tab->id == id)
                return tab.get();

        return nullptr;
    }

    bool hasTabWithURL(const std::string& normalizedUrl)
    {
        for (auto& tab: tabs)
            if (PlacesStore::normalize(tab->url) == normalizedUrl)
                return true;

        return false;
    }

    void cycleTab(int direction)
    {
        auto index = tabs.getIndexOfItem(activeTab);

        if (index < 0 || tabs.size() < 2)
            return;

        auto next = (index + direction + tabs.size()) % tabs.size();
        mru.touch(PlacesStore::normalize(tabs[next]->url));
        switchTo(*tabs[next]);
    }

    // --- Go palette -------------------------------------------------------------

    void showPalette()
    {
        if (paletteOpen)
            return;

        paletteOpen = true;
        originTab = activeTab;
        paletteQuery.clear();
        paletteSelected = 0;
        paletteTouched = false;
        generation++;
        syncPalette();

        addSubview(paletteWeb);
        paletteWeb.setBounds(getLocalBounds());
        paletteWeb.focusContent();
    }

    void hidePalette()
    {
        if (!paletteOpen)
            return;

        paletteOpen = false;
        originTab = nullptr;
        previewDirty = false;
        paletteWeb.removeFromParent();

        if (activeTab != nullptr)
            activeTab->view().focusContent();
    }

    void setPaletteQuery(const std::string& text)
    {
        if (!paletteOpen)
            return;

        paletteQuery = text;
        paletteSelected = 0;
        paletteTouched = true;
        refilterPalette();
        publishResults();
        previewDirty = true;
    }

    void movePaletteSelection(int delta)
    {
        if (!paletteOpen || paletteFiltered.empty())
            return;

        paletteSelected =
            std::clamp(paletteSelected + delta, 0, (int) paletteFiltered.size() - 1);
        paletteTouched = true;
        publishResults();
        previewDirty = false;
        previewSelected();
    }

    // Enter. A touched tab selection was already previewed live; a place or
    // the search row loads now. An untouched palette dismisses without moving.
    void activatePalette()
    {
        if (!paletteOpen)
            return;

        if (paletteTouched && !paletteFiltered.empty())
        {
            openItem(paletteFiltered[(size_t) paletteSelected]);
            return;
        }

        hidePalette();
    }

    void chooseItem(const GoItem& item)
    {
        if (paletteOpen)
            openItem(item);
    }

    void previewSelected()
    {
        if (!paletteOpen || !paletteTouched)
            return;

        if (paletteSelected < (int) paletteFiltered.size())
            previewItem(paletteFiltered[(size_t) paletteSelected]);
    }

    // Typing debounce: setPaletteQuery only marks the preview dirty; this 8 Hz
    // tick applies it, so a fast typist doesn't load a page per keystroke.
    void handlePreviewTick()
    {
        if (!previewDirty)
            return;

        previewDirty = false;
        previewSelected();
    }

    // Selection moved in the palette: switching between OPEN tabs previews
    // live underneath (it's free), so Enter only dismisses. Places that
    // aren't open would cost a page load per selection change, so they only
    // load when actually chosen (Enter / click).
    void previewItem(const GoItem& item)
    {
        if (!paletteOpen || item.tabId < 0)
            return;

        if (auto* tab = findTab(item.tabId))
            switchTo(*tab);
    }

    void openItem(const GoItem& item)
    {
        mru.touch(PlacesStore::normalize(item.url));

        if (item.tabId >= 0)
        {
            if (auto* tab = findTab(item.tabId))
                switchTo(*tab);
        }
        else
            openTab(item.url);

        hidePalette();
    }

    void cancelPalette()
    {
        if (originTab != nullptr && originTab != activeTab)
            switchTo(*originTab);

        hidePalette();
    }

    void toggleBookmark(const std::string& title, const std::string& url)
    {
        places.toggle(title, url);
        syncPalette();
    }

    // Rebuild the merged tabs+places list, re-rank it against the current
    // query, and push the render model. Called on every tab/place mutation so
    // an open palette stays live.
    void syncPalette()
    {
        rebuildPaletteItems();
        refilterPalette();
        publishResults();
    }

    void rebuildPaletteItems()
    {
        paletteAll.clear();

        for (auto& tab: tabs)
        {
            paletteAll.push_back(
                {tab->id,
                 tab->title.empty() ? tab->url : tab->title,
                 tab->url,
                 places.contains(PlacesStore::normalize(tab->url))});
        }

        for (auto& place: places.all())
            if (!hasTabWithURL(PlacesStore::normalize(place.url)))
                paletteAll.push_back({-1, place.title, place.url, true});

        // MRU is the palette's base order: whatever you used last is on top.
        // The fuzzy re-rank in refilterPalette is stable, so equal scores
        // keep this order too.
        std::stable_sort(paletteAll.begin(),
                         paletteAll.end(),
                         [this](const GoItem& a, const GoItem& b)
                         {
                             return mru.lastUsed(PlacesStore::normalize(a.url))
                                    > mru.lastUsed(PlacesStore::normalize(b.url));
                         });
    }

    void refilterPalette()
    {
        paletteFiltered.clear();

        if (paletteQuery.empty())
        {
            paletteFiltered = paletteAll;
        }
        else
        {
            auto scored = std::vector<std::pair<int, const GoItem*>> {};

            // score > 0 is the "really hit" bar: full-subsequence matches
            // spread thin across a long title/URL go negative on gap
            // penalties and shouldn't outrank the search row.
            for (auto& item: paletteAll)
            {
                auto score =
                    wim::fuzzyScore(paletteQuery, item.title + " " + item.url);

                if (score && *score > 0)
                    scored.push_back({*score, &item});
            }

            std::stable_sort(scored.begin(),
                             scored.end(),
                             [](const auto& a, const auto& b)
                             { return a.first > b.first; });

            for (auto& [score, item]: scored)
                paletteFiltered.push_back(*item);
        }

        if (!paletteQuery.empty())
            paletteFiltered.push_back(searchRow(paletteQuery));

        auto last = std::max((int) paletteFiltered.size() - 1, 0);
        paletteSelected = std::clamp(paletteSelected, 0, last);
    }

    static GoItem searchRow(const std::string& query)
    {
        auto row = GoItem {};
        row.title = looksLikeURL(query) ? "Open " + query
                                        : "Search Google for “" + query + "”";
        row.url = searchURL(query);
        row.isSearch = true;
        return row;
    }

    void publishResults()
    {
        api.results.publish({paletteFiltered, paletteSelected, generation});
    }

    // --- Page commands ----------------------------------------------------------

    void handleCommand(const std::string& command)
    {
        if (command == "focusAddressBar")
        {
            addressBar.setText("");
            addressBar.focus();
        }
        else if (command == "openPalette")
            showPalette();
        else if (command == "toggleBookmark")
        {
            if (activeTab != nullptr)
                toggleBookmark(activeTab->title, activeTab->url);
        }
        else if (command == "closeTab")
            Threads::callAsync([this] { closeTab(activeTab); });
        else if (command == "nextTab")
            cycleTab(1);
        else if (command == "prevTab")
            cycleTab(-1);
    }

    void navigateActive(const std::string& text)
    {
        activeTab->view().loadURL(searchURL(text));
        activeTab->view().focusContent();
    }

    // --- URL helpers ----------------------------------------------------------

    static std::string searchURL(const std::string& query)
    {
        if (looksLikeURL(query))
            return withScheme(query);

        return "https://www.google.com/search?q=" + urlEncode(query);
    }

    static bool looksLikeURL(const std::string& text)
    {
        if (text.find("://") != std::string::npos)
            return true;

        return text.find(' ') == std::string::npos
               && text.find('.') != std::string::npos;
    }

    static std::string withScheme(const std::string& url)
    {
        if (url.find("://") != std::string::npos)
            return url;

        return "https://" + url;
    }

    static std::string urlEncode(const std::string& text)
    {
        auto encoded = std::string {};

        for (auto c: text)
        {
            if (std::isalnum((unsigned char) c) || c == '-' || c == '_' || c == '.'
                || c == '~')
                encoded += c;
            else if (c == ' ')
                encoded += '+';
            else
            {
                char buffer[4];
                std::snprintf(buffer, sizeof(buffer), "%%%02X", (unsigned char) c);
                encoded += buffer;
            }
        }

        return encoded;
    }

    static WebView::Options getWebViewOptions()
    {
        auto options = WebView::Options();
        options.statusBar = false;
        return options;
    }

    static WebView::Options paletteOptions()
    {
        auto options = embeddedOptions("WimGo");
        options.transparentBackground = true;
        return options;
    }

    static constexpr auto homePage = "https://www.wikipedia.org";

    Api::WimApi api;
    AddressBar addressBar {std::string(homePage)};
    emberstore::Database db {emberstore::appDataDirectory("pond", "Wim"),
                             emberstore::Durability::Durable};
    PlacesStore places {db};
    MruStore mru {
        emberstore::Database {db.directory(), emberstore::Durability::Atomic}};
    View content;
    EA::OwnedVector<Tab> tabs;
    Tab* activeTab = nullptr;
    Tab* originTab = nullptr;
    std::int64_t nextTabId = 0;
    std::int64_t generation = 0;
    bool paletteOpen = false;
    bool paletteTouched = false;
    bool previewDirty = false;
    int paletteSelected = 0;
    std::string paletteQuery;
    std::vector<GoItem> paletteAll;
    std::vector<GoItem> paletteFiltered;
    Rect contentBounds;
    WebView paletteWeb {paletteOptions()};
    WebViewBridge transport {paletteWeb};
    Threads::Timer previewTimer {[this] { handlePreviewTick(); }, 8};
};

struct WimApp
{
    WimApp()
    {
        setApplicationMenuBar(buildDefaultWebViewMenuBar());
        window.setContentView(view);
    }

    static WindowOptions getOptions()
    {
        auto options = WindowOptions();

        options.title = "Wim";
        options.width = 1100;
        options.height = 760;
        options.minWidth = 480;
        options.minHeight = 320;

        return options;
    }

    BrowserView view;
    Window window {getOptions()};
};

int main()
{
    return eacp::Apps::run<WimApp>();
}
