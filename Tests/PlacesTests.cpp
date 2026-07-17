#include "TestSupport.h"

#include <Places.h>
#include <Url.h>

#include <Miro/Reflect.h>
#include <NanoTest/NanoTest.h>

#include <fstream>

using namespace nano;
using namespace wim;

namespace
{
std::filesystem::path missingLegacy()
{
    return std::filesystem::temp_directory_path() / "WimTests"
           / "no-such-legacy.json";
}
} // namespace

auto tSeedsDefaultsWhenEmpty =
    test("Places/an empty store seeds the starter places") = []
{
    auto db = testing::freshDatabase("places-seed");
    auto store = PlacesStore {db, missingLegacy()};

    check(!store.all().empty());
    check(store.contains("github.com"));
};

auto tToggleAddsAndRemoves = test("Places/toggle bookmarks and unbookmarks") = []
{
    auto db = testing::freshDatabase("places-toggle");
    auto store = PlacesStore {db, missingLegacy()};

    store.toggle("Example", "https://example.com");
    check(store.contains("example.com"));

    store.toggle("Example", "https://www.example.com/");
    check(!store.contains("example.com"));
};

auto tToggleFallsBackToURLTitle =
    test("Places/a place saved without a title shows its URL") = []
{
    auto db = testing::freshDatabase("places-untitled");
    auto store = PlacesStore {db, missingLegacy()};

    store.toggle("", "https://example.com");

    auto found = false;

    for (auto& place: store.all())
        if (place.url == "https://example.com")
            found = place.title == "https://example.com";

    check(found);
};

auto tImportsLegacyFile = test("Places/first run imports the legacy JSON") = []
{
    auto legacy =
        std::filesystem::temp_directory_path() / "WimTests" / "legacy-places.json";
    std::filesystem::create_directories(legacy.parent_path());

    {
        auto file = std::ofstream {legacy};
        auto contents = PlacesFile {{{"Old Place", "https://old.example.com"}}};
        file << Miro::toJSONString(contents, 2);
    }

    auto db = testing::freshDatabase("places-legacy");
    auto store = PlacesStore {db, legacy};

    check(store.contains("old.example.com"));
    check(!store.contains("github.com"));
};

auto tPersistsAcrossReopen = test("Places/places survive reopening the store") = []
{
    auto db = testing::freshDatabase("places-reopen");

    {
        auto store = PlacesStore {db, missingLegacy()};
        store.toggle("Example", "https://example.com");
    }

    auto reopened = PlacesStore {db, missingLegacy()};
    check(reopened.contains("example.com"));
};
