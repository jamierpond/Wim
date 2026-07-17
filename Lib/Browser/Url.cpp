#include "Url.h"

#include <cctype>
#include <cstdio>

namespace wim
{
std::string navigationURL(const std::string& text)
{
    if (looksLikeURL(text))
        return withScheme(text);

    return searchURLFor(text);
}

bool looksLikeURL(const std::string& text)
{
    if (text.empty())
        return false;

    if (text.find("://") != std::string::npos)
        return true;

    if (text.find(' ') != std::string::npos)
        return false;

    return text.find('.') != std::string::npos
           || text.starts_with("localhost");
}

std::string withScheme(const std::string& url)
{
    if (url.find("://") != std::string::npos)
        return url;

    return "https://" + url;
}

std::string urlEncode(const std::string& text)
{
    auto encoded = std::string {};

    for (auto c: text)
    {
        if (std::isalnum((unsigned char) c) || c == '-' || c == '_' || c == '.'
            || c == '~')
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

std::string searchURLFor(const std::string& query)
{
    return "https://www.google.com/search?q=" + urlEncode(query);
}

std::string normalizeURL(std::string url)
{
    for (auto* prefix: {"https://", "http://"})
    {
        auto view = std::string_view(prefix);

        if (url.starts_with(view))
        {
            url = url.substr(view.size());
            break;
        }
    }

    if (url.starts_with("www."))
        url = url.substr(4);

    while (!url.empty() && url.back() == '/')
        url.pop_back();

    return url;
}
} // namespace wim
