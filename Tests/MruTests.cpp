#include "TestSupport.h"

#include <Mru.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace wim;

auto tUnknownURLIsZero = test("Mru/an untouched URL reports zero") = []
{
    auto db = testing::freshDatabase("mru-zero");
    auto mru = MruStore {db};

    check(mru.lastUsed("github.com") == 0);
};

auto tTouchStamps = test("Mru/touch records the given time") = []
{
    auto db = testing::freshDatabase("mru-touch");
    auto mru = MruStore {db};

    mru.touch("github.com", 1000);
    mru.touch("github.com", 2000);

    check(mru.lastUsed("github.com") == 2000);
};

auto tPruneKeepsNewest = test("Mru/prune drops the oldest stamps") = []
{
    auto db = testing::freshDatabase("mru-prune");
    auto mru = MruStore {db};

    for (std::size_t i = 0; i < MruStore::maxEntries + 10; ++i)
        mru.touch("site-" + std::to_string(i) + ".com", (std::int64_t) i + 1);

    mru.prune();

    check(mru.stamps.size() == MruStore::maxEntries);
    check(mru.lastUsed("site-0.com") == 0);
    check(mru.lastUsed("site-" + std::to_string(MruStore::maxEntries + 9) + ".com")
          == (std::int64_t) MruStore::maxEntries + 10);
};
