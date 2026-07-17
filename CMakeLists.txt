cmake_minimum_required(VERSION 3.31)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(Wim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/CPM.cmake)

# Work against a local eacp checkout with:
#   cmake -B build -DCPM_eacp_SOURCE=$HOME/projects/eacp
CPMAddPackage(
        NAME eacp
        GITHUB_REPOSITORY jamierpond/eacp
        GIT_TAG main
)

# Local persistence (Firestore-flavoured JSON documents). Must come after
# eacp -- emberstore fetches its own copy only when no eacp targets exist.
CPMAddPackage(
        NAME emberstore
        GITHUB_REPOSITORY tamber-inc/emberstore
        GIT_TAG main
)

list(APPEND CMAKE_MODULE_PATH "${eacp_SOURCE_DIR}/CMake")

# eacp only sets its bundle plist template when built top-level.
set(EACP_MACOS_PLIST "${eacp_SOURCE_DIR}/CMake/macOSBundleInfo.plist.in"
        CACHE INTERNAL "eacp macOS bundle Info.plist template")

eacp_add_webview_app(Wim
        SOURCES     Source/Main.cpp
        API         Api::WimApi
        API_HEADER  ${CMAKE_CURRENT_SOURCE_DIR}/Source/Types.h
        WEB_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/web
        CATEGORY    WimGo
        BUNDLE_ID   com.pond.wim
        BUNDLE_NAME "Wim"
        REACT)

target_link_libraries(Wim PRIVATE emberstore)
