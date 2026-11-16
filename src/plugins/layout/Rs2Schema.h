//
// Rs2Schema.h - .rs2 の PiDocument(AST) → Rs2Layout(型付き生データ) 変換
//
// ブロック名/キー名の解釈をここに集約する。未知ブロック・未知キーはエラーに
// せず警告して読み飛ばす(前方互換。LackPlugin方針と同じ考え方)。
//
#pragma once

#include <string>
#include <vector>

#include "Rs2Types.h"
#include "../schema/parser/Ast.h"

namespace rs::legacy_layout {

// Surface(地形)の配置1件。Pos/Dir/Upを持たないSurfaceは原点・Z前方・Y上に置く
// (.rs2のSurfacePluginと、既定プラグインの自動配置で共用する)。
Rs2PlacedInstance MakeSurfaceInstance(const std::string& pluginName);

// PiDocument(rs::legacy_pi::ParseFile の結果) を Rs2Layout へ変換する。
// warnings には未知ブロック等の診断メッセージを追記する(エラーにはしない)。
Rs2Layout ParseRs2Layout(const rs::legacy_pi::PiDocument& doc, std::vector<std::string>& warnings);

} // namespace rs::legacy_layout
