//
// PluginDefinitionCache.cpp
//
#include "PluginDefinitionCache.h"

namespace rs::legacy_pi {

ParsedDefinitionResult PluginDefinitionCache::Get(
    const std::wstring& path, const std::function<ParsedDefinitionResult()>& parse) {
    std::error_code ec;
    std::filesystem::file_time_type mtime = std::filesystem::last_write_time(path, ec);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = map_.find(path);
        if (it != map_.end() && it->second->mtime == mtime) {
            lru_.splice(lru_.begin(), lru_, it->second);
            return it->second->result; // コピー返し
        }
    }

    ParsedDefinitionResult res = parse();
    if (!res.ok) return res; // 失敗はキャッシュしない

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = map_.find(path);
    if (it != map_.end()) {
        lru_.erase(it->second);
        map_.erase(it);
    }
    Entry e;
    e.path = path;
    e.mtime = mtime;
    e.result = res;
    lru_.push_front(std::move(e));
    map_[path] = lru_.begin();
    while (lru_.size() > kMaxEntries) {
        map_.erase(lru_.back().path);
        lru_.pop_back();
    }
    return res;
}

PluginDefinitionCache& GetPluginDefinitionCache() {
    static PluginDefinitionCache sCache;
    return sCache;
}

} // namespace rs::legacy_pi
