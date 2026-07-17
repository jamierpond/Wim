#include "Control.h"
#include "ErrorPage.h"
#include "History.h"
#include "Mru.h"
#include "PageScript.h"
#include "Palette.h"
#include "Places.h"
#include "Session.h"
#include "Types.h"
#include "Url.h"

#include <eacp/Network/IPC/Lock.h>
#include <eacp/Network/IPCRpc/RpcClient.h>
#include <eacp/Network/IPCRpc/RpcServer.h>
#include <eacp/WebView/WebView.h>
#include <eacp/WebView/WebView/JsStringLiteral.h>
#include <emberstore/AppDatabase.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
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
            auto cleared = item;
            Threads::callAsync([this, cleared] { clearItem(cleared); });
        };
        api.onToggleBookmark = [this](const GoItem& item)
        { toggleBookmark(item.title, item.url); };

        transport.getBridge().use(api);

        chromeBar.addScriptMessageHandler(
            "wimChrome",
            [this](const std::string& command)
            {
                if (command == "toggleMaximize")
                    onToggleMaximize();
                else if (command == "back" && activeTab != nullptr)
                    activeTab->view().goBack();
                else if (command == "forward" && activeTab != nullptr)
                    activeTab->view().goForward();
            });
        chromeBar.loadHTML(chromeBarHtml);

        addChildren({content, chromeBar});
        setupControl();
        restoreSession();
    }

    // --- CLI control plane ------------------------------------------------------

    // Serve the singleton control channel: list / focus / open commands from a
    // later `./Wim --...` invocation land here. Only the lock-holding instance
    // reaches this, so the name is ours -- but a control-plane hiccup must
    // never take the browser down, hence the swallowed IPC::Error.
    void setupControl()
    {
        controlApi.onListTabs = [this] { return buildTabList(); };
        controlApi.onFocusTab = [this](std::int64_t id) { return focusTabById(id); };
        controlApi.onOpenUrl = [this](const std::string& text)
        { return openUrlFromControl(text); };
        controlApi.onReactivate = [this]
        {
            onActivateWindow();
            return Control::Ack {true, ""};
        };

        controlBridge.use(controlApi);

        try
        {
            controlServer.emplace(Control::channelName, controlBridge);
        }
        catch (const IPC::Error&)
        {
        }
    }

    Control::TabList buildTabList()
    {
        auto list = Control::TabList {};

        for (auto& tab: tabs)
        {
            auto info = Control::TabInfo {};
            info.id = tab->id;
            info.title = tab->title.empty() ? tab->url : tab->title;
            info.url = tab->url;
            info.active = tab.get() == activeTab;
            info.failed = tab->failed;
            list.tabs.push_back(std::move(info));
        }

        return list;
    }

    Control::Ack focusTabById(std::int64_t id)
    {
        auto* tab = findTab(id);

        if (tab == nullptr)
            return {false, "no open tab with id " + std::to_string(id)};

        if (paletteOpen)
            hidePalette();

        mru.touch(normalizeURL(tab->url));
        switchTo(*tab);
        onActivateWindow();
        return {true, ""};
    }

    Control::Ack openUrlFromControl(const std::string& text)
    {
        if (text.empty())
            return {false, "no url given"};

        openTab(navigationURL(text));
        onActivateWindow();
        return {true, ""};
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

            updateChromeNav();
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

    // The palette's ✕ / ctrl-x: remove whatever the row is -- close an open
    // tab, unsave a place, or forget a history suggestion.
    void clearItem(const GoItem& item)
    {
        if (item.isSearch)
            return;

        if (item.tabId >= 0)
        {
            closeTabById(item.tabId);
            return;
        }

        if (item.bookmarked)
            places.toggle(item.title, item.url);
        else
            history.forget(item.url);

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
        updateChromeNav();
    }

    void updateChromeNav()
    {
        auto canBack = activeTab != nullptr && activeTab->view().canGoBack();
        auto canForward = activeTab != nullptr && activeTab->view().canGoForward();

        chromeBar.evaluateJavaScript(
            std::string {"window.__wimNav && window.__wimNav("}
            + (canBack ? "true" : "false") + "," + (canForward ? "true" : "false")
            + ")");
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
  body { background: #131316; --eacp-app-region: drag;
         display: flex; align-items: center; }
  .nav { display: flex; gap: 2px; margin-left: 84px;
         --eacp-app-region: no-drag; }
  .nav button { width: 32px; height: 26px; border: none; border-radius: 6px;
                background: transparent; color: #b9b9c4; font-size: 16px;
                line-height: 1; padding: 0; }
  .nav button:hover { background: #26262e; color: #fff; }
  .nav button:disabled { opacity: .3; background: transparent; }
  .nav button[hidden] { display: none; }
</style></head><body>
<div class="nav">
  <button id="back" title="Back" disabled>&#8592;</button>
  <button id="fwd" title="Forward" hidden>&#8594;</button>
</div>
<script>
  const post = message =>
      window.webkit?.messageHandlers?.wimChrome?.postMessage(message);

  addEventListener('dblclick', event => {
      if (event.target.tagName !== 'BUTTON')
          post('toggleMaximize');
  });

  back.onclick = () => post('back');
  fwd.onclick = () => post('forward');

  window.__wimNav = (canBack, canForward) => {
      back.disabled = !canBack;
      fwd.hidden = !canForward;
  };
</script></body></html>)HTML";

    // Bound by WimApp, which owns the Window: double-clicking the chrome bar
    // zooms to fill the workspace, like a native titlebar.
    std::function<void()> onToggleMaximize = [] {};

    // Bound by WimApp: raise and activate the window. Fired when a CLI control
    // command (focus / open / a duplicate launch) brings the browser forward.
    std::function<void()> onActivateWindow = [] {};

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

    // The CLI control plane. controlServer is last on purpose: it must be torn
    // down before the bridge and api it dispatches into.
    Control::ControlApi controlApi;
    Miro::Bridge controlBridge;
    std::optional<IPC::RpcServer> controlServer;
};

struct WimApp
{
    WimApp()
    {
        setApplicationMenuBar(buildMenuBar());
        view.onToggleMaximize = [this] { window.toggleMaximize(); };
        view.onActivateWindow = [this] { window.toFront(); };
        window.setContentView(view);
        window.toggleMaximize();
    }

    // The Edit menu is load-bearing: macOS only delivers Cmd+X/C/V/A to the
    // focused view (the palette input, page fields) by matching them against
    // the menu bar -- without it, paste is dead everywhere.
    static MenuBar buildMenuBar()
    {
        auto bar = MenuBar {};
        bar.add(standardApplicationMenu("Wim"));
        bar.add(standardEditMenu());

        auto viewMenu = Menu {"View"};
        viewMenu.add(MenuItem::withAction(
            "Zoom In", WebHelpers::zoomInFocusedWebView, commandKey("+")));
        viewMenu.add(MenuItem::withAction(
            "Zoom Out", WebHelpers::zoomOutFocusedWebView, commandKey("-")));
        viewMenu.add(MenuItem::withAction(
            "Actual Size", WebHelpers::resetSizedFocusedWebView, commandKey("0")));
        bar.add(std::move(viewMenu));

        return bar;
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

// --- CLI control commands -------------------------------------------------------

// A control command aimed at the running singleton. Reactivate is the internal
// "a duplicate launch lost the lock -- raise the existing window" case; the
// other three come straight from the CLI flags.
enum class ControlKind
{
    ListTabs,
    FocusTab,
    OpenUrl,
    Reactivate
};

struct ControlRequest
{
    ControlKind kind;
    std::int64_t tabId = -1;
    std::string url;
};

namespace
{
// Handed to ControlClientApp, which Apps::run default-constructs and so cannot
// be passed the request directly.
std::optional<ControlRequest> pendingRequest;

[[noreturn]] void usageError(const std::string& message)
{
    std::fprintf(stderr, "wim: %s\n", message.c_str());
    std::exit(2);
}

std::int64_t parseTabId(const std::string& text)
{
    try
    {
        return std::stoll(text);
    }
    catch (const std::exception&)
    {
        usageError("--focus-tab expects a numeric tab id, got '" + text + "'");
    }
}

// Recognises the control flags; returns nullopt for a plain launch. A known
// flag missing its argument is a hard usage error rather than a silent launch.
std::optional<ControlRequest> parseControlRequest(const std::vector<std::string>& args)
{
    for (std::size_t i = 1; i < args.size(); ++i)
    {
        const auto& arg = args[i];

        if (arg == "--list-open-tabs")
            return ControlRequest {.kind = ControlKind::ListTabs};

        if (arg == "--focus-tab")
        {
            if (i + 1 >= args.size())
                usageError("--focus-tab needs a tab id");

            return ControlRequest {.kind = ControlKind::FocusTab,
                                   .tabId = parseTabId(args[i + 1])};
        }

        if (arg == "--open-url")
        {
            if (i + 1 >= args.size())
                usageError("--open-url needs a url");

            return ControlRequest {.kind = ControlKind::OpenUrl, .url = args[i + 1]};
        }

        if (arg.starts_with("--"))
            usageError("unknown option '" + arg + "'");
    }

    return std::nullopt;
}
} // namespace

// A windowless app that dials the running instance, issues the pending command
// and quits with the result. It never creates a Window, so `./Wim --...` acts
// on the existing browser without launching a second one. Missing instance:
// the dial never lands, onDisconnected reports "not running" and exits 1 (a
// duplicate-launch reactivate exits 0 silently -- a plain launch shouldn't
// error just because the peer raced away).
struct ControlClientApp
{
    ControlClientApp()
    {
        // A control command is a quick round trip, not an app -- keep it out
        // of the Dock and the app switcher.
        eacp::Apps::setDockIconVisible(false);

        client.onConnected = [this]
        {
            connected = true;
            dispatch();
        };

        client.onDisconnected = [this]
        {
            if (settled)
                return;

            settled = true;
            auto reactivate = pendingRequest->kind == ControlKind::Reactivate;

            if (!connected && !reactivate)
                std::fputs("wim: not running\n", stderr);

            eacp::Apps::quit(reactivate || connected ? 0 : 1);
        };
    }

    void dispatch()
    {
        const auto& request = *pendingRequest;

        switch (request.kind)
        {
            case ControlKind::ListTabs:
                client.call<Control::TabList>("listTabs")
                    .then([this](Control::TabList list) { printTabs(list); },
                          [this](const std::string& error) { fail(error); });
                break;

            case ControlKind::FocusTab:
                client.call<Control::Ack>("focusTab", Control::TabRef {request.tabId})
                    .then([this](Control::Ack ack) { reportAck(ack); },
                          [this](const std::string& error) { fail(error); });
                break;

            case ControlKind::OpenUrl:
                client.call<Control::Ack>("openUrl", Control::UrlRef {request.url})
                    .then([this](Control::Ack ack) { reportAck(ack); },
                          [this](const std::string& error) { fail(error); });
                break;

            case ControlKind::Reactivate:
                client.call<Control::Ack>("reactivate")
                    .then([this](Control::Ack) { finish(0); },
                          [this](const std::string&) { finish(0); });
                break;
        }
    }

    // id, active marker, title, url -- tab-separated so `cut -f1` yields the id
    // to hand back to `--focus-tab`.
    void printTabs(const Control::TabList& list)
    {
        for (auto& tab: list.tabs)
            std::printf("%lld\t%s\t%s\t%s\n",
                        (long long) tab.id,
                        tab.active ? "*" : "",
                        tab.title.c_str(),
                        tab.url.c_str());

        finish(0);
    }

    void reportAck(const Control::Ack& ack)
    {
        if (!ack.ok && !ack.message.empty())
            std::fprintf(stderr, "wim: %s\n", ack.message.c_str());

        finish(ack.ok ? 0 : 1);
    }

    void fail(const std::string& reason)
    {
        if (settled)
            return;

        std::fprintf(stderr, "wim: %s\n", reason.empty() ? "request failed"
                                                         : reason.c_str());
        finish(1);
    }

    void finish(int code)
    {
        if (settled)
            return;

        settled = true;
        eacp::Apps::quit(code);
    }

    bool connected = false;
    bool settled = false;
    eacp::IPC::RpcClient client {Control::channelName, eacp::Time::MS {4000}};
};

int main(int argc, char* argv[])
{
    using namespace eacp;

    Apps::setCommandLineArgs(argc, argv);

    // An explicit control flag never launches a window: talk to the running
    // instance and exit.
    if (auto request = parseControlRequest({argv, argv + argc}))
    {
        pendingRequest = request;
        return Apps::run<ControlClientApp>();
    }

    // Plain launch. Hold the per-user singleton for the whole process; the
    // static locals outlive Apps::run, so the lock is released only at exit
    // (or by the kernel on a crash). Losing it means another Wim already owns
    // the window -- raise that one and go, don't open a second.
    static IPC::Lock instanceLock {Control::lockName};
    static IPC::ScopedLock held {instanceLock};

    if (!held)
    {
        pendingRequest = ControlRequest {.kind = ControlKind::Reactivate};
        return Apps::run<ControlClientApp>();
    }

    return Apps::run<WimApp>(argc, argv);
}
