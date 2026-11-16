//
// ParsedPluginCache.cpp
//
#include "ParsedPluginCache.h"

#include <chrono>
#include <filesystem>
#include <utility>

#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginPaths.h"

namespace rs::app {

namespace {

// ワーカーの先読みとEnsureParsedの読込が共有する、読込オプションと読込の入口。
// 定義ファイル名があれば、フォルダ内のそのファイルを指定して読む。
std::string ParseKey(const PluginSource& source, const rs::legacy_plugin::LoadOptions& options) {
    std::string key = MakePluginSourceKey(source);
    if (!options.forceDefFile.empty())
        key += "?def=" + rs::legacy_plugin::NormalizePluginFolderKey(
            rs::platform_text::WideToUtf8(options.forceDefFile));
    if (!options.loadMeshes) key += "|definition";
    if (!options.preferKBuild) key += "|standard";
    if (options.pierHeightOverrideM >= 0.0f) key += "|pier=" + std::to_string(options.pierHeightOverrideM);
    if (options.curveRadiusM != 0.0f) key += "|curve=" + std::to_string(options.curveRadiusM);
    return key;
}

rs::legacy_plugin::LoadedPlugin ParsePluginSource(
    const PluginSource& source, rs::legacy_plugin::LoadOptions options = {}) {
    const std::wstring folder = rs::platform_text::Utf8ToWide(source.folderUtf8);
    if (!source.definitionFile.empty()) {
        options.forceDefFile =
            (std::filesystem::path(folder) / rs::platform_text::Utf8ToWide(source.definitionFile)).wstring();
    }
    return rs::legacy_plugin::LoadPluginFolder(folder, options);
}

} // namespace

ParsedPluginCache::~ParsedPluginCache() {
    CancelPrefetch();
}

ParsedPluginCache& GetParsedPluginCache() {
    static ParsedPluginCache cache;
    return cache;
}

std::string MakePluginMeshKey(const std::string& contentKey) {
    return contentKey + "|generation=" + std::to_string(GetParsedPluginCache().Generation());
}

void ParsedPluginCache::CancelPrefetch() {
    cancelPrefetch_.store(true, std::memory_order_release);
    for (PrefetchBatch& batch : prefetchJobs_) batch.jobs.Join();
    prefetchJobs_.clear();
    cancelPrefetch_.store(false, std::memory_order_release);
}

void ParsedPluginCache::Invalidate() {
    CancelPrefetch();
    const std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    generation_.fetch_add(1, std::memory_order_acq_rel);
}

bool ParsedPluginCache::Contains(const PluginSource& source) const {
    std::shared_ptr<Slot> slot;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(ParseKey(source, {}));
        if (it == entries_.end()) return false;
        slot = it->second;
    }
    return slot->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

std::shared_ptr<ParsedPluginCache::Slot> ParsedPluginCache::AcquireSlot(const std::string& key, bool& ownerOut) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto [it, inserted] = entries_.try_emplace(key, nullptr);
    if (inserted) it->second = std::make_shared<Slot>();
    ownerOut = inserted;
    return it->second;
}

void ParsedPluginCache::FillSlot(const std::string& key, const std::shared_ptr<Slot>& slot,
                                 const PluginSource& source, const rs::legacy_plugin::LoadOptions& options) {
    try {
        slot->promise.set_value(
            std::make_shared<const rs::legacy_plugin::LoadedPlugin>(ParsePluginSource(source, options)));
    } catch (...) {
        slot->promise.set_exception(std::current_exception());
        // 失敗を表に残すと再試行できなくなるので外す(無効化で別の枠に替わっていれば触らない)。
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(key);
        if (it != entries_.end() && it->second == slot) entries_.erase(it);
    }
}

std::shared_ptr<const rs::legacy_plugin::LoadedPlugin> ParsedPluginCache::EnsureParsed(
    const PluginSource& source, bool* cacheHit, const rs::legacy_plugin::LoadOptions& options) {
    const std::string key = ParseKey(source, options);
    bool owner = false;
    const std::shared_ptr<Slot> slot = AcquireSlot(key, owner);
    if (cacheHit != nullptr) *cacheHit = !owner;
    if (owner) FillSlot(key, slot, source, options);
    return slot->future.get();
}

bool ParsedPluginCache::LoadInto(PluginSession& session, const PluginSource& source,
                                 std::vector<std::string>& log, float pierHeightOverrideM) {
    bool cacheHit = false;
    rs::legacy_plugin::LoadOptions options;
    options.pierHeightOverrideM = pierHeightOverrideM;
    const ParsedPtr parsed = EnsureParsed(source, &cacheHit, options);
    session.LoadFromParsed(*parsed, source.folderUtf8);
    // プリフェッチ経由の登録も含め、ヒット・ミスのどちらでも読込のログを伝える。
    log.insert(log.end(), parsed->log.begin(), parsed->log.end());
    return cacheHit;
}

void ParsedPluginCache::RequestPrefetch(const std::vector<PluginSource>& sourceList) {
    std::shared_ptr<std::vector<PluginSource>> jobs = std::make_shared<std::vector<PluginSource>>();
    jobs->reserve(sourceList.size());
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const PluginSource& source : sourceList) {
            if (entries_.contains(ParseKey(source, {}))) continue;
            bool queued = false;
            for (const PluginSource& existing : *jobs) queued = queued || existing == source;
            if (!queued) jobs->push_back(source);
        }
    }
    if (jobs->empty()) return;

    // 前の要求のジョブとsourceが重なっても、ワーカー側のAcquireSlotで1回だけ解析される。
    std::shared_ptr<std::atomic<size_t>> remaining = std::make_shared<std::atomic<size_t>>(jobs->size());
    PrefetchBatch batch;
    batch.remaining = remaining;
    batch.jobs = rs::platform_thread::StartParallelFor(jobs->size(), [this, jobs, remaining](size_t idx) {
        if (!cancelPrefetch_.load(std::memory_order_acquire)) {
            // 呼んでよいのはLoadPluginFolder系(ParsePluginSource)のみ(PluginSession::LoadFolder/
            // BuildRuntimes/ApplySwitchOptionsAndBakeは呼ばない。これがスレッド境界)。
            const PluginSource& source = (*jobs)[idx];
            const std::string key = ParseKey(source, {});
            bool owner = false;
            const std::shared_ptr<Slot> slot = AcquireSlot(key, owner);
            // 別スレッドが担当なら待たずに抜ける(先読みは結果を受け取らない)。
            if (owner) FillSlot(key, slot, source, {});
        }
        remaining->fetch_sub(1, std::memory_order_acq_rel);
    });
    prefetchJobs_.push_back(std::move(batch));
}

void ParsedPluginCache::HarvestCompletedPrefetch() {
    std::erase_if(prefetchJobs_, [](PrefetchBatch& batch) {
        if (batch.remaining->load(std::memory_order_acquire) != 0) return false;
        batch.jobs.Join();
        return true;
    });
}

} // namespace rs::app
