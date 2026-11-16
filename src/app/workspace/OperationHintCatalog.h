//
// OperationHintCatalog.h - 画面の右下に小さく出す操作説明(例: Delete:削除)の文言を、モードごとに表で持つ
//
// 鑑賞とシーン設定の画面には出さない(空文字)。作業モードの並びはWorkMode、画面の並びはScreenの列挙と同じ。
// 文言を変えるときはこの表だけを直す(ほかに画面・モードごとの分岐を書かない)。
//
#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "EnumIndex.h"
#include "WorkspaceShell.h"

namespace rs::app::workspace {

// 1つの文言の中で項目(`キー:動作`)を区切る文字列。表示では項目ごとに分けて間を広く空ける。
inline constexpr std::string_view kOperationHintSeparator = "  ";

// 作業モードごとの操作説明(WorkModeの並び: Rail, Train, Signal, Dia, Scenery, Terrain)。
inline constexpr std::array<const char*, kWorkModeCount> kWorkModeHints{
    "Enter:決定  Esc:取り消し  Delete:削除",
    "↑↓:編成を選択  Esc:取り消し  Delete:削除",
    "Esc:取り消し  Delete:削除",
    "Esc:一つ戻る",
    "←→:回転  Esc:取り消し  Delete:撤去",
    "",
};

// 画面ごとの操作説明(Screenの並び: Spectate, Work, Environment, Camera)。Workは作業モードの表を使う。
inline constexpr std::array<const char*, kScreenCount> kScreenHints{
    "",
    "",
    "",
    "Q/E:傾き  R:傾きを戻す  Enter:撮影  Esc:閉じる",
};

// 運転台視点中の操作説明(画面・モードによらない)。
inline constexpr const char* kCabinViewHint = "WASD:移動  ↑↓:高さ  右ドラッグ:見回す  Esc:戻る";

// 配置待ちなど、作業モードの中の一時的な状態で替える操作説明。label は操作説明の先頭に札として出す状態名。
struct OperationHintState {
    const char* label;
    const char* hint;
};

// 信号の配置待ち中(逆向きに置くのはShift+クリック。PointerAction::PlacementReverseの割り当てと合わせる)。
inline constexpr OperationHintState kSignalPlacingHint{
    "信号を配置中", "クリック:置く  Shift:逆向き  Esc:やめる"};

// 今の作業モードの中の一時的な状態。無ければnullptr。
inline const OperationHintState* OperationHintStateFor(const WorkspaceSnapshot& snapshot) {
    if (snapshot.cabinView || !snapshot.InWorkMode()) return nullptr;
    if (snapshot.workMode == WorkMode::Signal && snapshot.signalPlacing) return &kSignalPlacingHint;
    return nullptr;
}

// 操作説明の先頭に札として出す状態名。出さないときは空文字。
inline const char* OperationHintLabelFor(const WorkspaceSnapshot& snapshot) {
    const OperationHintState* state = OperationHintStateFor(snapshot);
    return state != nullptr ? state->label : "";
}

// 今の画面・モードの操作説明。出さないときは空文字。
inline const char* OperationHintFor(const WorkspaceSnapshot& snapshot) {
    if (snapshot.cabinView) return kCabinViewHint;
    if (const OperationHintState* state = OperationHintStateFor(snapshot)) return state->hint;
    if (snapshot.InWorkMode()) return kWorkModeHints[EnumIndex(snapshot.workMode)];
    return kScreenHints[EnumIndex(snapshot.screen)];
}

// 操作説明の1項目(`キー:動作`)。表示ではキーを枠で囲む。
struct OperationHintItem {
    std::string_view key;
    std::string_view action;
};

// 操作説明の文言を項目に分け、各項目をキーと動作に分ける(`:` が無い項目は全体をキーにする)。
inline std::vector<OperationHintItem> SplitOperationHint(std::string_view hint) {
    std::vector<OperationHintItem> items;
    for (std::string_view rest = hint; !rest.empty();) {
        const std::size_t gap = rest.find(kOperationHintSeparator);
        const std::string_view item = rest.substr(0, gap);
        const std::size_t colon = item.find(':');
        items.push_back({item.substr(0, colon), colon == std::string_view::npos ? std::string_view() : item.substr(colon + 1)});
        if (gap == std::string_view::npos) break;
        rest.remove_prefix(gap + kOperationHintSeparator.size());
    }
    return items;
}

} // namespace rs::app::workspace
