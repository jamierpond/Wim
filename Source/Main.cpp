#include <eacp/WebView/WebView.h>

#include "TabSwitcher.h"

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
        'o': () => post('focusAddressBar'),
        't': () => post('openSwitcher'),
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

struct AddressBar final : TextInput
{
    using TextInput::TextInput;

    void keyDown(const KeyEvent& event) override
    {
        if (event.keyCode == KeyCode::Escape)
        {
            onEscape();
            return;
        }

        TextInput::keyDown(event);
    }

    std::function<void()> onEscape = [] {};
};

struct Tab
{
    explicit Tab(WebView::Options options)
        : webView(new WebView(std::move(options)))
    {
    }

    explicit Tab(EA::OwningPointer<WebView> adopted)
        : webView(std::move(adopted))
    {
    }

    WebView& view() { return *webView; }

    EA::OwningPointer<WebView> webView;
    std::string title;
    std::string url;
};

struct BrowserView final : View
{
    BrowserView()
    {
        addressBar.onSubmit([this](const std::string& text)
                            { navigateActive(text); });
        addressBar.onEscape = [this] { activeTab->view().focusContent(); };

        switcher.onDismiss = [this] { hideSwitcher(); };
        switcher.onTabChosen = [this](int index)
        {
            hideSwitcher();
            switchTo(*tabs[index]);
        };
        switcher.onQuerySubmitted = [this](const std::string& query)
        {
            hideSwitcher();
            openTab(searchURL(query));
        };

        addChildren({addressBar});
        openTab(homePage);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        addressBar.setBounds(bounds.removeFromTop(44.f).inset(8.f, 7.f));
        contentBounds = bounds;

        if (activeTab != nullptr)
            activeTab->view().setBounds(contentBounds);

        switcher.setBounds(getLocalBounds());
    }

    // --- Tabs ---------------------------------------------------------------

    void openTab(const std::string& url)
    {
        auto& tab = tabs.createNew(getWebViewOptions());
        tab.view().addUserScript(vimiumScript);
        tab.view().addScriptMessageHandler(
            "wim",
            [this](const std::string& command) { handleCommand(command); });

        wireTab(tab);
        tab.view().loadURL(url);
        switchTo(tab);
    }

    // A page-initiated popup (target=_blank, window.open) adopted as a tab. It
    // shares the opener's web-process configuration, so the vimium user script
    // and the "wim" message handler are already installed -- re-adding them
    // would collide on the shared user-content controller.
    void adoptPopup(EA::OwningPointer<WebView> popup)
    {
        auto& tab = tabs.createNew(std::move(popup));
        wireTab(tab);
        switchTo(tab);
    }

    void wireTab(Tab& tab)
    {
        auto* t = &tab;
        auto& view = tab.view();

        view.onNavigationStarted = [this, t](const std::string& url)
        {
            t->url = url;

            if (t == activeTab)
                addressBar.setText(url);
        };

        view.onTitleChanged = [t](const std::string& title)
        { t->title = title; };

        view.onNavigationFinished = [this, t](const std::string&)
        {
            if (t == activeTab && !addressBar.hasFocus() && !switcherVisible)
                t->view().focusContent();
        };

        view.onNewWindowRequested =
            [this](EA::OwningPointer<WebView> popup, const std::string&)
        {
            adoptPopup(std::move(popup));
            return true;
        };

        view.onClose = [this, t]
        { Threads::callAsync([this, t] { closeTab(t); }); };
    }

    void switchTo(Tab& tab)
    {
        if (activeTab == &tab)
            return;

        if (activeTab != nullptr)
            activeTab->view().removeFromParent();

        activeTab = &tab;
        addSubview(tab.view());
        tab.view().setBounds(contentBounds);
        addressBar.setText(tab.url);

        if (switcherVisible)
        {
            switcher.removeFromParent();
            addSubview(switcher);
        }
        else
            tab.view().focusContent();
    }

    void closeTab(Tab* tab)
    {
        auto index = tabs.getIndexOfItem(tab);

        if (index < 0)
            return;

        auto wasActive = tab == activeTab;

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
    }

    void cycleTab(int direction)
    {
        auto index = tabs.getIndexOfItem(activeTab);

        if (index < 0 || tabs.size() < 2)
            return;

        auto next = (index + direction + tabs.size()) % tabs.size();
        switchTo(*tabs[next]);
    }

    // --- Omnibar / commands ---------------------------------------------------

    void showSwitcher()
    {
        auto items = Vector<wim::TabSwitcher::Item> {};

        for (auto i = 0; i < tabs.size(); ++i)
            items.push_back({tabs[i]->title, tabs[i]->url, i});

        switcherVisible = true;
        addSubview(switcher);
        switcher.setBounds(getLocalBounds());
        switcher.open(std::move(items));
    }

    void hideSwitcher()
    {
        switcherVisible = false;
        switcher.removeFromParent();

        if (activeTab != nullptr)
            activeTab->view().focusContent();
    }

    void handleCommand(const std::string& command)
    {
        if (command == "focusAddressBar")
        {
            addressBar.setText("");
            addressBar.focus();
        }
        else if (command == "openSwitcher")
            showSwitcher();
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
            if (std::isalnum((unsigned char) c) || c == '-' || c == '_'
                || c == '.' || c == '~')
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

    static constexpr auto homePage = "https://www.wikipedia.org";

    AddressBar addressBar {std::string(homePage)};
    wim::TabSwitcher switcher;
    EA::OwnedVector<Tab> tabs;
    Tab* activeTab = nullptr;
    Rect contentBounds;
    bool switcherVisible = false;
};

struct WimApp
{
    WimApp() { window.setContentView(view); }

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
