#include "ErrorPage.h"
#include "History.h"
#include "Mru.h"
#include "PageScript.h"
#include "Palette.h"
#include "Places.h"
#include "Session.h"
#include "Types.h"
#include "Url.h"

#include <eacp/WebView/WebView.h>
#include <eacp/WebView/WebView/JsStringLiteral.h>
#include <emberstore/AppDatabase.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace eacp;
using namespace Graphics;
using namespace wim;

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
    bool failed = false;
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

        chromeBar.loadHTML(chromeBarHtml);

        addChildren({content, chromeBar});
        restoreSession();
    }

    // The chrome bar owns the titlebar band (its whole surface is an
    // --eacp-app-region drag handle, traffic lights float inside it); pages
    // lay out below it, never underneath. Tab webviews live inside `content`;
    // the palette is a permanently later sibling, so switching tabs never
    // touches its key focus.
    //
    // NOTE: root-level child coordinates are currently unflipped on macOS
    // (same as eacp's own Browser example), so the VISUAL top band is
    // removeFromBottom here.
    void resized() override
    {
        auto bounds = getLocalBounds();
        chromeBar.setBounds(bounds.removeFromBottom(chromeBarHeight));
        content.setBounds(bounds);

        if (activeTab != nullptr)
            activeTab->view().setBounds(content.getLocalBounds());

        if (paletteOpen)
            paletteWeb.setBounds(getLocalBounds());
    }

    // --- Session ----------------------------------------------------------------

    void restoreSession()
    {
        auto state = session.load();

        if (state.urls.empty())
        {
            openTab(homePage);
            return;
        }

        for (auto& url: state.urls)
            createTab(url);

        switchTo(*tabs[(int) state.active]);
        syncPalette();
    }

    void saveSession()
    {
        auto state = SessionState {};

        for (auto& tab: tabs)
            state.urls.push_back(tab->url);

        state.active = std::max(tabs.getIndexOfItem(activeTab), 0);
        session.save(std::move(state));
    }

    // --- Tabs -----------------------------------------------------------------

    Tab& createTab(const std::string& url)
    {
        auto& tab = tabs.createNew(nextTabId++, getWebViewOptions());
        tab.view().addUserScript(vimiumScript);
        tab.view().addScriptMessageHandler(
            "wim", [this](const std::string& command) { handleCommand(command); });

        wireTab(tab);

        // Known before any navigation event arrives, so a session save that
        // lands mid-load (or a load that fails outright) keeps the tab.
        tab.url = url;
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

        // The http guard keeps loadHTML navigations (the error page) from
        // clobbering the tab's real URL or polluting history.
        view.onNavigationStarted = [this, t](const std::string& url)
        {
            if (url.starts_with("http"))
            {
                t->url = url;
                t->failed = false;
                history.recordVisit(url, "", MruStore::nowMs());

                if (t == activeTab)
                    mru.touch(normalizeURL(url));
            }

            syncPalette();
        };

        view.onTitleChanged = [this, t](const std::string& title)
        {
            t->title = title;

            // The error page's own title must not rename the history entry
            // of the URL that failed to load.
            if (!t->failed)
                history.updateTitle(t->url, title);

            syncPalette();
        };

        view.onNavigationFinished = [this, t](const std::string&)
        {
            if (t == activeTab && !paletteOpen)
                t->view().focusContent();
        };

        view.onNavigationFailed = [this, t](const std::string& error)
        {
            t->failed = true;
            history.forget(t->url);
            t->view().loadHTML(errorPageHTML(t->url, error));
            syncPalette();
        };

        view.onNewWindowRequested =
            [this](EA::OwningPointer<WebView> popup, const std::string&)
        {
            adoptPopup(std::move(popup));
            return true;
        };

        view.onDownloadFinished = [this, t](const std::string& path)
        {
            auto name = std::filesystem::path(path).filename().string();
            showToast(activeTab != nullptr ? *activeTab : *t, "Downloaded " + name);
        };

        view.onDownloadFailed = [this, t](const std::string& error)
        {
            showToast(activeTab != nullptr ? *activeTab : *t,
                      "Download failed: " + error);
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

        saveSession();
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

    void cycleTab(int direction)
    {
        auto index = tabs.getIndexOfItem(activeTab);

        if (index < 0 || tabs.size() < 2)
            return;

        auto next = (index + direction + tabs.size()) % tabs.size();
        mru.touch(normalizeURL(tabs[next]->url));
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
        mru.touch(normalizeURL(item.url));

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
    // query, push the render model, and keep the on-disk session current.
    // Called on every tab/place mutation so an open palette stays live.
    void syncPalette()
    {
        rebuildPaletteItems();
        refilterPalette();
        publishResults();
        saveSession();
    }

    void rebuildPaletteItems()
    {
        auto tabItems = std::vector<GoItem> {};

        for (auto& tab: tabs)
        {
            auto item = GoItem {};
            item.tabId = tab->id;
            item.title = tab->title.empty() ? tab->url : tab->title;
            item.url = tab->url;
            item.failed = tab->failed;
            tabItems.push_back(std::move(item));
        }

        paletteAll = mergePaletteItems(std::move(tabItems),
                                       places.all(),
                                       [this](const std::string& url)
                                       { return mru.lastUsed(url); });
    }

    void refilterPalette()
    {
        paletteFiltered = rankPalette(
            paletteAll,
            paletteQuery.empty() ? std::vector<HistoryEntry> {} : history.all(),
            paletteQuery);

        auto last = std::max((int) paletteFiltered.size() - 1, 0);
        paletteSelected = std::clamp(paletteSelected, 0, last);
    }

    void publishResults()
    {
        api.results.publish(
            {paletteFiltered,
             paletteSelected,
             generation,
             activeTab != nullptr ? activeTab->url : std::string {}});
    }

    // --- Page commands ----------------------------------------------------------

    void handleCommand(const std::string& command)
    {
        if (command == "openPalette")
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

    // A page-side notification for events with no chrome of their own
    // (finished downloads, etc.).
    void showToast(Tab& tab, const std::string& message)
    {
        auto script = std::string {"(() => {"}
                      + "const toast = document.createElement('div');"
                      + "toast.textContent = " + jsStringLiteral(message) + ";"
                      + "toast.style.cssText = 'position:fixed;bottom:20px;"
                        "right:20px;z-index:2147483647;background:#222;"
                        "color:#fff;padding:10px 14px;border-radius:8px;"
                        "font:13px -apple-system,sans-serif;"
                        "box-shadow:0 4px 12px rgba(0,0,0,.35)';"
                      + "document.documentElement.appendChild(toast);"
                      + "setTimeout(() => toast.remove(), 4000); })()";

        tab.view().evaluateJavaScript(script);
    }

    static WebView::Options getWebViewOptions()
    {
        auto options = WebView::Options();
        options.statusBar = false;

        // The engine tail Safari itself appends. WebKit derives the OS/WebKit
        // prefix, so the UA reads as stock Safari and sites (Google sign-in
        // especially) treat Wim as a real browser, not an embedded web view.
        options.applicationNameForUserAgent = "Version/26.0 Safari/605.1.15";

        options.backForwardGestures = true;
        options.allowsMagnification = true;
        options.elementFullscreen = true;
        return options;
    }

    static WebView::Options paletteOptions()
    {
        auto options = embeddedOptions("WimGo");
        options.transparentBackground = true;
        return options;
    }

    static constexpr auto homePage = "https://www.wikipedia.org";

    static constexpr auto chromeBarHeight = 38.f;

    // Our own titlebar chrome, FancyWindow-style: one flat surface whose CSS
    // declares the whole bar a window-drag region.
    static constexpr auto chromeBarHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><style>
  html, body { margin: 0; height: 100%; }
  body { background: #131316; --eacp-app-region: drag; }
</style></head><body></body></html>)HTML";

    Api::WimApi api;
    emberstore::Database durable {emberstore::appDataDirectory("pond", "Wim"),
                                  emberstore::Durability::Durable};
    emberstore::Database atomic {durable.directory(),
                                 emberstore::Durability::Atomic};
    PlacesStore places {durable};
    MruStore mru {atomic};
    HistoryStore history {atomic};
    SessionStore session {atomic};
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
    WebView chromeBar {getWebViewOptions()};
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
        window.toggleMaximize();
    }

    // Frameless chrome: the page runs edge to edge under a hidden, transparent
    // title bar -- only the traffic lights remain, and the green one
    // fullscreens. Starts filling the workspace, like a browser should.
    static WindowOptions getOptions()
    {
        auto options = WindowOptions();

        options.title = "Wim";
        options.width = 1100;
        options.height = 760;
        options.minWidth = 480;
        options.minHeight = 320;

        options.flags.add(WindowFlags::FullSizeContentView);
        options.flags.add(WindowFlags::FullScreen);
        options.showTitle = false;
        options.titlebarTransparent = true;
        options.showTitlebarSeparator = false;
        options.cornerRadius = 12.f;

        // Centers the ~16pt-tall light cluster in the 38pt chrome bar.
        options.trafficLightPosition = Point {17.f, 11.f};

        return options;
    }

    BrowserView view;
    Window window {getOptions()};
};

int main()
{
    return eacp::Apps::run<WimApp>();
}
