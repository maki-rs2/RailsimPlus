//
// PlatformParentDriver.h - 駅の Platform の ParentObject に追従するedgeの、今の姿勢を更新する。
//
#pragma once

#include <functional>
#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"

namespace rs::legacy_pi {
struct PluginDefinition;
} // namespace rs::legacy_pi

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

// entityGuid の駅を親にする番線のedge(IsParentDrivenEdge)について、親の部品の休止姿勢
// (def.parts[i].worldMat × entityWorld)から今の姿勢(dynamicWorlds[i] × entityWorld)への
// 剛体差分を edge.parentDelta に書く。dynamicWorlds は def.parts と同じ並び。
// 駅のアニメ評価(UpdateSessionPose)の後、列車の車軸解決より前に、駅の配置ごとに呼ぶ。
void UpdatePlatformParentDeltas(rs::world::TrackGraph& graph, const rs::world::Guid& entityGuid,
                                const rs::legacy_pi::PluginDefinition& def,
                                const std::vector<rs::core::Mat4>& dynamicWorlds,
                                const rs::core::Mat4& entityWorld);

// 親の駅の配置が無くなった番線のedgeの parentDelta を単位行列(休止姿勢)へ戻す。parentEntity は残すので、
// 削除をUndoして同じ配置が戻れば、次のtickで追従が復活する。entityExists は配置の存在を答える。
void ResetOrphanedParentDeltas(rs::world::TrackGraph& graph,
                               const std::function<bool(const rs::world::Guid&)>& entityExists);

// 親に追従するすべてのedgeの parentDelta を単位行列(休止姿勢)へ戻す。駅の配置を持たない
// 非アクティブSceneのgraphは、駅のアニメを評価しないので休止姿勢で扱う。
void ResetAllParentDeltas(rs::world::TrackGraph& graph);

} // namespace rs::app
