//
// RmlUiFormationImages.h - 編成カードの車両の絵・先頭アイコンの画像を、RmlUiのテクスチャへ結ぶ
//
// ブラウザのカード用(RmlUiPluginBrowserImages)とは別の名前(変種つき)で出すので、片方の解放が
// もう片方のテクスチャを巻き込まない。1フレームの流れは BeginFrame → Want(カードの車両ごと) → EndFrame。
// インスタンスは1つを編成リストと編成情報カードで共有し、1フレームに1回だけ BeginFrame/EndFrame で囲む
// (インスタンスを分けると同じソース名を互いに解放してしまう)。
//   - Want は車両のフォルダと変種からキーを決めて「見せたい」と覚え、サムネイルのRGBAが届いていれば
//     ソース名を返す(届くまでは空。カードは車両名の箱のまま)
//   - EndFrame は見せたいキーをまとめて要求し、見せなくなったもの・RGBAを手放したものを解放する
// フォルダ→EntryIdの引き直しはファイルシステムに触るため、カタログの更新までは覚えておく。
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../plugin_browser/ThumbnailView.h"

namespace rs::app::plugin_browser {
class PluginBrowserService;
} // namespace rs::app::plugin_browser

namespace rs::app::rmlui {

class RmlUiFormationImages {
public:
    void BeginFrame(plugin_browser::PluginBrowserService& service);
    std::string Want(const std::string& folderUtf8, plugin_browser::ThumbnailView view);
    // 編成の先頭車のアイコン(進行方向の前面)を見せたいと覚える。先頭車が反転していれば元の車体の後ろ側が
    // 前面になるので、反対側から撮った変種を選ぶ。編成リストのカードと編成情報カードが同じ選び方を使う。
    std::string WantHeadIcon(const std::string& headFolderUtf8, bool headReversed);
    void EndFrame();

    // 出したソース名のテクスチャを全部解放する(編成一覧が見えなくなったとき)。
    void ReleaseAll();

private:
    std::optional<plugin_browser::EntryId> EntryOf(const std::string& folderUtf8);

    plugin_browser::PluginBrowserService* service_ = nullptr;
    std::uint64_t generation_ = 0;
    std::uint64_t revision_ = 0;
    std::unordered_map<std::string, std::optional<plugin_browser::EntryId>> entryByFolder_;
    std::unordered_map<plugin_browser::ImageKey, std::string> emitted_;
    std::vector<plugin_browser::ImageKey> wanted_;
    std::unordered_set<plugin_browser::ImageKey> wantedSet_;
};

} // namespace rs::app::rmlui
