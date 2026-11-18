//
// ModelMeshHelpers.h - モデル読込/差替え時の静的メッシュ変換(PluginLoader.cppから分離)
//
// RailSim II -k-build PBR(ChangeTexture/EnvMap/BumpMap)の静的適用と、ModelScale/ChangeModel
// スケールの頂点座標への焼き込みという、読込時・ChangeModel差替え時の両方で
// 使う小さなメッシュ変換ヘルパーをまとめる。木構造の走査やアセンブリ合成には
// 関与しない。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "PluginLoader.h"
#include "../../core/Math.h"
#include "../../render/mesh/Mesh.h"
#include "../schema/PartCustomizerSchema.h"

namespace rs::legacy_pi {
struct ModelPart;
} // namespace rs::legacy_pi

namespace rs::legacy_plugin {

// パーツの ChangeTexture 1件ぶんの解決結果(パスと、差し替え先が抜き穴を持つか)。
// どちらもパーツの定義だけで決まり、ChangeModel の候補モデルごとには変わらない。
struct ResolvedPartChangeTexture {
    LoadedModel::ResolvedChangeTexture paths;
    bool swappable = false; // 差し替え先に alpha==0 の texel がある、または解決できなかった
};

// 1パーツの ChangeTexture の解決結果を、同じパーツの全モデル(基本モデル・ChangeModel
// 候補)で使い回すための表。添字は ChangeTexture だけを宣言順に数えた番号(ModelPart::ForEachChangeTexture の index)で、まだ解決していない
// 要素は std::nullopt。候補の多いパーツで同じファイルの存在確認と画像判定を候補の数だけ
// 繰り返さないために使う。
using PartChangeTextureCache = std::vector<std::optional<ResolvedPartChangeTexture>>;

// mesh のマテリアルに PBR ChangeTexture / EnvMap / BumpMap を適用する。
// baseDir は定義ファイルのあるフォルダ（相対パス解決の基準）。
// ChangeTexture は trig.chain が空(Always=常時適用)の候補のみ mesh.materials[]
// へ静的に焼き込む(Case/If条件付き候補を焼き込むと実行時のMaterialRuntimeと
// 競合して戻らなくなるバグになるため)。一方resolvedOutには
// Case/If条件付きも含め全候補のパスを解決して埋める(実行時プリロード用)。
// setAnimationDefs を渡すと、SetAnimation の画像切替(Simple/Numbered)フレームの
// 対象マテリアルにも swappableTexture フラグを立てる（ChangeTexture 対象は常に
// 立てる）。nullptr の場合は ChangeTexture 対象のみ。
// partCache を渡すと ChangeTexture の解決結果をそこから引き、無ければ解決して書き込む
// (同じパーツの別モデルへの呼び出しで同じ表を渡す)。ファイルが無い警告と missing の加算は
// 解決した1回だけ行う。nullptr なら呼び出しごとに解決する。
int ApplyPbr(rs::render::Mesh& mesh, const rs::legacy_pi::ModelPart& part,
             const std::wstring& baseDir, std::vector<std::string>& log,
             int& missing, bool& anyApplied,
             std::vector<LoadedModel::ResolvedChangeTexture>* resolvedOut = nullptr,
             const std::vector<rs::legacy_pi::AnimationDef>* setAnimationDefs = nullptr,
             PartChangeTextureCache* partCache = nullptr);

// modelScale をメッシュ頂点座標へ焼き込む(ChangeModel第2引数/ModelScale)。
// 一様正スケールのため法線/UVは不変。scale==1.0fなら何もしない。
void BakeModelScale(rs::render::Mesh& mesh, float scale);

// plugin.assemblyPlacements の並び(= assemblyMesh の partIndex)に対応する、
// 各パーツの静的ワールド行列を取り出す。ModelPart::worldMat（mover非依存、
// 読込時に resolveJoint3D/resolvePlacements で確定済み。Joint3D の
// AttachCoord/AttachDir や JointZY の offsetZ/offsetY/turn を含む）をそのまま
// 使う。GPUインスタンス化のため静的アセンブリメッシュを1回だけワールド化
// する際に BakeStaticPartWorlds と組み合わせて使う。
std::vector<rs::core::Mat4> CollectStaticPartWorlds(const LoadedPlugin& plugin);

// perPartDefWorlds を plugin.assemblyPlacements の並び(= assemblyMesh の partIndex)へ
// 並べ替える。WorldPartIndex() が範囲外の配置には単位行列を入れる。
std::vector<rs::core::Mat4> GatherAssemblyPartWorlds(
    const LoadedPlugin& plugin, const std::vector<rs::core::Mat4>& perPartDefWorlds);

// mesh の各頂点(pos/normal)を、partIndex に対応する partWorlds[] の行列で
// 変換する(row-major/row-vector: v' = v * M)。範囲外の partIndex を持つ頂点は
// 無変換のまま(=モデル作成時のローカル座標がそのままAABBへ算入される。
// partWorldsがassemblyPlacementsの一部しかカバーしない構成のプラグインでは、
// 意図しない位置の頂点がAABBを歪ませる要因になりうる)。呼び出し後は AABB を
// 再計算する。BuildAssemblyMesh は「ワールド変換は焼き込まず実行時に gPartWorld で
// 与える」前提でメッシュを組む(PluginModelVariant.cpp参照)ため、実行時の
// パートワールド更新を経由しない静的GPUインスタンス化パス
// (SceneRenderer::TryGetStaticInstanceMesh等)では、キャッシュ前にこの関数で
// 1回だけ焼き込む必要がある。焼き込んだ頂点は partIndex を0へそろえる(パーツ行列を
// 消費済みで、複数パーツのまま残すとパーツ数1のインスタンスバッチで別配置の行列を引くため)。
// outSkippedVertexCountを渡すと、範囲外partIndexで無変換のまま残った頂点数を
// 返す(診断用、nullptrなら計測しない)。
void BakeStaticPartWorlds(rs::render::Mesh& mesh, const std::vector<rs::core::Mat4>& partWorlds,
                          size_t* outSkippedVertexCount = nullptr);

// BakeStaticPartWorldsを掛けたあとのAABBだけを、メッシュをコピーせずに求める(結果は同じ)。
// 可動部が動く間は毎フレーム呼ばれる。頂点が無ければ原点の大きさ0のAABBを返す。
void ComputeBakedAabb(const rs::render::Mesh& mesh, const std::vector<rs::core::Mat4>& partWorlds,
                      float outAabbMin[3], float outAabbMax[3],
                      size_t* outSkippedVertexCount = nullptr);

} // namespace rs::legacy_plugin
