#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <filesystem>
#include <string>
#include <vector>

namespace wim
{
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

// The user's saved places, an emberstore collection keyed by normalized URL.
// First run imports the legacy ~/.wim/places.json if present, else seeds a
// starter set.
struct PlacesStore
{
    explicit PlacesStore(const emberstore::Database& db,
                         std::filesystem::path legacyPathToUse =
                             defaultLegacyPath());

    bool contains(const std::string& normalizedUrl);

    // Adds the page as a place, or removes it if it already is one.
    void toggle(const std::string& title, const std::string& url);

    std::vector<Place> all();

    static std::filesystem::path defaultLegacyPath();

    bool importLegacy();
    void seedDefaults();

    emberstore::Collection<Place> collection;
    std::filesystem::path legacyPath;
};
} // namespace wim
