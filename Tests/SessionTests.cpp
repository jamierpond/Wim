#include "TestSupport.h"

#include <Session.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace wim;

auto tRoundTrips = test("Session/save and load round-trip") = []
{
    auto db = testing::freshDatabase("session-roundtrip");
    auto store = SessionStore {db};

    store.save({{"https://a.com", "https://b.com"}, 1});

    auto restored = store.load();
    check(restored.urls.size() == 2);
    check(restored.urls[1] == "https://b.com");
    check(restored.active == 1);
};

auto tFreshStoreIsEmpty = test("Session/a fresh store loads an empty session") = []
{
    auto db = testing::freshDatabase("session-fresh");
    auto store = SessionStore {db};

    auto state = store.load();
    check(state.urls.empty());
    check(state.active == 0);
};

auto tSanitizeDropsEmptyURLs = test("Session/sanitize drops empty URLs") = []
{
    auto state = sanitizeSession({{"", "https://a.com", ""}, 0});

    check(state.urls.size() == 1);
    check(state.urls[0] == "https://a.com");
};

auto tSanitizeClampsActive = test("Session/sanitize clamps the active index") = []
{
    check(sanitizeSession({{"https://a.com"}, 5}).active == 0);
    check(sanitizeSession({{"https://a.com"}, -3}).active == 0);
    check(sanitizeSession({{}, 2}).active == 0);
};
