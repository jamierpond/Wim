#include <FuzzyMatch.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace wim;

auto tSubsequenceMatches = test("FuzzyMatch/subsequences match") = []
{
    check(fuzzyScore("ghub", "GitHub").has_value());
    check(fuzzyScore("wiki", "Wikipedia — Main Page").has_value());
};

auto tMissingCharsDontMatch = test("FuzzyMatch/missing characters fail") = []
{ check(!fuzzyScore("gz", "GitHub").has_value()); };

auto tCaseInsensitive = test("FuzzyMatch/matching ignores case") = []
{ check(fuzzyScore("GITHUB", "github.com").has_value()); };

auto tSpacesIgnored = test("FuzzyMatch/spaces in the query are skipped") = []
{ check(fuzzyScore("wiki main", "Wikipedia — Main Page").has_value()); };

auto tExactBeatsScattered =
    test("FuzzyMatch/consecutive hits outrank scattered ones") = []
{
    auto exact = fuzzyScore("git", "github.com");
    auto scattered = fuzzyScore("git", "grand illusion tour");

    check(exact.has_value() && scattered.has_value());
    check(*exact > *scattered);
};

auto tEmptyQueryMatchesAll = test("FuzzyMatch/the empty query matches anything") = []
{ check(fuzzyScore("", "anything").has_value()); };
