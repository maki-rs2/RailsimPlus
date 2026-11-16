#include "Editor.h"

#include "../../core/Epsilon.h"

#include <algorithm>
#include <iterator>

#include <spdlog/spdlog.h>

#include "Command.h"
#include "PlatformCommand.h"
#include "TrackLayingCommit.h"
#include "../../world/geometry/GeometrySolvers.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/ScenePlacement.h"
#include "../../plugins/load/runtime/PlatformParentPose.h"

namespace rs::editor {
namespace {
rs::core::Vec3 TransformPoint(const rs::core::Mat4& matrix, const rs::core::Vec3& point) {
    const float input[4] = {point.x, point.y, point.z, 1.0f};
    float output[4];
    rs::core::TransformPoint4(matrix, input, output);
    if (output[3] != 0.0f) return {output[0] / output[3], output[1] / output[3], output[2] / output[3]};
    return {output[0], output[1], output[2]};
}

} // namespace

std::vector<PlatformRailPlan> Editor::PlanPlatformRails(const rs::world::SceneEntity& entity,
                                                        const rs::legacy_pi::PluginDefinition& definition) const {
    const rs::core::Mat4 world = rs::world::ResolveWorldMatrix(entity.placement, graph);
    std::vector<PlatformRailPlan> result;
    for (size_t definitionIndex = 0; definitionIndex < definition.platforms.size(); ++definitionIndex) {
        const rs::legacy_pi::PlatformDef& platform = definition.platforms[definitionIndex];
        if (platform.parentUndefined) {
            spdlog::warn("[platform] Platform #{}: ParentObject \"{}\" is undefined (UndefinedObject), skipped",
                         definitionIndex, platform.parentObject);
            continue;
        }
        // 親に追従する番線: 休止姿勢のレールを、親の部品の休止姿勢(部品行列×配置)にかけた Coord で作る。
        // 親のワールド行列は配置の行列を含み、Coord は親自身のローカル座標。
        const bool parentDriven = platform.parentPartIndex >= 0 &&
                                  static_cast<size_t>(platform.parentPartIndex) < definition.parts.size();
        rs::core::Mat4 parentRest = rs::core::Mat4::Identity();
        if (parentDriven) {
            rs::core::Mat4 partRest;
            std::copy(std::begin(definition.parts[static_cast<size_t>(platform.parentPartIndex)].worldMat),
                      std::end(definition.parts[static_cast<size_t>(platform.parentPartIndex)].worldMat),
                      partRest.m);
            parentRest = rs::core::Mat4::Mul(partRest, world);
        }
        const auto toWorld = [&](const rs::core::Vec3& coord) {
            return parentDriven ? rs::app::TransformPlatformParentPoint(parentRest, coord)
                                : TransformPoint(world, coord);
        };
        if (platform.trackNum > 1) {
            spdlog::warn("[platform] skipped TrackNum={} platform rail generation", platform.trackNum);
            continue;
        }
        if (platform.coords.size() < 2 || platform.coords.size() > 3) {
            spdlog::warn("[platform] skipped platform rail generation with {} coordinates", platform.coords.size());
            continue;
        }
        const rs::core::Vec3 start = toWorld(platform.coords[0]);
        const rs::core::Vec3 end = toWorld(platform.coords.back());
        // 水平の線形(直線 or 3点円弧)から、両端の水平接線と水平長を求める。勾配は
        // 敷設と同じく新規ノードの接線ピッチ(弦勾配)で持ち、形は AddEdgeCommand が作る。
        float horizontalLength = 0.0f;
        rs::core::Vec3 startTangent;
        rs::core::Vec3 endTangent;
        if (platform.coords.size() == 2) {
            const rs::core::Vec3 delta(end.x - start.x, 0.0f, end.z - start.z);
            horizontalLength = delta.Length();
            if (horizontalLength <= rs::core::kGeometryLengthEpsM) {
                spdlog::warn("[platform] skipped degenerate two-coordinate platform rail");
                continue;
            }
            startTangent = delta.Normalized();
            endTangent = startTangent;
        } else {
            const rs::core::Vec3 middle = toWorld(platform.coords[1]);
            const rs::world::ThreePointArcSolveResult solved = rs::world::SolveThreePointArc(start, middle, end);
            if (!solved.acceptable) {
                spdlog::warn("[platform] skipped three-coordinate platform rail: {}",
                             solved.rejectReason == nullptr ? "unacceptable curve" : solved.rejectReason);
                continue;
            }
            horizontalLength = rs::world::AlignmentTotalLength(solved.curve);
            startTangent = rs::core::Vec3(middle.x - start.x, 0.0f, middle.z - start.z).Normalized();
            endTangent = solved.endTangent;
        }

        LaidEdgeInput input;
        input.a.pos = start;
        input.a.tangent = startTangent;
        input.b.pos = end;
        input.b.tangent = endTangent;
        input.horizontalLength = horizontalLength;
        PlatformRailPlan railPlan;
        railPlan.definitionIndex = definitionIndex;
        railPlan.parentPartIndex = parentDriven ? platform.parentPartIndex : -1;
        railPlan.plan = PlanLaidEdgePoses(graph, input);
        // 形を作れない番線は含めない(edgeも番線も追加しない)。
        railPlan.shape = BuildLaidEdgeShape(railPlan.plan);
        if (!railPlan.shape.ok) {
            spdlog::warn("[platform] skipped platform rail whose shape cannot be built: {}", railPlan.shape.error);
            continue;
        }
        result.push_back(std::move(railPlan));
    }
    return result;
}

void Editor::GeneratePlatformRails(const rs::world::SceneEntity& entity,
                                   const rs::legacy_pi::PluginDefinition& definition) {
    std::unique_ptr<CompositeCommand> composite = std::make_unique<CompositeCommand>();
    int addedCount = 0;
    for (PlatformRailPlan& railPlan : PlanPlatformRails(entity, definition)) {
        const rs::legacy_pi::PlatformDef& platform = definition.platforms[railPlan.definitionIndex];
        rs::world::TrackEdge edge = railPlan.plan.edge;
        edge.guid = rs::world::GenerateGuid();
        // プラグインは駅の指定ではなく、敷設エディタで選んでいるもの(敷設テンプレート)を付ける。
        rs::world::SetEdgeFolders(edge, layingSettings.defaultTrackPlugins);
        if (railPlan.parentPartIndex >= 0) {
            edge.parentEntity = entity.guid;
            edge.parentPart = railPlan.parentPartIndex;
        }
        composite->Add(std::make_unique<AddEdgeCommand>(edge, true, railPlan.plan.nodeA, true, railPlan.plan.nodeB));

        rs::world::PlatformStopInfo stop;
        stop.stoppable = platform.stoppable;
        stop.openDoor[0] = platform.openDoor[0];
        stop.openDoor[1] = platform.openDoor[1];
        // 番号は定義順の添字。作れない番線を読み飛ばしても、後ろの番線の番号はずれない
        // (接近マスクのビット位置・effector の番線番号がRailSim II の番号と一致する)。
        stop.indexInStation = static_cast<int>(railPlan.definitionIndex);
        stop.stationEntity = entity.guid;
        ++addedCount;
        stop.railEdges.push_back(edge.guid);
        composite->Add(std::make_unique<AddPlatformCommand>(std::move(stop)));
    }
    if (addedCount > 0) ExecuteCommand(std::move(composite));
}

} // namespace rs::editor
