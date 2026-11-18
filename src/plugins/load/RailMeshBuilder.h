//
// RailMeshBuilder.h - Rail/Tie/Pier 断面プロファイルの押し出し・反復メッシュ生成
//
// PluginDefinition の断面プロファイル（railProfiles）から表示用メッシュを生成する
// 純ジオメトリ層。LoadedModel/読み込み状態には依存せず、PluginDefinition と Mesh、
// パス解決（PluginPaths）のみを使う。plugin_loader（IO/オーケストレーション層）から
// 呼ばれる。
//
#pragma once

#include <string>
#include <vector>

#include "../../render/mesh/Mesh.h"
#include "runtime/TrackingMoveSolve.h"
#include "../../core/Math.h"
#include "../../world/alignment/AlignmentSegment.h"

namespace rs::legacy_pi { struct PluginDefinition; struct PierDef; struct RailInterval; struct LineDef; }

namespace rs::legacy_plugin {

// 半径 curveRadius[m] のカーブに対するカント角[deg]を RailInfo(CantRatio/MaxCant)
// から算出する。curveRadius≈0(直線)や CantRatio/MaxCant 未設定なら 0。
// 式: theta = MaxCant * (1 - exp(-CantRatio / (MaxCant * |R|)))
//   R→∞ で theta≈CantRatio/R (曲率半径に反比例)、R→0 で MaxCant へ指数収束。
// (公式 Help pi_rail.html の記述に整合する推定式。厳密な収束係数は未照合)
float ComputeCantDegrees(const rs::legacy_pi::PluginDefinition& def, float curveRadius);

// Rail 断面プロファイルを Z 方向へ length[m] 押し出して1メッシュ化する。
// curveRadius が 0 に近ければ直線、それ以外なら半径 curveRadius[m] の円弧に沿って押し出す
// (curveRadius > 0 は +X 方向へ曲がる円弧)。曲線では RailInfo からカント(横断傾斜)を
// 適用する: 断面を内側レール(=カーブ内側)軸まわりに傾け、外側レールが上がる。
// IgnoreCant 頂点は傾けない。緩和曲線は非対応(定カント)。
// cantDegOverride >= 0 のときは ComputeCantDegrees による自動算出を無視し、
// 指定角[deg]を強制適用する(UIプレビュー用)。既定(-1)は自動算出。
rs::render::Mesh BuildRailMesh(const rs::legacy_pi::PluginDefinition& def,
                               const std::wstring& baseDir, float length,
                               float curveRadius,
                               std::vector<std::string>& log, int& missing,
                               float cantDegOverride = -1.0f);

// Rail 断面プロファイルを、任意の線形（TrackNodeSample 列, 弧長サンプリング済み）に
// 沿って押し出して1メッシュ化する（BuildRailMesh の一般化）。
// BuildRailMesh の円弧ブランチと同じカント規約（内側レール頭頂をピボットに断面を
// ロール）を各サンプルへ個別に適用するため、緩和区間を含む複合線形でも滑らかに
// カントが変化する。sample.up はカント適用済みだがピボット規約と競合するため
// 使用せず、常に right=normalize(cross(worldUp, fwd)) / upH=cross(fwd, right)
// で断面フレームを再構築する。samples.size() < 2 の場合は空メッシュを返す。
// extraOffsetY: この断面全体を軌道基準(alignment Y)から下方向へ沈める量[m]
// (Rail=0, Tie=上位Railの累積Height, Girder=上位Rail+Tieの累積Height。RailSim IIの
// CProfilePlugin::AfterDumpの「Rail描画後にp1をHeight分下げ、その点を次のTie描画
// 原点にする」逐次オフセット連鎖に相当。カント適用前のローカルy座標へ加算するため、
// カントで断面が傾く際もこのオフセット自体が一緒にロールする)。既定0.0はRail用。
rs::render::Mesh BuildRailMeshAlongSpline(
    const rs::legacy_pi::PluginDefinition& def,
    const std::wstring& baseDir,
    const std::vector<rs::world::TrackNodeSample>& samples,
    std::vector<std::string>& log, int& missing,
    float extraOffsetY = 0.0f);

// Pier のシャフト断面(Profile)を Y 方向(高さ)へ height[m] 押し出して1メッシュ化。
// 断面は X(左右)-Z(前後) 平面(Coord の第2成分をワールドZとして扱う)。Rail/Tie の
// Z方向押し出し(断面X-Y)に対し、Pier は断面がX-Z(水平)でY方向(垂直)へ押し出す。
rs::render::Mesh BuildPierShaftMesh(const rs::legacy_pi::PluginDefinition& def,
                                    const std::wstring& baseDir, float height,
                                    std::vector<std::string>& log, int& missing);

struct PierIntervalPlacement {
    float distanceM = 0.0f;
    float scaleZ = 1.0f;
};

// 橋脚の進行方向に沿うInterval配置距離とzスケールを計算する。
std::vector<PierIntervalPlacement> CalculatePierIntervalPlacements(
    float lengthM, float offsetM, float intervalM, float taperZ, bool directionUp);

// 1つのIntervalモデルをシャフト軸へ反復配置する。lengthMはシャフト下端から上端まで。
rs::render::Mesh BuildPierIntervalMesh(const rs::render::Mesh& src, float modelScale,
                                       const rs::legacy_pi::PierDef& pier,
                                       const rs::legacy_pi::RailInterval& interval,
                                       float lengthM);

// mesh の全頂点マテリアルインデックスを out の末尾へオフセット追記し、頂点座標に
// scale を掛け (0,offsetY,0) 平行移動してマージする。Pier の Base/Head/Joint
// モデルをシャフトへ積み上げ合成するための共通処理。
void AppendOffsetMesh(rs::render::Mesh& out, const rs::render::Mesh& src,
                      float scale, float offsetY);

// 1つのモデルメッシュを曲線(curveRadius, 0=直線)に沿って interval[m] 間隔で
// length[m] 分だけ反復複製し1メッシュにマージする(まくらぎ等)。scale は頂点座標に
// 適用。cantDeg[deg]・gauge[m] でカント(横断傾斜)を断面ローカルX-Y平面で適用する
// (BuildRailMesh円弧ブランチと同じ規約)。ignoreCant=true ならカントを適用しない。
rs::render::Mesh BuildIntervalMesh(const rs::render::Mesh& src, float scale,
                                   float interval, float length,
                                   float curveRadius, float cantDeg, float gauge,
                                   bool ignoreCant);

// 1つのモデルメッシュ(src)を、任意の線形(TrackNodeSample列、弧長サンプリング済み)
// に沿って interval[m] 間隔・offset[m] の位相ずれで反復複製し1メッシュにマージする
// (まくらぎのモデル表示、BuildIntervalMesh の spline 一般化)。s = offset,
// offset+interval, offset+2*interval, ... と samples.back().s まで配置する
// (offset が interval 以上でも単純に開始点をずらすだけで、複数 Interval ブロック
// を組み合わせる場合の位相ずらしに使う想定)。frame(right/upH)・カント適用の規約は
// BuildRailMeshAlongSpline と同じ(各配置位置で samples を線形補間して求める)。
// samples.size() < 2 の場合は空メッシュを返す。extraOffsetYはBuildRailMeshAlongSpline
// と同じ意味(軌道基準からの沈め量、カント適用前に加算しカントと一緒にロールする)。
// Interval反復モデル1つ分の読込結果。intervalはdef.intervalsの要素を指す(defより長く使わない)。
struct LoadedIntervalModel {
    const rs::legacy_pi::RailInterval* interval = nullptr;
    rs::render::Mesh mesh;
};

// def.intervalsの各モデル(.x)をbaseDir基準で読む。ファイルが無い・読めないものは飛ばす。
// 敷設時の押し出し(ViewportRailMeshBuild)とブラウザのサムネイルで共用する。
std::vector<LoadedIntervalModel> LoadIntervalModels(const rs::legacy_pi::PluginDefinition& def,
                                                    const std::wstring& baseDir);

rs::render::Mesh BuildIntervalMeshAlongSpline(
    const rs::render::Mesh& src, float scale, float interval, float offset,
    const std::vector<rs::world::TrackNodeSample>& samples,
    float gauge, bool ignoreCant, float extraOffsetY = 0.0f);

// 1つのモデルメッシュ(src)をワールド空間の任意の1点(pos)へ、fwd/upで定まる
// 姿勢(カント無し)でscale倍して単発配置する(Lineのハンガー等、spline反復
// ではなくラベル付き明示座標へ置く用途向け。BuildIntervalMeshAlongSplineの単一
// インスタンス変換部分を切り出した独立実装)。fwd/upは直交でなくてもよい
// (内部でright=cross(up,fwd)、upH=cross(fwd,right)へ再直交化する)。
rs::render::Mesh PlaceMeshInstance(const rs::render::Mesh& src, float scale,
                                   const rs::core::Vec3& pos, const rs::core::Vec3& fwd,
                                   const rs::core::Vec3& up);

// Line(架線)の径間1本ぶんのワイヤーメッシュを生成する。
// baseDirはテクスチャ相対パス(Wiredmesh.Material.TexFileName)解決用の
// プラグインフォルダ。baseA/baseBはワイヤーのローカルz=0/1が対応するワールド位置
// (架線支持点=Pole原点ではなくトロリ線高さ=TrolleyAlt、呼び出し側で
// track位置+TrolleyAlt*upとして求めて渡す)。upA/upBはその地点の軌道アップベクトル。
// lineDef.wiredmeshes(RailSim II -k-build、実架線本体)があれば優先して使い、両点間の距離
// (径間長)が[MinInterval,MaxInterval)に収まる全プロファイルを重ねて描画する
// (トロリ線=区間指定なし=常時+messenger=径間長帯別、の複数一致を許容)。各プロファイルに
// Face断面があればBuildRailMeshAlongSplineと同じ要領でLine{}ポリライン沿いにスイープし、
// Material.TexFileNameがあればテクスチャを解決してmaterialへ設定する。Faceが無いプロファイルはDiffuse色の「+」断面
// リボン(2枚、レンダリングパイプラインが三角形限定のため RailSim II の線プリミティブをリボンで
// 近似)へフォールバックする。wiredmeshesが無ければlineDef.wireframes(旧形式、Faceを
// 持たないためリボンのみ)を単一プロファイル選択で使う。-k-buildのWireframeはハンガー
// 取付点トラッキング(Diffuseなし)であり架線本体ではないため、wiredmeshesがあれば無視する。
// 両方空、またはbaseA/baseBの距離が0に近い場合は空メッシュを返す。missingTexは見つから
// なかったテクスチャ数(BuildRailMeshAlongSplineと同じ規約でlogに警告も積む)。
rs::render::Mesh BuildLineWireMesh(const rs::legacy_pi::LineDef& lineDef,
                                   const std::wstring& baseDir, const rs::core::Vec3& baseA,
                                   const rs::core::Vec3& upA, const rs::core::Vec3& baseB,
                                   const rs::core::Vec3& upB, std::vector<std::string>& log,
                                   int& missingTex, TrackingTargetTable* trackingTargets = nullptr);

} // namespace rs::legacy_plugin
