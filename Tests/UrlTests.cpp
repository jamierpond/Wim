#include <Url.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace wim;

auto tSchemeMeansURL = test("Url/an explicit scheme is always a URL") = []
{
    check(looksLikeURL("https://github.com"));
    check(looksLikeURL("http://x"));
    check(looksLikeURL("app://local/index.html"));
};

auto tDottedTokenIsURL = test("Url/a spaceless dotted token is a URL") = []
{
    check(looksLikeURL("github.com"));
    check(looksLikeURL("news.ycombinator.com/news"));
};

auto tLocalhostIsURL = test("Url/localhost counts as a URL without a dot") = []
{
    check(looksLikeURL("localhost"));
    check(looksLikeURL("localhost:5173/app"));
};

auto tProseIsNotURL = test("Url/anything with spaces is a search") = []
{
    check(!looksLikeURL("how to exit vim"));
    check(!looksLikeURL("weather 94110 today"));
};

auto tBareWordIsNotURL = test("Url/a bare word is a search") = []
{
    check(!looksLikeURL("recipes"));
    check(!looksLikeURL(""));
};

auto tWithSchemeAddsHttps =
    test("Url/withScheme adds https:// only when missing") = []
{
    check(withScheme("github.com") == "https://github.com");
    check(withScheme("http://github.com") == "http://github.com");
};

auto tNavigationURL = test("Url/navigationURL loads URLs and searches prose") = []
{
    check(navigationURL("github.com") == "https://github.com");
    check(navigationURL("hello world")
          == "https://www.google.com/search?q=hello+world");
};

auto tUrlEncode = test("Url/urlEncode escapes reserved characters") = []
{
    check(urlEncode("a b") == "a+b");
    check(urlEncode("c++ & you") == "c%2B%2B+%26+you");
    check(urlEncode("safe-._~") == "safe-._~");
};

auto tNormalizeStripsNoise =
    test("Url/normalizeURL strips scheme, www. and trailing slashes") = []
{
    check(normalizeURL("https://www.github.com/") == "github.com");
    check(normalizeURL("http://github.com//") == "github.com");
    check(normalizeURL("github.com") == "github.com");
};

auto tNormalizeKeepsPath = test("Url/normalizeURL keeps paths and queries") = []
{
    check(normalizeURL("https://github.com/pulls?q=open")
          == "github.com/pulls?q=open");
};
