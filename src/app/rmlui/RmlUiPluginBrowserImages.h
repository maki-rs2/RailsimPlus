//
// RmlUiPluginBrowserImages.h - プラグインブラウザのカードの画像を、RmlUiのテクスチャへ結ぶ
//
// RmlUiPluginBrowserViewが毎フレーム呼ぶ。
//   - 見えている行のエントリの画像を要求する(PluginImageStore::Request)
//   - カードのimageへ入れるソース名を決める。RGBAを持っているカードだけ"pbimg:..."を出し、
//     画像を1枚も作れなかったカードにはフォールバック画像のパスを出し、それ以外は空にする
//     (RmlUiは読込の失敗を覚えて解放するまで読み直さないので、RGBAが無いうちは名前を出さない)
//   - 差し替わった画像、窓のGPU範囲から外れた画像、世代が変わった画像のテクスチャを
//     Rml::ReleaseTextureで解放する(CPU側のRGBAはストアに残る)
//
#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "../plugin_browser/BrowserFrameBudget.h"
#include "../plugin_browser/PluginBrowserState.h"

namespace rs::app::plugin_browser {
class PluginBrowserService;
} // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

class RmlUiPluginBrowserImages {
public:
    // 窓が決まったあと、カードを作る前に呼ぶ。itemsはグリッドの項目(フォルダの項目は画像を持たないので無視する)、windowはその窓。
    void Update(plugin_browser::PluginBrowserService& service, const std::vector<plugin_browser::EntryId>& items,
                const plugin_browser::BrowserWindow& window);

    // カードのimageに入れるソース名(出さないときは空)。Updateのあとに、窓の作る範囲のカードだけに使う。
    std::string SourceFor(const plugin_browser::PluginBrowserService& service, plugin_browser::EntryId id);

    // 前のフレームで測ったテクスチャ作成の合計時間(ms)と枚数を渡す(毎フレーム、Updateの前に)。
    // 次のフレームに新しく見せる画像の枚数が、1枚あたりの平均時間から決まる。
    void ObserveTextureLoads(double totalMs, std::size_t count);

    // 出したことのあるソース名のテクスチャを全部解放する。
    void ReleaseAll();

private:
    void Release(const std::string& source) const;
    // 差し替わった画像のテクスチャを、枠の残りの分だけ解放する(同じ名前で読み直させる)。
    void ReleaseReplaced(std::size_t& quota);
    // 枠の残りの分だけ、まだ見せていない画像(RGBAを持つもの)を今のフレームで見せてよいことにする。
    void GrantNewImages(const plugin_browser::PluginBrowserService& service,
                        const std::vector<plugin_browser::EntryId>& ids, std::size_t& quota);

    // 出したことのある"pbimg:..."のソース名。ReleaseTextureのキーにそのまま使う。
    std::unordered_map<plugin_browser::EntryId, std::string> emitted_;
    // 1フレームに新しく見せる(初めてソース名を出す、または読み直させる)画像の上限。
    std::size_t newImageLimit_ = plugin_browser::kInitialNewImagesPerFrame;
    // 今のフレームで初めてソース名を出してよいと決めた画像。
    std::unordered_set<plugin_browser::EntryId> granted_;
    // 差し替わって、まだテクスチャを解放していない画像(枠が空くまで待つ)。
    std::vector<plugin_browser::EntryId> pendingReload_;
    // フォールバック画像のパス(絶対パス。初回に解決する)。
    std::string fallbackPath_;
    bool fallbackResolved_ = false;
};

} // namespace rs::app::rmlui
