//
// RailwayPluginSetWriter.h - RailwayPluginSetDef -> RailwayPluginSet\*.txt(敷設テンプレート)本文
//
// ParseRailwayPluginSetの逆。各種類の先頭を RailwayPluginSet{ } に書き、2つ目以降
// (extraPlugins)は閉じの `}` の後ろの RailsimPlusExtra{ Plugin = "..."; } に書く。
// RailSim IIは閉じの `}` より後ろを読まないため、各種類の先頭だけが使われる。
//
#pragma once

#include "RailwayPluginSetSchema.h"
#include "TemplateWriteText.h"

namespace rs::legacy_layout {

// RailSim IIでは使われない2つ目以降の種類(RailsimPlusExtra)を持つか。保存時の警告の判定に使う。
bool HasExtraPlugins(const RailwayPluginSetDef& def);

// 書けない値があれば失敗にして、その項目名をbadFieldへ入れる。
TemplateWriteResult WriteRailwayPluginSet(const RailwayPluginSetDef& def);

}  // namespace rs::legacy_layout
