#include <ErrorPage.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace wim;

auto tEscapesMarkup = test("ErrorPage/htmlEscape neutralizes markup") = []
{
    check(htmlEscape("<script>alert('&')</script>")
          == "&lt;script&gt;alert(&#39;&amp;&#39;)&lt;/script&gt;");
    check(htmlEscape("plain text") == "plain text");
};

auto tPageShowsUrlAndError = test("ErrorPage/the page shows URL and reason") = []
{
    auto html = errorPageHTML("https://example.com", "Server not found");

    check(html.find("https://example.com") != std::string::npos);
    check(html.find("Server not found") != std::string::npos);
};

auto tPageEscapesInputs = test("ErrorPage/untrusted inputs are escaped") = []
{
    auto html = errorPageHTML("https://example.com/<svg>", "<b>boom</b>");

    check(html.find("<svg>") == std::string::npos);
    check(html.find("<b>") == std::string::npos);
};
