#pragma once

#include <string>

namespace wim
{
// What the omnibox does with typed text: anything URL-shaped loads directly
// (scheme added if missing), everything else becomes a web search.
std::string navigationURL(const std::string& text);

// URL-shaped: has an explicit scheme, or is a spaceless token with a dot
// ("news.ycombinator.com"), or is localhost (with optional port/path).
bool looksLikeURL(const std::string& text);

// Adds https:// when no scheme is present.
std::string withScheme(const std::string& url);

// Percent-encodes a search query for use in a URL query parameter.
std::string urlEncode(const std::string& text);

std::string searchURLFor(const std::string& query);

// Canonical key for "the same place": scheme, leading www. and trailing
// slashes stripped. Used to match tabs against places, history and MRU
// stamps.
std::string normalizeURL(std::string url);
} // namespace wim
