//
// LayingSetPreviewStore.h - 敷設プラグインセットの合成プレビュー(1枚の画像)の置き場と要求の管理
//
// RmlUi非依存、メインスレッド専用。
// 「いま出したい合成」をキーで受け取り、撮影済みの画像があればすぐ切り替え、無ければワーカーへ撮影を要求する。
// 結果が届くまでは、前の画像を出し続ける。撮影の受付は最大1件で、新しいキーが来たら古い待ちを捨てる。
// 画像の中身が変わるたびに番号を進め、RmlUiのテクスチャの名前("lsimg:<番号>")へ写す
// (番号が変われば別のテクスチャとして読み直される)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../render/resource/TextureLoader.h"
#include "PluginImageWorker.h"

namespace rs::app::plugin_browser {

// 合成の中身(フォルダの列・各フォルダの定義ファイルの更新時刻・読込結果の世代)から作る識別子。
using LayingPreviewKey = std::uint64_t;

// フォルダの列から、合成のキーを作る。定義ファイルの更新時刻を調べるので、毎フレームは呼ばず、
// フォルダの列か読込結果の世代が変わったときだけ呼ぶこと。
LayingPreviewKey MakeLayingPreviewKey(const std::vector<std::string>& folders);

// RmlUiのテクスチャの名前。画像の番号と相互に変換する。名前でなければ nullopt。
std::string MakeLayingImageName(std::uint64_t imageNumber);
std::optional<std::uint64_t> ParseLayingImageName(const std::string& name);

// LRUに持つ合成の件数。
inline constexpr std::size_t kMaxCachedLayingPreviews = 8;

class LayingSetPreviewStore {
public:
    explicit LayingSetPreviewStore(std::size_t maxCached = kMaxCachedLayingPreviews) : maxCached_(maxCached) {}

    // いま出したい合成を渡す。request は layingFolders・layingHeights だけ埋めておく
    // (種類・通し番号はここで決める)。
    //   - 今出している画像と同じキー: 何もしない
    //   - LRUにある: すぐ切り替える
    //   - 失敗済み・撮影中または撮影待ちのキー: 出し直さない
    //   - それ以外: poster.ClearPendingLayingSet() で古い待ちを捨ててから要求する
    void Want(LayingPreviewKey key, IPluginImagePoster& poster, PluginImageRequest request);

    // ワーカーの結果を取り込む。古い要求の結果もLRUへ入れ、表示を切り替えるのは、いま出したいキーと
    // 一致したときだけ。失敗したキーは以後出し直さない。
    void Accept(const PluginImageResult& result);

    // いま出している画像。まだ無ければ nullptr。
    const rs::render::ImageData* Current() const { return hasShown_ ? &shown_ : nullptr; }
    // 出している画像の番号(中身が変わるたびに進む。まだ無いときは0)。
    std::uint64_t ImageNumber() const { return hasShown_ ? imageNumber_ : 0; }
    // "lsimg:<番号>" が今の画像を指しているときだけ、その画像。
    const rs::render::ImageData* FindByName(const std::string& name) const;

private:
    struct CachedImage {
        LayingPreviewKey key = 0;
        rs::render::ImageData image;
    };

    void Show(LayingPreviewKey key, const rs::render::ImageData& image);
    std::list<CachedImage>::iterator FindCached(LayingPreviewKey key);

    std::size_t maxCached_;
    std::optional<LayingPreviewKey> wanted_;
    std::optional<LayingPreviewKey> shownKey_;
    rs::render::ImageData shown_;
    bool hasShown_ = false;
    std::uint64_t imageNumber_ = 0;
    std::optional<LayingPreviewKey> posted_; // 撮影中または撮影待ち(結果が届くまで)
    std::uint64_t nextSerial_ = 1;
    std::map<std::uint64_t, LayingPreviewKey> serialKeys_; // 通し番号から、要求したキー
    std::set<LayingPreviewKey> failed_;
    std::list<CachedImage> lru_; // 先頭が最近使ったもの
};

} // namespace rs::app::plugin_browser
