#include "../../render/RenderPrecision.h"
#include "TrainAssembly.h"

#include <algorithm>

namespace rs::legacy_plugin {

using rs::world::TrackGraph;
using rs::world::TrackPosition;

std::vector<TrackPosition> SolveTrainHeads(const TrackGraph& graph, const TrackPosition& head,
                                              const std::vector<CarSpec>& cars) {
    std::vector<TrackPosition> heads;
    heads.reserve(cars.size());
    TrackPosition cur = head;
    for (size_t i = 0; i < cars.size(); ++i) {
        heads.push_back(cur);
        if (i + 1 < cars.size()) {
            const double length = cars[i].frontLimit - cars[i].tailLimit;
            cur = rs::sim::ChaseBackward(graph, cur, length).pos;
        }
    }
    return heads;
}

std::vector<CarPose> SolveTrainHeadsAcrossScenes(
    const std::function<const TrackGraph*(size_t)>& graphForScene,
    const rs::world::WarpLinkTable& warpTable, size_t headSceneIndex,
    const TrackPosition& head, const std::vector<CarSpec>& cars,
    bool travelReversed, const rs::sim::TrainOccupiedPath* occupiedPath) {
    std::vector<CarPose> out;
    out.reserve(cars.size());

    size_t curScene = headSceneIndex;
    TrackPosition curPos = head;
    double pathOffsetM = 0.0;
    for (size_t i = 0; i < cars.size(); ++i) {
        const double length = cars[i].frontLimit - cars[i].tailLimit;
        pathOffsetM += length;
        rs::world::WarpAdvanceResult tail;
        size_t pathScene = curScene;
        TrackPosition pathPos;
        const rs::sim::GraphForScene pathGraphs = graphForScene;
        if (occupiedPath != nullptr && rs::sim::ChaseBackwardOnPath(*occupiedPath, pathGraphs,
                                                                    pathOffsetM, pathScene, pathPos)) {
            tail.sceneIndex = pathScene;
            tail.pos = pathPos;
        } else {
            tail = rs::world::ChaseBackwardAcrossScenes(graphForScene, warpTable, curScene, curPos, length);
        }

        CarPose pose;
        pose.sceneIndex = curScene;
        pose.head = curPos;
        pose.pathOffsetM = pathOffsetM - length;
        // 判定の実体は「Sceneが変わったか」ではなく「warpの不連続を跨いだか」。
        // warpは同一Scene内の2nodeを繋ぐこともあり(実データで確認)、その場合
        // sceneIndexは変化しない。SolveAxlesはScene単体のChaseBackwardしか
        // 行わずwarpを辿れないため、跨いでいる車両を描くと全車軸が端点nodeへ
        // 張り付いた(軸間の詰まった)姿勢になる。
        // truncatedAtWarp: warp端点まで到達したが相手側へ渡れなかった場合も、
        // 収まっているとは言えないので跨ぎ扱いにする。
        pose.spansScenes =
            tail.sceneIndex != curScene || tail.warpHops > 0 || tail.truncatedAtWarp;
        if (pose.spansScenes) {
            pose.fadeAlpha = 0.0f;
        } else {
            // warpへ近づいていく端(=進行方向の先端)から前方をN[m]走査する。順走では
            // 車両前端(head)、逆走では車両後端(tail)が先端になる。headから常に
            // head.dir方向へ走査すると、逆走時は車体をまたいだ先を見ることになり
            // 検出できない。
            const size_t scanScene = travelReversed ? tail.sceneIndex : curScene;
            rs::world::TrackPosition scanStart = travelReversed ? tail.pos : curPos;
            if (travelReversed) scanStart.direction = -scanStart.direction;
            const rs::world::WarpAdvanceResult ahead = rs::world::AdvanceAcrossScenes(
                graphForScene, warpTable, scanScene, scanStart, kWarpFadeLengthM);
            if (ahead.firstWarpDistance.has_value()) {
                pose.fadeAlpha = rs::render::ToGpuFloat(std::clamp(
                    *ahead.firstWarpDistance / kWarpFadeLengthM, 0.0, 1.0));
            }
        }
        out.push_back(pose);

        curScene = tail.sceneIndex;
        curPos = tail.pos;
    }
    return out;
}

} // namespace rs::legacy_plugin
