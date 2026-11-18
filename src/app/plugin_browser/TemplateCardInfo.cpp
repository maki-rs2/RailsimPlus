#include "TemplateCardInfo.h"

#include <filesystem>
#include <system_error>

#include "../../platform/text/Encoding.h"

namespace rs::app::plugin_browser {

TemplateCardInfo ComputeTemplateCardInfo(const TemplateImageSource& source, const TemplateParseFunc& parse) {
    TemplateCardInfo info;
    if (source.kind == TemplateImageKind::Rail) {
        info.imageKey = MakeTemplateImageKey(source);
        return info;
    }
    const TrainGroupTemplateParseResult parsed = parse(source.pathUtf8, source.rootUtf8);
    const TrainGroupTemplateSummary summary = SummarizeTrainGroupTemplate(parsed);
    info.hasSummary = true;
    info.carCount = summary.carCount;
    info.missingCount = summary.missingCount;
    info.imageKey = MakeTrainTemplateImageKey(source, parsed);
    return info;
}

TemplateCardInfoCacheKey MakeTemplateCardInfoCacheKey(const TemplateImageSource& source, std::uint64_t generation) {
    TemplateCardInfoCacheKey key;
    key.kind = source.kind;
    key.pathUtf8 = source.pathUtf8;
    key.rootUtf8 = source.rootUtf8;
    key.generation = generation;
    std::error_code error;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(rs::platform_text::Utf8ToWide(source.pathUtf8), error);
    key.writeTime = error ? 0 : static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    return key;
}

std::optional<TemplateCardInfo> TemplateCardInfoCache::Find(const TemplateCardInfoCacheKey& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::map<TemplateCardInfoCacheKey, TemplateCardInfo>::const_iterator it = entries_.find(key);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

void TemplateCardInfoCache::Store(const TemplateCardInfoCacheKey& key, const TemplateCardInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() >= maxEntries_ && !entries_.contains(key)) entries_.clear();
    entries_[key] = info;
}

std::size_t TemplateCardInfoCache::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

} // namespace rs::app::plugin_browser
