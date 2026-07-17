#pragma once

#include <Miro/Reflect.h>
#include <emberstore/Emberstore.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wim
{
struct SessionState
{
    std::vector<std::string> urls;
    std::int64_t active = 0;

    MIRO_REFLECT(urls, active)
};

// Drops empty URLs and clamps `active` into range, so a hand-edited or
// corrupt session file can't restore into a broken state.
SessionState sanitizeSession(SessionState state);

// The open tabs + active index, persisted on every tab mutation so a relaunch
// picks up where the last run left off. Atomic database: losing the very last
// change to a crash is fine.
struct SessionStore
{
    explicit SessionStore(const emberstore::Database& db);

    SessionState load();
    void save(SessionState state);

    emberstore::Document<SessionState> document;
};
} // namespace wim
