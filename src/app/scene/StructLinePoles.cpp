//
// StructLinePoles.cpp - StructLinePoles.h の実装
//
#include "StructLinePoles.h"

#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

#include "../../core/Math.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

namespace {

using rs::world::Guid;
using rs::world::PlacedIndividual;

// row-vector規約の行列(行0=右, 行1=上, 行2=前, 行3=原点)で点・方向を変える。計算はdoubleで行う。
rs::core::DVec3 TransformByRows(const rs::core::Mat4& matrix, const rs::core::Vec3& local,
                                bool isPoint) {
    const float* m = matrix.m;
    const double x = local.x;
    const double y = local.y;
    const double z = local.z;
    rs::core::DVec3 result;
    result.x = x * m[0] + y * m[4] + z * m[8] + (isPoint ? m[12] : 0.0);
    result.y = x * m[1] + y * m[5] + z * m[9] + (isPoint ? m[13] : 0.0);
    result.z = x * m[2] + y * m[6] + z * m[10] + (isPoint ? m[14] : 0.0);
    return result;
}

double DistanceBetween(const rs::core::Vec3& a, const rs::core::Vec3& b) {
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
    const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool IsPlainWorldPole(const PlacedIndividual& pole) {
    return pole.hasWorldAnchor && pole.folderUtf8.empty();
}

// toleranceM以内で最も近い導出した柱。無ければnullptr。
const PlacedIndividual* FindNearestDerived(const std::vector<PlacedIndividual>& derived,
                                           const rs::core::Vec3& pos, double toleranceM) {
    const PlacedIndividual* best = nullptr;
    double bestDistance = toleranceM;
    for (const PlacedIndividual& candidate : derived) {
        const double distance = DistanceBetween(candidate.worldPos, pos);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = &candidate;
        }
    }
    return best;
}

}  // namespace

Guid StructLinePoleId(const Guid& entityGuid, std::size_t index) {
    return rs::world::GuidFromSeed(rs::world::GuidToHex(entityGuid) + "|LinePoint|" +
                                   std::to_string(index));
}

std::vector<PlacedIndividual> DeriveStructLinePoles(
    const std::vector<rs::world::SceneEntity>& entities, const rs::world::TrackGraph& graph,
    const LinePointLookup& lookup) {
    std::vector<PlacedIndividual> poles;
    for (const rs::world::SceneEntity& entity : entities) {
        if (entity.category != rs::world::SceneEntityCategory::Scenery) continue;
        const std::vector<rs::legacy_pi::LinePointDef>* points = lookup(entity.folderUtf8);
        if (points == nullptr || points->empty()) continue;
        const rs::core::Mat4 matrix = rs::world::ResolveWorldMatrix(entity.placement, graph);
        for (std::size_t index = 0; index < points->size(); ++index) {
            const rs::legacy_pi::LinePointDef& point = (*points)[index];
            const rs::core::DVec3 worldPos = TransformByRows(matrix, point.coord, true);
            const rs::core::DVec3 worldDir = TransformByRows(matrix, point.dir, false);
            PlacedIndividual pole;
            pole.id = StructLinePoleId(entity.guid, index);
            pole.hasWorldAnchor = true;
            pole.worldPos = rs::core::Vec3(static_cast<float>(worldPos.x),
                                           static_cast<float>(worldPos.y),
                                           static_cast<float>(worldPos.z));
            pole.worldYawDeg = static_cast<float>(
                std::atan2(worldDir.x, worldDir.z) * 180.0 / std::numbers::pi);
            poles.push_back(std::move(pole));
        }
    }
    return poles;
}

std::unordered_set<Guid, rs::world::GuidHash> CollectPoleIds(
    const std::vector<PlacedIndividual>& poles) {
    std::unordered_set<Guid, rs::world::GuidHash> ids;
    for (const PlacedIndividual& pole : poles) ids.insert(pole.id);
    return ids;
}

int BindPlainPolesToStructLinePoles(rs::world::ManualPlacementOverlay& overlay,
                                    const std::vector<PlacedIndividual>& derived,
                                    double toleranceM) {
    if (derived.empty()) return 0;
    std::unordered_map<Guid, Guid, rs::world::GuidHash> remap;
    for (const PlacedIndividual& pole : overlay.poles) {
        if (!IsPlainWorldPole(pole)) continue;
        const PlacedIndividual* nearest = FindNearestDerived(derived, pole.worldPos, toleranceM);
        if (nearest != nullptr) remap.emplace(pole.id, nearest->id);
    }
    if (remap.empty()) return 0;
    for (rs::world::WireSpan& span : overlay.lines) {
        const std::unordered_map<Guid, Guid, rs::world::GuidHash>::const_iterator a =
            remap.find(span.poleA);
        if (a != remap.end()) span.poleA = a->second;
        const std::unordered_map<Guid, Guid, rs::world::GuidHash>::const_iterator b =
            remap.find(span.poleB);
        if (b != remap.end()) span.poleB = b->second;
    }
    std::vector<PlacedIndividual> kept;
    kept.reserve(overlay.poles.size());
    for (PlacedIndividual& pole : overlay.poles) {
        if (remap.find(pole.id) == remap.end()) kept.push_back(std::move(pole));
    }
    overlay.poles = std::move(kept);
    return static_cast<int>(remap.size());
}

}  // namespace rs::app
