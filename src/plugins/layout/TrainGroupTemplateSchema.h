//
// TrainGroupTemplateSchema.h - TrainGroupTemplate\*.txt(編成テンプレート)の
// PiDocument(AST) -> TrainGroupTemplateDef(型付き生データ) 変換
//
// 位置情報を持たない点で.rs2の
// TrainGroupList > TrainGroup(Rs2TrainGroup)とは異なる。.rs2側のTrain(1両)も
// SwitchOptionを持つため、スイッチ確定値の保持自体はどちらも同じ形。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../schema/parser/Ast.h"

namespace rs::legacy_layout {

// TrainTemplate{ ... } 1両ぶん。trainPluginはフォルダ名のみ(未解決)。
// switchOptionsはレイアウト保存時点のスイッチ確定値(DefineSwitch宣言順に
// 位置対応、Rs2PlacedInstance::switchOptionsと同じ意味)。
// TrainGroupTemplateApplierはreverseとswitchOptionsの両方をFormationへ適用する。
struct TrainTemplateEntry {
    std::string trainPlugin;
    bool        reverse = false;
    std::vector<int32_t> switchOptions;
};

struct TrainGroupTemplateDef {
    bool found = false;  // TrainGroupTemplateブロックがあった(無ければ別の形式のファイル)
    std::vector<TrainTemplateEntry> trains;
};

// PiDocument(rs::legacy_pi::ParseFile の結果) を TrainGroupTemplateDef へ変換する。
// warnings には DatafileHeader/TrainGroupTemplate ブロック不在等の診断メッセージを
// 追記する(エラーにはしない)。
TrainGroupTemplateDef ParseTrainGroupTemplate(const rs::legacy_pi::PiDocument& doc,
                                              std::vector<std::string>& warnings);

} // namespace rs::legacy_layout
