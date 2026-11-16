//
// MissingPluginViewModel.h - 見つからないプラグインの画面に出す値を作る純粋関数
//
// MissingPluginAction::Collectの結果(行)、Replace/Deleteの結果(通知文言)、差し替えのフォルダ選択の
// 最初の場所を、画面(RmlUi)が表示できる文字列と数にする。文言は翻訳キー(assets/locale/strings.json)
// から引く。状態は持たず、ファイルシステムにも触れない(フォルダの有無は関数で受ける)。
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "MissingPluginAction.h"
#include "../locale/Localization.h"
#include "../plugin_session/MissingPluginCatalog.h"

namespace rs::app {

// 一覧の1行ぶんの表示値。
struct MissingPluginRowView {
    std::string name;     // フォルダの最後の名前
    std::string path;     // フルパス(マウスを載せたときに出す)
    std::string kind;     // 場所の種類の名前
    std::string usage;    // 使っている個体数の表示
    bool deletable = true;  // 環境の行はfalse(削除ボタンを出さない)
    bool operator==(const MissingPluginRowView&) const = default;
};

// 画面の固定文言(言語を切り替えると変わる)。
struct MissingPluginTexts {
    std::string title;
    std::string badgeTitle;
    std::string addRoot;
    std::string rescan;
    std::string replace;
    std::string remove;
    bool operator==(const MissingPluginTexts&) const = default;
};

// 操作の結果の通知。warningは失敗(何も変えていない)のとき。
struct MissingPluginNotice {
    std::string text;
    bool warning = false;
};

// 一覧の行。entriesの並びのまま、1行ずつ作る。
std::vector<MissingPluginRowView> BuildMissingPluginRows(const std::vector<MissingPluginEntry>& entries,
                                                         const Localization& localization);

MissingPluginTexts BuildMissingPluginTexts(const Localization& localization);

// 警告バッジの数(一覧の行数。個体数ではない)。0なら出さない。
std::size_t MissingPluginBadgeCount(const std::vector<MissingPluginEntry>& entries);

// 場所の種類の名前(翻訳済み)。
std::string MissingPluginKindName(PluginReferenceSite site, const Localization& localization);

// Replace/Deleteの結果から通知を作る。成功は件数、失敗は理由。
MissingPluginNotice BuildMissingPluginNotice(const MissingPluginReplaceResult& result, bool wasDelete,
                                             const Localization& localization);

// 差し替えのフォルダ選択を最初に開く場所。ルートの直下にある、その場所の種類のフォルダ(例: Struct)のうち、
// 実在する最初のもの。どのルートにも無ければ先頭のルート。ルートが無ければ空。
std::string MissingPluginPickerStartFolder(PluginReferenceSite site, const std::vector<std::string>& roots,
                                           const PluginExistsFn& folderExists);

// textの{count}・{detail}を置き換える。
std::string ExpandMissingPluginText(const std::string& text, std::size_t count, const std::string& detail);

}  // namespace rs::app
