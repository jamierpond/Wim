#include "Session.h"

#include <algorithm>
#include <utility>

namespace wim
{
SessionState sanitizeSession(SessionState state)
{
    std::erase_if(state.urls, [](const std::string& url) { return url.empty(); });

    auto last = std::max((std::int64_t) state.urls.size() - 1, (std::int64_t) 0);
    state.active = std::clamp(state.active, (std::int64_t) 0, last);

    return state;
}

SessionStore::SessionStore(const emberstore::Database& db)
    : document(db.document<SessionState>("session"))
{
}

SessionState SessionStore::load()
{
    return sanitizeSession(document.get());
}

void SessionStore::save(SessionState state)
{
    document.set(sanitizeSession(std::move(state)));
}
} // namespace wim
