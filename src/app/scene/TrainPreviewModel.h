//
// TrainPreviewModel.h - 編成配置プレビュー用の車両モデル(GPU非依存)
//
// 配置前の編成(FormationCar の並び)を、実際の車両モデルを半透明で出すために、
// 各車のメッシュを1つへ結合し(車ごとに partIndex を振る)、車ごとのワールド行列を
// 軌道位置から求める。
// - モデルのロード・ベイク・結合は編成の中身(folder+switchOptionsの並び)が変わった
//   ときだけ行う(Update)。毎フレーム呼ぶのは SolveWorlds だけで、編成長に比例する。
// - 各車の軌道位置は配置処理と同じ SolveCarPlacements で求める。
// - 台車は直線のままベイクされるため、曲線では実配置より少しずれる。
//
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "../../core/Math.h"
#include "../../plugins/load/TrainAssembly.h"
#include "../../render/mesh/Mesh.h"
#include "../../world/alignment/TrackPosition.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/PluginSession.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

// 各メッシュを1つへ結合する。meshes[k] の頂点には partIndex=k を振る。
// 同じ内容のマテリアルは1つにまとめる(テクスチャスロットの節約)。
rs::render::Mesh MergeMeshesByPart(const std::vector<const rs::render::Mesh*>& meshes);

class TrainPreviewModel {
public:
    // carsの先頭から最大maxCars両を対象に、中身が前回と変わっていれば読み込み直して結合する。
    // 戻り値は「結合メッシュが変わったので再アップロードが要る」こと。
    bool Update(const std::vector<FormationCar>& cars, size_t maxCars, std::vector<std::string>& log);

    // carsの先頭から最大maxCars両のプラグインが解析済みか。未解析の車があれば先読みのスレッドへ出して
    // falseを返す(同じ並びでは1回だけ出す)。Updateは未解析の車をその場で解析して数秒止まるので、
    // メインスレッドではこれがtrueになってからUpdateを呼ぶ。
    bool PrefetchReady(const std::vector<FormationCar>& cars, size_t maxCars);

    // 結合メッシュの中身を表すキー(アップロード済みかどうかの判定用)。
    const std::string& ContentKey() const { return contentKey_; }
    const rs::render::Mesh& Mesh() const { return mergedMesh_; }
    // 描く車の数(ロードに失敗した車を除く)。0なら何も描かない。結合メッシュの partIndex の数と一致する。
    size_t DrawnCarCount() const { return drawn_.size(); }

    // 描く車ごとのワールド行列(DrawnCarCountと同じ並び)。carsはUpdateに渡したものと同じ並び。
    std::vector<rs::core::Mat4> SolveWorlds(const std::vector<FormationCar>& cars,
                                            const rs::world::TrackPosition& head,
                                            const rs::world::TrackGraph& graph) const;

private:
    struct CarModel {
        bool ok = false;
        rs::render::Mesh mesh; // プラグインローカル座標へ静的パーツ行列を焼き込み済み
        rs::legacy_plugin::TrainVehicleTemplate tmpl;
        // 車体として使うパーツ(末尾のBody)。無ければ -1(先頭headの軌道姿勢で代用する)。
        int anchorPart = -1;
        // 車体パーツの静的配置の逆行列(プラグインローカル→車体ローカル)。
        rs::core::Mat4 anchorStaticInverse = rs::core::Mat4::Identity();
    };
    struct DrawnCar {
        size_t carIndex = 0;     // cars 内の添字
        std::string modelKey;    // models_ のキー
    };

    const CarModel& LoadModel(const std::string& modelKey, const FormationCar& car,
                              std::vector<std::string>& log);

    PluginSession session_;
    uint64_t pluginGeneration_ = 0;
    std::unordered_map<std::string, CarModel> models_;
    std::vector<DrawnCar> drawn_;
    std::string contentKey_;
    rs::render::Mesh mergedMesh_;
    // 先読みを出した車の並びと、解析済みを確かめた並び(どちらもキャッシュの世代を含む)。
    std::string prefetchKey_;
    std::string readyKey_;
};

} // namespace rs::app
