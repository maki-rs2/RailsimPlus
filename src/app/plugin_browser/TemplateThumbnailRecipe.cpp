#include "TemplateThumbnailRecipe.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "../../plugins/schema/PluginTypeTraits.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../plugin_session/Formation.h"
#include "../plugin_session/PluginSession.h"
#include "../plugin_session/RailwayPluginSetResolve.h"
#include "../plugin_session/TrainGroupTemplateParse.h"
#include "../scene/ParsedPluginCache.h"
#include "../scene/TrainPreviewModel.h"

namespace rs::app::plugin_browser {
namespace {

// 先頭車の前(連結面より先)と末尾車の後ろに、軸・台車が乗れる余白を取る長さ[m]。
constexpr double kStraightTrackMarginM = 40.0;

// +Z 向きに進む直線の軌道を1本だけ持つ graph。head は前端(+Z 側)から kStraightTrackMarginM の位置に置く。
struct StraightTrack {
    rs::world::TrackGraph graph;
    rs::world::TrackPosition head;
};

StraightTrack MakeStraightTrack(double trainLengthM) {
    const double edgeLengthM = trainLengthM + 2.0 * kStraightTrackMarginM;
    auto makeSample = [](float z) {
        rs::world::TrackNodeSample sample;
        sample.pos = rs::core::Vec3(0.0f, 0.0f, z);
        sample.tangent = rs::core::Vec3(0.0f, 0.0f, 1.0f);
        sample.up = rs::core::Vec3(0.0f, 1.0f, 0.0f);
        sample.s = z;
        return sample;
    };

    StraightTrack track;
    rs::world::TrackEdge edge;
    edge.guid = rs::world::GenerateGuid();
    edge.nodeA = rs::world::GenerateGuid();
    edge.nodeB = rs::world::GenerateGuid();
    edge.spline = {makeSample(0.0f), makeSample(static_cast<float>(edgeLengthM))};
    track.graph.nodes[edge.nodeA] = rs::world::TrackNode{};
    track.graph.nodes[edge.nodeB] = rs::world::TrackNode{};
    track.head.edge = edge.guid;
    track.head.s = trainLengthM + kStraightTrackMarginM;
    track.head.direction = +1;
    track.graph.edges[edge.guid] = std::move(edge);
    return track;
}

// 編成テンプレートの車両を、読めたものだけ FormationCar にする(連結位置は FormationCarへ積むのと同じ def.trainInfo)。
std::vector<FormationCar> LoadTemplateCars(const TrainGroupTemplateParseResult& parsed, std::stop_token stopToken) {
    std::vector<FormationCar> cars;
    for (const FormationCarSpec& spec : parsed.cars) {
        if (stopToken.stop_requested()) return {};
        const std::string folder = ResolveTrainCarFolder(parsed.installRootUtf8, spec);
        if (folder.empty()) continue;
        PluginSession session;
        std::vector<std::string> loadLog;
        GetParsedPluginCache().LoadInto(session, folder, loadLog);
        if (!session.plugin.ok || session.plugin.def.header.type != rs::legacy_pi::PluginType::Train) continue;

        FormationCar car;
        car.folderUtf8 = folder;
        car.reversed = spec.reverse;
        car.frontLimit = session.plugin.def.trainInfo.frontLimit;
        car.tailLimit = session.plugin.def.trainInfo.tailLimit;
        car.switchOptions = spec.switchOptions;
        cars.push_back(std::move(car));
    }
    return cars;
}

// 結合メッシュ(車ごとに partIndex)へ、車ごとのワールド行列を頂点へ焼き込み、全頂点を partIndex=0 にする。
// 行列は row-vector 慣習(v' = v * M)の剛体変換。
void BakePartWorlds(rs::render::Mesh& mesh, const std::vector<rs::core::Mat4>& worlds) {
    auto worldOf = [&worlds](std::uint32_t part) -> const rs::core::Mat4& {
        return worlds[std::min<std::size_t>(part, worlds.size() - 1)];
    };
    auto transformPoint = [](const rs::core::Mat4& m, const float in[3], float out[3]) {
        const float x = in[0] * m.m[0] + in[1] * m.m[4] + in[2] * m.m[8] + m.m[12];
        const float y = in[0] * m.m[1] + in[1] * m.m[5] + in[2] * m.m[9] + m.m[13];
        const float z = in[0] * m.m[2] + in[1] * m.m[6] + in[2] * m.m[10] + m.m[14];
        out[0] = x;
        out[1] = y;
        out[2] = z;
    };

    // サブメッシュの中心は、元の partIndex が分かるうちに求める(1つのサブメッシュは1両の中に収まる)。
    for (rs::render::SubMesh& submesh : mesh.submeshes) {
        if (submesh.indexCount == 0 || submesh.indexOffset >= mesh.indices.size()) continue;
        const std::uint32_t part = mesh.vertices[mesh.indices[submesh.indexOffset]].partIndex;
        transformPoint(worldOf(part), submesh.center, submesh.center);
    }
    for (rs::render::MeshVertex& vertex : mesh.vertices) {
        const rs::core::Mat4& m = worldOf(vertex.partIndex);
        transformPoint(m, vertex.pos, vertex.pos);
        // 法線は回転部だけを掛ける。
        const float nx = vertex.normal[0] * m.m[0] + vertex.normal[1] * m.m[4] + vertex.normal[2] * m.m[8];
        const float ny = vertex.normal[0] * m.m[1] + vertex.normal[1] * m.m[5] + vertex.normal[2] * m.m[9];
        const float nz = vertex.normal[0] * m.m[2] + vertex.normal[1] * m.m[6] + vertex.normal[2] * m.m[10];
        vertex.normal[0] = nx;
        vertex.normal[1] = ny;
        vertex.normal[2] = nz;
        rs::core::Normalize3(vertex.normal);
        vertex.partIndex = 0;
    }
    mesh.ComputeAabb();
}

} // namespace

ThumbnailRecipeResult BuildTrainTemplateRecipe(const std::string& templatePathUtf8, const std::string& pluginRootUtf8,
                                               std::uint32_t width, std::uint32_t height,
                                               std::stop_token stopToken) {
    ThumbnailRecipeResult result;
    const TrainGroupTemplateParseResult parsed = ParseTrainGroupTemplateFile(templatePathUtf8, pluginRootUtf8);
    if (!parsed.ok) return result;

    const std::vector<FormationCar> cars = LoadTemplateCars(parsed, stopToken);
    if (cars.empty() || stopToken.stop_requested()) return result;

    double trainLengthM = 0.0;
    for (const FormationCar& car : cars) trainLengthM += car.frontLimit - car.tailLimit;
    const StraightTrack track = MakeStraightTrack(trainLengthM);

    // 配置前のプレビューと同じ結合・軸/台車の解決を使う。
    TrainPreviewModel model;
    std::vector<std::string> log;
    model.Update(cars, cars.size(), log);
    if (model.DrawnCarCount() == 0 || stopToken.stop_requested()) return result;
    const std::vector<rs::core::Mat4> worlds = model.SolveWorlds(cars, track.head, track.graph);

    result.mesh = model.Mesh();
    BakePartWorlds(result.mesh, worlds);
    if (result.mesh.Empty()) return result;
    FillThumbnailCamera(result, ThumbnailView::TrainTemplate, width, height);
    result.ok = true;
    return result;
}

ThumbnailRecipeResult BuildRailTemplateRecipe(const std::string& templatePathUtf8, const std::string& pluginRootUtf8,
                                              std::uint32_t width, std::uint32_t height,
                                              std::stop_token stopToken) {
    const RailwayPluginSetResolveResult resolved = ResolveRailwayPluginSetFile(templatePathUtf8, pluginRootUtf8);
    if (!resolved.ok) return {};
    return BuildLayingSetRecipe(resolved.folders, std::nullopt, ThumbnailView::RailTemplate, width, height,
                                stopToken);
}

} // namespace rs::app::plugin_browser
