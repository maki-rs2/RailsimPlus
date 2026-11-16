//
// RailwayPluginSetSchema.h - RailwayPluginSet\*.txt(敷設テンプレート)の
// PiDocument(AST) -> RailwayPluginSetDef(型付き生データ) 変換
//
// 1本の軌道に載せる装飾一式(Rail/Tie/Girder/Pier/Line/Pole)
// への参照+各*Checkフラグ+敷設パラメータを保持する。Rs2Schema.hと同じ変換方針
// (未知ブロック/未知キーはエラーにせず警告して読み飛ばす)。
//
#pragma once

#include <string>
#include <vector>

#include "../schema/parser/Ast.h"

namespace rs::legacy_layout {

// RailwayPluginSet{ ... } 1ブロックぶん。*Pluginはフォルダ名のみ(未解決)、
// *Checkは対応する要素を敷くかどうか(false時はRailwayPluginSetApplierが
// 家族リストへ追加しない)。
struct RailwayPluginSetDef {
    bool found = false;  // RailwayPluginSetブロックがあった(無ければ別の形式のファイル)
    std::string settingName;

    std::string railPlugin;   bool railCheck = false;
    std::string tiePlugin;    bool tieCheck = false;
    std::string girderPlugin; bool girderCheck = false;
    std::string pierPlugin;   bool pierCheck = false;
    std::string linePlugin;   bool lineCheck = false;
    std::string polePlugin;   bool poleCheck = false;

    bool  enableCant = false;
    bool  liftRailSurface = false;
    bool  multiTrack = false;
    int   trackNum = 1;
    float trackInterval = 0.0f;

    // 閉じの `}` の後ろの RailsimPlusExtra{ Plugin = "..."; } に書かれた、各種類の2つ目以降。
    // フォルダ名から種類は分からないため、種類ごとには持たない。RailSim II は読まない。
    std::vector<std::string> extraPlugins;
};

// PiDocument(rs::legacy_pi::ParseFile の結果) を RailwayPluginSetDef へ変換する。
// warnings には DatafileHeader/RailwayPluginSet ブロック不在等の診断メッセージを
// 追記する(エラーにはしない)。
RailwayPluginSetDef ParseRailwayPluginSet(const rs::legacy_pi::PiDocument& doc,
                                          std::vector<std::string>& warnings);

} // namespace rs::legacy_layout
