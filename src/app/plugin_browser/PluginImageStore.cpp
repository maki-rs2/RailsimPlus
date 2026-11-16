#include "PluginImageStore.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace rs::app::plugin_browser {

ImageState PluginImageStore::State(ImageKey key) const {
    const std::unordered_map<ImageKey, Slot>::const_iterator it = slots_.find(key);
    return it == slots_.end() ? ImageState::None : it->second.state;
}

const rs::render::ImageData* PluginImageStore::Find(ImageKey key) const {
    const std::unordered_map<ImageKey, Slot>::const_iterator it = slots_.find(key);
    if (it == slots_.end() || !HoldsImage(it->second.state)) return nullptr;
    return &it->second.image;
}

void PluginImageStore::Reset() {
    slots_.clear();
    lru_.clear();
    lastVisible_.clear();
}

void PluginImageStore::Touch(Slot& slot) {
    lru_.splice(lru_.begin(), lru_, slot.lruPos);
}

void PluginImageStore::SetImage(ImageKey key, Slot& slot, ImageState state, rs::render::ImageData&& image) {
    const bool hadImage = HoldsImage(slot.state);
    slot.state = state;
    slot.image = std::move(image);
    if (hadImage) {
        Touch(slot);
    } else {
        lru_.push_front(key);
        slot.lruPos = lru_.begin();
    }
}

// 上限を超えた分を、使っていない順に押し出す。押し出したスロットは、失敗の印(iconFailed・
// 失敗したサムネイルのthumbFinished)を残したままRGBAだけ手放してNoneへ戻す。keepは今入れたもの(押し出さない)。
void PluginImageStore::EvictOverflow(ImageKey keep) {
    while (lru_.size() > maxCachedImages_) {
        const ImageKey victim = lru_.back();
        if (victim == keep) break; // 上限が1件のときなど、入れたものしか無い
        lru_.pop_back();
        Slot& slot = slots_[victim];
        if (slot.state == ImageState::Thumbnail) slot.thumbFinished = false; // 撮れていたものは、見えたら撮り直す
        slot.state = ImageState::None;
        slot.image = rs::render::ImageData{};
        slot.thumbPending = false;
    }
}

void PluginImageStore::PostRequest(ImageKey key, const PluginCatalogEntry& entry, std::uint64_t generation,
                                   ImageKind kind, IPluginImagePoster& poster) {
    PluginImageRequest request;
    request.id = key.id;
    request.view = key.view;
    request.generation = generation;
    request.kind = kind;
    request.folderUtf8 = entry.folderUtf8;
    request.iconPathUtf8 = entry.iconPathUtf8;
    std::copy(std::begin(entry.iconRect), std::end(entry.iconRect), std::begin(request.iconRect));
    request.browserKind = entry.kind;
    poster.Post(std::move(request), kind == ImageKind::Icon ? kIconPriority : kThumbnailPriority);
}

void PluginImageStore::Request(const std::vector<EntryId>& visible, const PluginCatalog& catalog,
                               IPluginImagePoster& poster) {
    const std::vector<PluginCatalogEntry>& entries = catalog.Entries();
    const std::uint64_t generation = catalog.Generation();

    if (visible != lastVisible_) {
        // 窓が動いた。未処理の受付を捨て、要求中だった印を下ろす(下ろさないと、捨てた受付の
        // 結果が来ないまま要求済み扱いで残る)。
        poster.ClearPending();
        for (std::pair<const ImageKey, Slot>& item : slots_) {
            Slot& slot = item.second;
            if (slot.state == ImageState::Requested) slot.state = ImageState::None;
            slot.thumbPending = false;
        }
        lastVisible_ = visible;
    }

    for (const EntryId id : visible) {
        if (id >= entries.size()) continue;
        const PluginCatalogEntry& entry = entries[id];
        Slot& slot = slots_[id];
        if (slot.state == ImageState::Failed) continue;
        if (HoldsImage(slot.state)) Touch(slot); // 見えているものは押し出されないよう、使った扱いにする

        if (slot.state == ImageState::None) {
            const bool useIcon = !entry.iconPathUtf8.empty() && !slot.iconFailed;
            if (useIcon) {
                PostRequest(id, entry, generation, ImageKind::Icon, poster);
            } else {
                PostRequest(id, entry, generation, ImageKind::Thumbnail, poster);
                slot.thumbPending = true;
            }
            slot.state = ImageState::Requested;
        } else if (slot.state == ImageState::Icon && !slot.thumbPending && !slot.thumbFinished) {
            PostRequest(id, entry, generation, ImageKind::Thumbnail, poster);
            slot.thumbPending = true;
        }
    }
}

void PluginImageStore::RequestViews(const std::vector<ImageKey>& keys, const PluginCatalog& catalog,
                                    IPluginImagePoster& poster) {
    const std::vector<PluginCatalogEntry>& entries = catalog.Entries();
    const std::uint64_t generation = catalog.Generation();
    for (const ImageKey& key : keys) {
        if (key.id >= entries.size()) continue;
        Slot& slot = slots_[key];
        if (slot.state == ImageState::Failed) continue;
        if (HoldsImage(slot.state)) Touch(slot); // 見えているものは押し出されないよう、使った扱いにする
        if (slot.state == ImageState::Thumbnail || slot.thumbPending || slot.thumbFinished) continue;
        PostRequest(key, entries[key.id], generation, ImageKind::Thumbnail, poster);
        slot.thumbPending = true;
        if (slot.state == ImageState::None) slot.state = ImageState::Requested;
    }
}

std::vector<EntryId> PluginImageStore::Accept(std::vector<PluginImageResult> results, std::uint64_t generation) {
    std::vector<EntryId> replaced;
    for (PluginImageResult& result : results) {
        if (result.generation != generation) continue;
        const ImageKey key(result.id, result.view);
        Slot& slot = slots_[key];

        if (result.kind == ImageKind::Icon) {
            if (result.ok) {
                // すでにサムネイルがある(サムネイルのほうが良い)か、同じアイコンが来ている(窓が動いて
                // 出し直した分の重複)なら捨てる。
                if (HoldsImage(slot.state)) continue;
                SetImage(key, slot, ImageState::Icon, std::move(result.image));
                EvictOverflow(key);
            } else {
                // アイコン無し扱い。次のRequestでサムネイルを要求できるようNoneへ戻す。
                slot.iconFailed = true;
                if (slot.state == ImageState::Requested) slot.state = ImageState::None;
            }
            continue;
        }

        slot.thumbPending = false;
        slot.thumbFinished = true;
        if (result.ok) {
            if (slot.state == ImageState::Thumbnail) continue; // 重複
            const bool hadImage = HoldsImage(slot.state);
            SetImage(key, slot, ImageState::Thumbnail, std::move(result.image));
            if (hadImage) replaced.push_back(key.id);
            EvictOverflow(key);
        } else if (!HoldsImage(slot.state)) {
            slot.state = ImageState::Failed; // アイコンが無ければ画像が1枚も無い
        }
    }
    return replaced;
}

} // namespace rs::app::plugin_browser
