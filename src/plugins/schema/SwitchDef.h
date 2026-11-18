//
// SwitchDef.h - DefineSwitch の定義
//
// スイッチの定義だけを使う側が、定義全体まで読まずに済むよう PluginSchema.h から分けてある。
//
#pragma once

#include <string>
#include <vector>

namespace rs::legacy_pi {

struct SwitchDef {
    std::string name;
    std::string groupCommon;
    std::vector<std::string> entries;
};

} // namespace rs::legacy_pi
