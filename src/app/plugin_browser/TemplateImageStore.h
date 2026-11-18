//
// TemplateImageStore.h - テンプレートのカード(編成・敷設)の画像の置き場と要求の管理
//
// RmlUi非依存、メインスレッド専用。
// 見せたいテンプレートを毎フレーム受け取り、撮影済みの画像が無いものだけワーカーへ要求する。
// 識別子(キー)は、テンプレートのファイルと、中で参照するプラグインの定義ファイルの更新時刻から作るので、
// 中身が変わると別のキーになり、別の画像として撮り直す。画像はキーごとに件数上限のLRUで持つ。
// RmlUiのテクスチャの名前は "tmimg:<キー>"(キーが同じなら画像は変わらないので、読み直されない)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "../../render/resource/TextureLoader.h"
#include "../plugin_session/TrainGroupTemplateParse.h"
#include "PluginImageWorker.h"

namespace rs::app::plugin_browser {

enum class TemplateImageKind { Train, Rail };

// 撮るテンプレート1本。pathUtf8 はテンプレートのファイル、rootUtf8 はプラグインのルート(空ならファイルの親の親)。
struct TemplateImageSource {
    TemplateImageKind kind = TemplateImageKind::Train;
    std::string pathUtf8;
    std::string rootUtf8;
};

using TemplateImageKey = std::uint64_t;

// テンプレートのファイルと、参照するプラグインの定義ファイルの更新時刻から作る識別子。
// ファイルを読むので、毎フレームは呼ばず、テンプレートの一覧を読み直したときだけ呼ぶこと。
TemplateImageKey MakeTemplateImageKey(const TemplateImageSource& source);
// 編成テンプレートのキー。読み込み済みの結果を渡すので、同じファイルを2回パースせずに済む。
// MakeTemplateImageKey(kind=Train)と同じ値になる。
TemplateImageKey MakeTrainTemplateImageKey(const TemplateImageSource& source,
                                           const TrainGroupTemplateParseResult& parsed);

// RmlUiのテクスチャの名前。キーと相互に変換する。名前でなければ nullopt。
std::string MakeTemplateImageName(TemplateImageKey key);
std::optional<TemplateImageKey> ParseTemplateImageName(const std::string& name);

// LRUに持つ画像の件数。
inline constexpr std::size_t kMaxCachedTemplateImages = 64;
// 撮影を受け付けてもらっている(結果待ちの)件数の上限。残りは、結果が届いた次のフレームで出す。
inline constexpr std::size_t kMaxPendingTemplateImages = 8;

struct WantedTemplateImage {
    TemplateImageKey key = 0;
    TemplateImageSource source;
};

class TemplateImageStore {
public:
    explicit TemplateImageStore(std::size_t maxCached = kMaxCachedTemplateImages) : maxCached_(maxCached) {}

    // 見せたいテンプレートを、見せたい順に渡す。画像を持っていない・要求中でない・失敗していないものを、
    // 結果待ちが kMaxPendingTemplateImages 件になるまで要求する。
    void Request(const std::vector<WantedTemplateImage>& wanted, IPluginImagePoster& poster);

    // ワーカーの結果を取り込む。失敗したキーは以後出し直さない。
    void Accept(const PluginImageResult& result);

    // キーの画像。まだ無い・失敗したときは nullptr。次にAcceptを呼ぶまで有効。
    const rs::render::ImageData* Find(TemplateImageKey key) const;
    // "tmimg:<キー>" が指す画像。
    const rs::render::ImageData* FindByName(const std::string& name) const;

private:
    struct Cached {
        TemplateImageKey key = 0;
        rs::render::ImageData image;
    };

    std::size_t maxCached_;
    std::list<Cached> lru_; // 先頭が最近使ったもの(Findでは並べ替えない。入れた順に古いものから捨てる)
    std::set<TemplateImageKey> pending_;
    std::set<TemplateImageKey> failed_;
};

} // namespace rs::app::plugin_browser
