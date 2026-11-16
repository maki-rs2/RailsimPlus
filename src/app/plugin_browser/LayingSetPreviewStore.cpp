#include "LayingSetPreviewStore.h"

#include <charconv>
#include <filesystem>
#include <functional>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginPaths.h"
#include "../scene/ParsedPluginCache.h"

namespace rs::app::plugin_browser {
namespace {

constexpr std::string_view kImagePrefix = "lsimg:";
// 通し番号の記録の上限(撮影待ちが捨てられると、その記録は答えが来ないまま残るため)。
constexpr std::size_t kMaxSerialRecords = 16;

void Mix(std::uint64_t& seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

} // namespace

LayingPreviewKey MakeLayingPreviewKey(const std::vector<std::string>& folders) {
    std::uint64_t key = GetParsedPluginCache().Generation();
    for (const std::string& folder : folders) {
        Mix(key, std::hash<std::string>()(folder));
        const std::vector<std::wstring> definitions =
            rs::legacy_plugin::FindDefinitionFiles(rs::platform_text::Utf8ToWide(folder));
        for (const std::wstring& path : definitions) {
            std::error_code error;
            const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(path, error);
            Mix(key, error ? 0u : static_cast<std::uint64_t>(writeTime.time_since_epoch().count()));
        }
    }
    return key;
}

std::string MakeLayingImageName(std::uint64_t imageNumber) {
    return std::string(kImagePrefix) + std::to_string(imageNumber);
}

std::optional<std::uint64_t> ParseLayingImageName(const std::string& name) {
    const std::string_view view(name);
    if (!view.starts_with(kImagePrefix)) return std::nullopt;
    const std::string_view digits = view.substr(kImagePrefix.size());
    std::uint64_t number = 0;
    const std::from_chars_result result = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (result.ec != std::errc() || result.ptr != digits.data() + digits.size()) return std::nullopt;
    return number;
}

std::list<LayingSetPreviewStore::CachedImage>::iterator LayingSetPreviewStore::FindCached(LayingPreviewKey key) {
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        if (it->key == key) return it;
    }
    return lru_.end();
}

void LayingSetPreviewStore::Show(LayingPreviewKey key, const rs::render::ImageData& image) {
    shown_ = image;
    hasShown_ = true;
    shownKey_ = key;
    ++imageNumber_;
}

void LayingSetPreviewStore::Want(LayingPreviewKey key, IPluginImagePoster& poster, PluginImageRequest request) {
    wanted_ = key;
    if (shownKey_ == key) return;
    const std::list<CachedImage>::iterator cached = FindCached(key);
    if (cached != lru_.end()) {
        lru_.splice(lru_.begin(), lru_, cached);
        Show(key, cached->image);
        return;
    }
    if (failed_.contains(key) || posted_ == key) return;

    poster.ClearPendingLayingSet();
    if (serialKeys_.size() >= kMaxSerialRecords) serialKeys_.erase(serialKeys_.begin());
    request.kind = ImageKind::LayingSet;
    request.layingSerial = nextSerial_++;
    serialKeys_[request.layingSerial] = key;
    posted_ = key;
    poster.Post(std::move(request), kLayingSetPreviewPriority);
}

void LayingSetPreviewStore::Accept(const PluginImageResult& result) {
    const auto record = serialKeys_.find(result.layingSerial);
    if (record == serialKeys_.end()) return;
    const LayingPreviewKey key = record->second;
    // 結果は出来上がった順(=要求の順)に届くので、これより古い記録は答えが来ない(捨てられた)もの。
    serialKeys_.erase(serialKeys_.begin(), std::next(record));
    if (posted_ == key) posted_.reset();

    if (!result.ok || !result.image.ok) {
        failed_.insert(key);
        return;
    }
    const std::list<CachedImage>::iterator existing = FindCached(key);
    if (existing != lru_.end()) lru_.erase(existing);
    lru_.push_front(CachedImage{key, result.image});
    while (lru_.size() > maxCached_) lru_.pop_back();
    if (wanted_ == key) Show(key, result.image);
}

const rs::render::ImageData* LayingSetPreviewStore::FindByName(const std::string& name) const {
    const std::optional<std::uint64_t> number = ParseLayingImageName(name);
    if (!number.has_value() || !hasShown_ || *number != imageNumber_) return nullptr;
    return &shown_;
}

} // namespace rs::app::plugin_browser
