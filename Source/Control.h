#pragma once

#include <Miro/Bridge.h>
#include <Miro/Reflect.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// The singleton control plane. The first Wim to start holds `lockName` and
// serves `ControlApi` on `channelName`; every later invocation -- a duplicate
// launch or a `--list-open-tabs` / `--focus-tab` / `--open-url` CLI command --
// dials that channel instead of opening a second window. Names are per user
// and fold to files, so a bundle id is the right shape (see IPC::Lock).
namespace Control
{

inline constexpr auto lockName = "com.pond.wim";
inline constexpr auto channelName = "com.pond.wim.control";

// One open tab, as reported to the CLI. `title` falls back to the URL before
// a page has named itself, mirroring how the Go palette renders a fresh tab.
struct TabInfo
{
    std::int64_t id = -1;
    std::string title;
    std::string url;
    bool active = false;
    bool failed = false;

    MIRO_REFLECT(id, title, url, active, failed)
};

struct TabList
{
    std::vector<TabInfo> tabs;

    MIRO_REFLECT(tabs)
};

struct TabRef
{
    std::int64_t id = -1;

    MIRO_REFLECT(id)
};

struct UrlRef
{
    std::string url;

    MIRO_REFLECT(url)
};

// A command's outcome: ok plus a human-readable reason when it isn't (an
// unknown tab id, an empty URL). The CLI prints `message` to stderr and exits
// non-zero when `ok` is false.
struct Ack
{
    bool ok = false;
    std::string message;

    MIRO_REFLECT(ok, message)
};

// The running instance's control surface, mounted on a Miro::Bridge and served
// over the IPC channel. BrowserView injects the behaviour through the
// std::function hooks; the CLI client dials in and invokes these by name.
// Handlers run main-thread-deferred (RpcServer's default), so the hooks may
// touch tabs and windows directly.
class ControlApi
{
public:
    void reflect(Miro::ApiReflector& r)
    {
        r.command(&ControlApi::listTabs, "listTabs");
        r.command(&ControlApi::focusTab, "focusTab");
        r.command(&ControlApi::openUrl, "openUrl");
        r.command(&ControlApi::reactivate, "reactivate");
    }

    TabList listTabs() { return onListTabs(); }
    Ack focusTab(const TabRef& ref) { return onFocusTab(ref.id); }
    Ack openUrl(const UrlRef& ref) { return onOpenUrl(ref.url); }
    Ack reactivate() { return onReactivate(); }

    std::function<TabList()> onListTabs = [] { return TabList {}; };
    std::function<Ack(std::int64_t)> onFocusTab = [](std::int64_t)
    { return Ack {}; };
    std::function<Ack(const std::string&)> onOpenUrl = [](const std::string&)
    { return Ack {}; };
    std::function<Ack()> onReactivate = [] { return Ack {true, ""}; };
};

} // namespace Control
