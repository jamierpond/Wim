#include "ErrorPage.h"

namespace wim
{
std::string htmlEscape(const std::string& text)
{
    auto escaped = std::string {};
    escaped.reserve(text.size());

    for (auto c: text)
    {
        switch (c)
        {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&#39;";
                break;
            default:
                escaped += c;
                break;
        }
    }

    return escaped;
}

std::string errorPageHTML(const std::string& url, const std::string& error)
{
    return R"(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Can&#39;t reach this page</title>
<style>
  :root { color-scheme: light dark; }
  body {
    font: 15px/1.5 -apple-system, sans-serif;
    display: flex; align-items: center; justify-content: center;
    min-height: 90vh; margin: 0;
  }
  main { max-width: 34em; padding: 2em; }
  h1 { font-size: 1.3em; }
  .url { word-break: break-all; opacity: .75; }
  .error { opacity: .75; }
</style>
</head>
<body>
<main>
<h1>Wim can&#39;t reach this page</h1>
<p class="url">)"
           + htmlEscape(url) + R"(</p>
<p class="error">)"
           + htmlEscape(error) + R"(</p>
<p><a href=")"
           + htmlEscape(url) + R"(">Try again</a></p>
</main>
</body>
</html>
)";
}
} // namespace wim
