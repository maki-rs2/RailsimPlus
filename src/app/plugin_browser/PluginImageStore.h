//
// PluginImageStore.h - カードの画像(アイコン・サムネイル)のCPU側の置き場と要求の管理
//
// RmlUi非依存、メインスレッド専用。
// エントリごとの状態(None/Requested/Icon/Thumbnail/Failed)とRGBAを持ち、見えているエントリに
// ついてだけ PluginImageWorker(の窓口 IPluginImagePoster)へ要求を出す。
// キーは(EntryId, 撮り方の変種)。ブラウザのカードは Standard、編成カードの車両の絵(Side・SideOpposite)・先頭アイコンは
// 変種ごとの別のスロットで、アイコンは持たずサムネイルだけを要求する。
// RGBAは件数上限のLRUで持ち、押し出したエントリはNoneへ戻す(見えたら要求し直す)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "../../render/resource/TextureLoader.h"
#include "PluginCatalog.h"
#include "PluginImageWorker.h"
#include "ThumbnailView.h"

namespace rs::app::plugin_browser {

enum class ImageState {
    None,       // 何も持っていない・要求していない
    Requested,  // 最初の画像(アイコン、無ければサムネイル)を要求中
    Icon,       // アイコンのRGBAを持っている
    Thumbnail,  // サムネイルのRGBAを持っている
    Failed,     // 画像を1枚も作れなかった(セッション中は要求し直さない)
};

// CPU側に持つ画像の件数の上限(サムネイル256x192なら約100MB)。
inline constexpr std::size_t kMaxCachedImages = 512;

class PluginImageStore {
public:
    explicit PluginImageStore(std::size_t maxCachedImages = kMaxCachedImages) : maxCachedImages_(maxCachedImages) {}

    // 窓の「見えている行」のエントリを渡す。見えている列が前回と変わったら、窓から外れた未処理の
    // 受付が無駄にならないよう poster.ClearPending() して、要求中だったものをNoneへ戻してから
    // 出し直す。generationはcatalog.Generation()を使う。
    void Request(const std::vector<EntryId>& visible, const PluginCatalog& catalog, IPluginImagePoster& poster);

    // ワーカーの結果を取り込む。generationが違う結果は捨てる。戻り値は、すでにRGBAを持っていたところへ
    // 新しいRGBAが来た(差し替わった)エントリ(呼び出し側がRmlUiのテクスチャを解放するため)。
    // 差し替わるのはアイコンからサムネイルへの置き換えだけなので、戻り値は Standard のエントリだけになる。
    std::vector<EntryId> Accept(std::vector<PluginImageResult> results, std::uint64_t generation);

    // 編成カードが見せたい画像(変種つき)のサムネイルを要求する。Requestと違い、窓の変化では未処理の受付を
    // 捨てない(ブラウザの窓の要求と打ち消し合わないため)。毎フレーム、ブラウザのRequestのあとに呼ぶ。
    // 撮影中・撮影待ちのキーには出し直さない。Standard で、ブラウザの窓がアイコンだけを持っているキーも
    // サムネイルを要求する。
    void RequestViews(const std::vector<ImageKey>& keys, const PluginCatalog& catalog, IPluginImagePoster& poster);

    ImageState State(ImageKey key) const;
    // RGBAを持っていればそのポインタ(Icon/Thumbnailのとき)。次にRequest/Acceptを呼ぶまで有効。
    const rs::render::ImageData* Find(ImageKey key) const;
    bool HasImage(ImageKey key) const { return Find(key) != nullptr; }
    // サムネイルのRGBAを持っているか(アイコンだけのときは false)。
    bool HasThumbnail(ImageKey key) const { return State(key) == ImageState::Thumbnail; }

    // 世代が変わったとき。全部捨てる。
    void Reset();

private:
    struct Slot {
        ImageState state = ImageState::None;
        rs::render::ImageData image;
        bool iconFailed = false;     // アイコンを読めなかった(サムネイルを要求する)
        bool thumbPending = false;   // サムネイルを要求中
        bool thumbFinished = false;  // サムネイルの結果を受け取った(成功でも失敗でも。再要求しない)
        std::list<ImageKey>::iterator lruPos; // RGBAを持つときだけ有効
    };

    static bool HoldsImage(ImageState state) { return state == ImageState::Icon || state == ImageState::Thumbnail; }
    void SetImage(ImageKey key, Slot& slot, ImageState state, rs::render::ImageData&& image);
    void Touch(Slot& slot);
    void EvictOverflow(ImageKey keep);
    static void PostRequest(ImageKey key, const PluginCatalogEntry& entry, std::uint64_t generation, ImageKind kind,
                            IPluginImagePoster& poster);

    std::size_t maxCachedImages_;
    std::unordered_map<ImageKey, Slot> slots_;
    std::list<ImageKey> lru_;            // 先頭が最近使ったもの。RGBAを持つスロットだけ
    std::vector<EntryId> lastVisible_;
};

} // namespace rs::app::plugin_browser
