//
// GroupCommonSwitchSync.h - 編成内のGroupCommonスイッチ同期(PI互換のスイッチ確定値共有)
//
// RailSim II(CModelSwitch::m_GroupCommon)は、DefineSwitchのGroupCommonへ同じ文字列を
// 与えたスイッチ同士を、名前・添字が異なっていても同一グループとみなし、編成内の
// いずれかの値が変わると同グループの全スイッチへ生値をそのままコピーする。
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

// folderUtf8のプラグインが宣言するスイッチ定義を返す関数(未解決ならnullptr)。
// SceneRenderer::TryGetSwitchDefsへの薄い依存注入で、単体テストではスタブを渡す。
using SwitchDefsLookup =
    std::function<const std::vector<rs::legacy_pi::SwitchDef>*(const std::string& folderUtf8)>;

// sourceEntityId/sourceIndexのスイッチがGroupCommonを持つ場合、同一trainId(編成)に
// 属する全Car(自分自身の他スイッチも含む)のうち、同じGroupCommon名を持つスイッチへ
// valueをそのままコピーする(clampなし、RailSim II 互換)。GroupCommonが空、またはsourceEntityId
// がCarでない場合は何もしない。呼び出し側で先にsourceEntityId自身のSetSwitchOptionを
// 適用してから呼ぶこと(この関数は他車両・他スイッチへの伝播のみを担う)。
void PropagateGroupCommonSwitch(rs::world::SceneRegistry& registry,
                                const SwitchDefsLookup& switchDefsLookup,
                                const rs::world::Guid& sourceEntityId, std::size_t sourceIndex,
                                std::int32_t value);

} // namespace rs::app
