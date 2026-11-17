#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "PluginSidecarCommon.h"

namespace rs::legacy_pi { struct SwitchDef; }

namespace rs::legacy_pi::sidecar {

// switchDefs(DefineSwitch宣言順)のラベルでassignment(ラベル→値)を解決し、
// currentSwitchOptions(現在の確定値配列)と比較して実際に値が変わるものだけを
// (index, newValue)として返す(変化なしはスキップ、無駄な再bakeを避ける)。
// "_"始まりのキー(システムスイッチ、書き込み禁止)と、switchDefsにラベルが
// 見つからないキーは無視し、outIgnoredKeysが非nullptrならそのキー名を追加する。
// currentSwitchOptionsがswitchDefsより短い(未初期化スイッチ)場合、既定値0として扱う。
std::vector<std::pair<size_t, int32_t>> ResolveSwitchAssignmentChanges(
    const std::vector<rs::legacy_pi::SwitchDef>& switchDefs,
    const std::vector<int32_t>& currentSwitchOptions,
    const SwitchAssignment& assignment,
    std::vector<std::string>* outIgnoredKeys = nullptr);

} // namespace rs::legacy_pi::sidecar
