#include "Places.h"
#include "Url.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

namespace wim
{
PlacesStore::PlacesStore(const emberstore::Database& db,
                         std::filesystem::path legacyPathToUse)
    : collection(db.collection<Place>("places"))
    , legacyPath(std::move(legacyPathToUse))
{
    if (!collection.empty())
        return;

    if (!importLegacy())
        seedDefaults();
}

bool PlacesStore::contains(const std::string& normalizedUrl)
{
    return collection.contains(normalizedUrl);
}

void PlacesStore::toggle(const std::string& title, const std::string& url)
{
    auto ref = collection.doc(normalizeURL(url));

    if (ref.exists())
    {
        ref.remove();
        return;
    }

    ref.set({title.empty() ? url : title, url});
}

std::vector<Place> PlacesStore::all()
{
    return collection.values();
}

std::filesystem::path PlacesStore::defaultLegacyPath()
{
    auto* home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : ".") / ".wim"
           / "places.json";
}

bool PlacesStore::importLegacy()
{
    auto file = std::ifstream(legacyPath);

    if (!file)
        return false;

    auto text = std::string(std::istreambuf_iterator<char>(file), {});
    auto legacy = PlacesFile {};
    Miro::fromJSONString(legacy, text);

    for (auto& place: legacy.places)
        collection.doc(normalizeURL(place.url)).set(place);

    return !collection.empty();
}

void PlacesStore::seedDefaults()
{
    auto seeds = std::vector<Place> {{"GitHub", "https://github.com"},
                                     {"Gmail", "https://mail.google.com"},
                                     {"Linear", "https://linear.app"},
                                     {"Wikipedia", "https://www.wikipedia.org"}};

    for (auto& place: seeds)
        collection.doc(normalizeURL(place.url)).set(place);
}
} // namespace wim
