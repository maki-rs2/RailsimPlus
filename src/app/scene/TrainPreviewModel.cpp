#include "ParsedPluginCache.h"
//
// TrainPreviewModel.cpp
//
#include "TrainPreviewModel.h"

#include <algorithm>
#include <cstring>

#include "../../world/scene/ScenePlacement.h"
#include "StaticInstanceKey.h"
#include "StaticMeshVariantBake.h"
#include "TrainPlacement.h"

namespace rs::app {

rs::render::Mesh MergeMeshesByPart(const std::vector<const rs::render::Mesh*>& meshes) {
    rs::render::Mesh merged;
    for (size_t part = 0; part < meshes.size(); ++part) {
        const rs::render::Mesh& source = *meshes[part];
        const uint32_t vertexBase = static_cast<uint32_t>(merged.vertices.size());
        const uint32_t indexBase = static_cast<uint32_t>(merged.indices.size());

        // マテリアルは内容が同じものを使い回す(元のmaterialIndex -> 結合後の添字)。
        std::vector<uint32_t> materialRemap(source.materials.size(), 0);
        for (size_t i = 0; i < source.materials.size(); ++i) {
            const std::vector<rs::render::Material>::iterator found =
                std::find(merged.materials.begin(), merged.materials.end(), source.materials[i]);
            if (found != merged.materials.end()) {
                materialRemap[i] = static_cast<uint32_t>(found - merged.materials.begin());
            } else {
                materialRemap[i] = static_cast<uint32_t>(merged.materials.size());
                merged.materials.push_back(source.materials[i]);
            }
        }

        for (rs::render::MeshVertex vertex : source.vertices) {
            vertex.partIndex = static_cast<uint32_t>(part);
            merged.vertices.push_back(vertex);
        }
        for (uint32_t index : source.indices) merged.indices.push_back(index + vertexBase);
        for (rs::render::SubMesh submesh : source.submeshes) {
            if (submesh.materialIndex < materialRemap.size()) {
                submesh.materialIndex = materialRemap[submesh.materialIndex];
            }
            submesh.indexOffset += indexBase;
            merged.submeshes.push_back(submesh);
        }
    }
    merged.ComputeAabb();
    return merged;
}

const TrainPreviewModel::CarModel& TrainPreviewModel::LoadModel(const std::string& modelKey,
                                                                const FormationCar& car,
                                                                std::vector<std::string>& log) {
    const std::unordered_map<std::string, CarModel>::const_iterator cached = models_.find(modelKey);
    if (cached != models_.end()) return cached->second;

    CarModel model;
    std::vector<std::string> loadLog;
    rs::app::GetParsedPluginCache().LoadInto(session_, car.folderUtf8, loadLog);
    log.insert(log.end(), loadLog.begin(), loadLog.end());
    if (session_.plugin.ok && session_.plugin.hasAssembly) {
        // 配置前の見本はカメラや時刻の更新を持たないため、読込時の状態を明示して焼く。
        const SwitchEvaluationInput input = session_.BuildSwitchEvaluationInput(&car.switchOptions);
        session_.ApplySwitchEvaluationAndBake(input);
        model.mesh = BakeSessionAssemblyMesh(session_);
        model.tmpl = rs::legacy_plugin::BuildTrainVehicleTemplate(session_.plugin.def);
        model.ok = !model.mesh.Empty();
        if (!model.tmpl.bodyPartIndex.empty()) {
            model.anchorPart = model.tmpl.bodyPartIndex.back();
            rs::core::Mat4 staticMat = rs::core::Mat4::Identity();
            std::memcpy(staticMat.m, session_.plugin.def.parts[static_cast<size_t>(model.anchorPart)].worldMat,
                        sizeof(staticMat.m));
            model.anchorStaticInverse = rs::core::InverseRigid(staticMat);
        }
    }
    return models_.emplace(modelKey, std::move(model)).first->second;
}

bool TrainPreviewModel::Update(const std::vector<FormationCar>& cars, size_t maxCars,
                               std::vector<std::string>& log) {
    const uint64_t generation = GetParsedPluginCache().Generation();
    if (pluginGeneration_ != generation) {
        models_.clear();
        contentKey_.clear();
        pluginGeneration_ = generation;
    }
    const size_t carCount = std::min(cars.size(), maxCars);
    std::vector<std::string> modelKeys;
    modelKeys.reserve(carCount);
    std::string key = MakePluginMeshKey("");
    for (size_t i = 0; i < carCount; ++i) {
        modelKeys.push_back(MakeStaticInstanceKey(cars[i].folderUtf8, cars[i].switchOptions));
        key += modelKeys.back();
        key += '\n';
    }
    if (key == contentKey_) return false;

    // 別の編成へ切り替わるたびに古いモデルが溜まり続けないよう、一定数を超えたら捨てる。
    constexpr size_t kMaxCachedModels = 64;
    if (models_.size() > kMaxCachedModels) models_.clear();

    drawn_.clear();
    std::vector<const rs::render::Mesh*> meshes;
    for (size_t i = 0; i < carCount; ++i) {
        const CarModel& model = LoadModel(modelKeys[i], cars[i], log);
        if (!model.ok) continue; // 読めなかった車は描かず、他の車だけ描く
        drawn_.push_back(DrawnCar{i, modelKeys[i]});
        meshes.push_back(&model.mesh);
    }
    mergedMesh_ = MergeMeshesByPart(meshes);
    contentKey_ = std::move(key);
    return true;
}

std::vector<rs::core::Mat4> TrainPreviewModel::SolveWorlds(const std::vector<FormationCar>& cars,
                                                           const rs::world::TrackPosition& head,
                                                           const rs::world::TrackGraph& graph) const {
    std::vector<rs::core::Mat4> worlds;
    worlds.reserve(drawn_.size());
    const std::vector<rs::world::TrackPlacement> placements = SolveCarPlacements(cars, head, graph);
    for (const DrawnCar& drawn : drawn_) {
        if (drawn.carIndex >= placements.size()) {
            worlds.push_back(rs::core::Mat4::Identity());
            continue;
        }
        const CarModel& model = models_.at(drawn.modelKey);
        const rs::world::TrackPlacement& placement = placements[drawn.carIndex];
        if (model.anchorPart < 0) {
            worlds.push_back(rs::world::ResolveWorldMatrix(placement, graph));
            continue;
        }
        // 実配置と同じ軸/台車の解決で車体パーツの姿勢を求め、プラグインローカル座標へ
        // 焼き込んだメッシュ用に車体の静的配置の逆行列を掛ける。
        const std::vector<rs::core::Mat4> solved = rs::legacy_plugin::SolvePartWorlds(
            graph, placement.position, model.tmpl, 0.0, 0.0, nullptr, nullptr, 1.0, 1.0, nullptr,
            cars[drawn.carIndex].reversed);
        worlds.push_back(rs::core::Mat4::Mul(model.anchorStaticInverse,
                                             solved[static_cast<size_t>(model.anchorPart)]));
    }
    return worlds;
}

} // namespace rs::app
