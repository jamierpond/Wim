#pragma once

#include <emberstore/Emberstore.h>

#include <filesystem>
#include <string>

namespace wim::testing
{
// A fresh on-disk database per test, under the system temp directory. The
// directory is wiped up front so every run starts clean; `name` keeps tests
// in one binary from sharing state.
inline emberstore::Database freshDatabase(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / "WimTests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return emberstore::Database {dir.string()};
}
} // namespace wim::testing
