//
// SceneryTemplateSchema.h - StationBuildTemplate\*.txt / StructBuildTemplate\*.txt(駅・施設のテンプレート)の
// PiDocument(AST) -> SceneryTemplateDef(型付き生データ) 変換
//
// 駅と施設は箱の名前とプラグインのキー名が違うだけで、個体の持つ値(スイッチ確定値・位置・姿勢)は
// 同じなので、1つの型で扱う。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../schema/parser/Ast.h"
#include "../../core/Math.h"

namespace rs::legacy_layout {

enum class SceneryTemplateKind { Station, Struct };

// StationTemplate / StructTemplate 1個ぶん。pluginはフォルダ名のみ(未解決)。
// posはテンプレートの原点からの相対位置。dirとupで姿勢(ピッチ・ロールを含む)を表す。
struct SceneryTemplateEntry {
    std::string plugin;
    std::vector<int32_t> switchOptions;
    rs::core::Vec3 pos{};
    rs::core::Vec3 dir{};
    rs::core::Vec3 up{0.0f, 1.0f, 0.0f};
};

struct SceneryTemplateDef {
    bool found = false;  // 箱が見つかった(無ければ別の形式のファイル)
    SceneryTemplateKind kind = SceneryTemplateKind::Station;
    std::vector<SceneryTemplateEntry> entries;
};

// PiDocument を SceneryTemplateDef へ変換する。ルートの箱の名前で kind を決める。
// warnings には DatafileHeader・箱の不在といった診断を追記する(エラーにはしない)。
// Up が無い個体は (0,1,0)。
SceneryTemplateDef ParseSceneryTemplate(const rs::legacy_pi::PiDocument& doc,
                                        std::vector<std::string>& warnings);

}  // namespace rs::legacy_layout
