add_executable(WimTests
        ErrorPageTests.cpp
        FuzzyMatchTests.cpp
        HistoryTests.cpp
        MruTests.cpp
        PaletteTests.cpp
        PlacesTests.cpp
        SessionTests.cpp
        UrlTests.cpp)

target_link_libraries(WimTests PRIVATE NanoTest NanoTestMain WimBrowser)
nano_discover_tests(WimTests)
