#include "RmlUiPluginBrowserImages.h"

#include <algorithm>
#include <unordered_set>

#include <RmlUi/Core/Core.h>

#include <spdlog/spdlog.h>

#include "../../platform/file/AssetPath.h"
#include "../plugin_browser/BrowserFrameBudget.h"
#include "../plugin_browser/PluginBrowserService.h"
#include "../plugin_browser/PluginImageSource.h"

namespace rs::app::rmlui {

using rs::app::plugin_browser::BrowserWindow;
using rs::app::plugin_browser::CardRange;
using rs::app::plugin_browser::EntryId;
using rs::app::plugin_browser::ImageState;
using rs::app::plugin_browser::PluginBrowserService;

namespace {

// 画像を1枚も作れなかったカードに共通で出す画像(ソースツリー相対)。
constexpr const char* kFallbackImagePath = "assets/ui/browser/thumbnail_fallback.png";

// 範囲[first, last)のエントリをidsへ足す(フォルダの項目は飛ばす)。
void CollectIds(const std::vector<EntryId>& items, const CardRange& range, std::vector<EntryId>& ids) {
    for (std::size_t i = range.first; i < range.last && i < items.size(); ++i) {
        if (!plugin_browser::IsFolderItem(items[i])) ids.push_back(items[i]);
    }
}

} // namespace

void RmlUiPluginBrowserImages::Release(const std::string& source) const {
    Rml::ReleaseTexture(source); // レンダラは1つだけなので、全レンダラから探してよい
}

void RmlUiPluginBrowserImages::ReleaseAll() {
    for (const std::pair<const EntryId, std::string>& item : emitted_) Release(item.second);
    emitted_.clear();
}

void RmlUiPluginBrowserImages::Update(PluginBrowserService& service, const std::vector<EntryId>& items,
                                      const BrowserWindow& window) {
    const std::uint64_t generation = service.Catalog().Generation();

    // 世代が変わったら、前の世代の名前で出したものを全部解放する(ストアはService側で空になっている)。
    for (std::unordered_map<EntryId, std::string>::iterator it = emitted_.begin(); it != emitted_.end();) {
        const std::optional<plugin_browser::PluginImageSource> parsed = plugin_browser::ParseImageSourceName(it->second);
        if (!parsed.has_value() || parsed->generation != generation) {
            Release(it->second);
            it = emitted_.erase(it);
        } else {
            ++it;
        }
    }

    // 差し替わった画像は、同じ名前のままRGBAが変わったので、テクスチャを解放して読み直させる。
    // 読み直しも新しく見せる画像と同じ枠を使うので、解放は枠の中で後から行う(ReleaseReplaced)。
    for (const EntryId id : service.TakeReplacedImages()) {
        if (emitted_.count(id) != 0 && std::find(pendingReload_.begin(), pendingReload_.end(), id) == pendingReload_.end()) {
            pendingReload_.push_back(id);
        }
    }

    // 見えている行の画像を要求する。
    std::vector<EntryId> visible;
    CollectIds(items, window.visible, visible);
    service.Images().Request(visible, service.Catalog(), service.ImageWorker());

    // 作る範囲とGPUに置く範囲の外に出たもの、CPU側のRGBAを手放したものを解放する。
    std::vector<EntryId> keepList;
    CollectIds(items, window.build, keepList);
    CollectIds(items, window.resident, keepList);
    const std::unordered_set<EntryId> keep(keepList.begin(), keepList.end());
    for (std::unordered_map<EntryId, std::string>::iterator it = emitted_.begin(); it != emitted_.end();) {
        if (keep.count(it->first) == 0 || !service.Images().HasImage(it->first)) {
            Release(it->second);
            it = emitted_.erase(it);
        } else {
            ++it;
        }
    }

    // 新しく見せる枠を、読み直し、見えている行、作る範囲の順に配る(各範囲は上から順)。
    std::size_t quota = newImageLimit_;
    ReleaseReplaced(quota);
    granted_.clear();
    GrantNewImages(service, visible, quota);
    std::vector<EntryId> buildIds;
    CollectIds(items, window.build, buildIds);
    GrantNewImages(service, buildIds, quota);
}

void RmlUiPluginBrowserImages::ReleaseReplaced(std::size_t& quota) {
    std::vector<EntryId>::iterator it = pendingReload_.begin();
    for (; it != pendingReload_.end() && quota > 0; ++it) {
        const std::unordered_map<EntryId, std::string>::const_iterator found = emitted_.find(*it);
        if (found == emitted_.end()) continue; // 窓から外れて解放済み
        Release(found->second);
        --quota;
    }
    pendingReload_.erase(pendingReload_.begin(), it);
}

void RmlUiPluginBrowserImages::GrantNewImages(const PluginBrowserService& service, const std::vector<EntryId>& ids,
                                              std::size_t& quota) {
    for (const EntryId id : ids) {
        if (quota == 0) return;
        if (emitted_.count(id) != 0 || granted_.count(id) != 0) continue;
        if (!service.Images().HasImage(id)) continue;
        granted_.insert(id);
        --quota;
    }
}

void RmlUiPluginBrowserImages::ObserveTextureLoads(double totalMs, std::size_t count) {
    // 作らなかったフレームは測れないので、前の値のままにする。
    if (count > 0) newImageLimit_ = plugin_browser::NewImagesForNextFrame(totalMs, count);
}

std::string RmlUiPluginBrowserImages::SourceFor(const PluginBrowserService& service, EntryId id) {
    const plugin_browser::PluginImageStore& images = service.Images();
    if (images.HasImage(id)) {
        // 初めて見せる画像は、この枠を配られたものだけ(残りは次のフレームへ回し、空のままにする)。
        if (emitted_.count(id) == 0 && granted_.count(id) == 0) return {};
        std::string& source = emitted_[id];
        if (source.empty()) {
            source = plugin_browser::MakeImageSourceName({service.Catalog().Generation(), id, plugin_browser::ImageSurface::BrowserGrid});
        }
        return source;
    }
    if (images.State(id) != ImageState::Failed) return {};

    // 共有の1枚。解放は要らない(カードごとの名前ではない)。
    if (!fallbackResolved_) {
        fallbackResolved_ = true;
        fallbackPath_ = rs::platform_file::ResolveAssetPath(kFallbackImagePath);
        if (fallbackPath_.empty()) spdlog::warn("[rmlui] フォールバック画像が見つかりません: {}", kFallbackImagePath);
    }
    return fallbackPath_;
}

} // namespace rs::app::rmlui
