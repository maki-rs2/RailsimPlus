//
// PluginThumbnailRecipe.h - プラグインフォルダから、サムネイルの撮影に渡すメッシュ・カメラを作る
//
// GPUには触らない(ワーカースレッドで呼ぶ)。
// ThumbnailRenderer(render層)はプラグインに依存しないので、プラグイン固有の作り方はここに置く。
//
#pragma once

#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

#include "../../render/mesh/Mesh.h"
#include "../plugin_session/RailTieHeights.h"
#include "BrowserKindCatalog.h"
#include "ThumbnailView.h"

namespace rs::legacy_plugin {
struct LoadedPlugin;
}

namespace rs::app::plugin_browser {

struct ThumbnailRecipeResult {
    bool ok = false;
    // 定義は読めたが、描くモデルが意図して1つも無いプラグイン(このとき ok は false)。
    // 背景だけの画像を撮る。mesh・カメラ・光は使わない。
    bool noModel = false;
    // パーツ行列を焼き込み、全頂点を partIndex=0 にしたメッシュ(撮るときのパーツ行列は単位行列1つ)。
    // カメラが +Z 側から -Z 向きに見る向き(方位0)になるよう、Y軸まわりに回してある(光が全変種で同じ向きになる)。
    rs::render::Mesh mesh;
    float viewProj[16] = {};
    float cameraPos[3] = {};
};

// 「意図してモデルが無い」プラグインか(モデル系の撮り方 ThumbnailMethod::Model 用)。
// 定義が読めて(ok かつ noDefinition でない)、参照ファイルが全部あり、組み上がったモデルが
// 1つも無く、Pier/レール断面の参照も無いときだけ true。モデルを参照していて読めなかった
// プラグインは(読込失敗なので) false。
bool IsPluginWithoutModel(const rs::legacy_plugin::LoadedPlugin& plugin);

// folderUtf8 のプラグインを撮るための材料を作る。読込失敗・メッシュが空は ok=false。
// モデル系でモデルが意図して無いときは ok=false・noModel=true。
// 種別ごとの作り方は表(BrowserKindTraits::thumbnailMethod)で、カメラの変種は表(kThumbnailViews)で分ける。
ThumbnailRecipeResult BuildThumbnailRecipe(const std::string& folderUtf8, BrowserKind kind, ThumbnailView view,
                                           std::uint32_t width, std::uint32_t height);

// 敷設プラグインセット(folders)のうち、軌道系でメッシュを押し出す種別(Rail・Tie・Girder・Line)を、
// 実際の敷設と同じ積み上げ(heights から ExtrusionOffsetY・Line の持ち上げ量を決める)で1本の直線に重ねて撮る材料にする。
// 読めないフォルダ・対象外の種別は飛ばし、1つも作れなければ ok=false。直線の長さは含まれる種別の表の長さの最大。
// プラグインを1つ読むごとに stopToken を見て、止める要求があれば ok=false で返す。
// heights が無いときは、読めたプラグインから求める(敷設テンプレートのカードのように、セレクタを持たない呼び出し元用)。
// view は構図(LayingSet=縦長の段、RailTemplate=カードの4:3)。
ThumbnailRecipeResult BuildLayingSetRecipe(const std::vector<std::string>& folders,
                                           const std::optional<rs::app::RailTieHeights>& heights, ThumbnailView view,
                                           std::uint32_t width, std::uint32_t height, std::stop_token stopToken = {});

// result.mesh(AABB計算済み)を撮るカメラを view の表から決めて result へ書く。
// 方位はモデルのほうをY軸まわりに回して0に合わせるので、result.mesh も回る。
void FillThumbnailCamera(ThumbnailRecipeResult& result, ThumbnailView thumbnailView, std::uint32_t width,
                         std::uint32_t height);

} // namespace rs::app::plugin_browser
