#pragma once

#include <string>

namespace wim
{
std::string htmlEscape(const std::string& text);

// A self-contained page shown when a navigation fails (DNS, offline, TLS…).
// Both inputs are untrusted and get escaped.
std::string errorPageHTML(const std::string& url, const std::string& error);
} // namespace wim
