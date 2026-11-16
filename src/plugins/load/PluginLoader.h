//
// PluginLoader.h - PI プラグインフォルダ → 内部表現（定義+モデル+テクスチャ）
//
// フォルダを渡すと定義 txt（Train2k.txt 優先→Train2.txt→*2.txt）を読み、
// PI パーサで AST 化 → PluginDefinition 構築 → 参照 .x モデルを x_loader で
// 読み、RailSim II -k-build PBR ChangeTexture（base/normal/pbr）を各マテリアルへ割り当て、
// 相対パス（..\ 含むプラグイン跨ぎ）を解決する。
//
// 無いファイルはエラーにせず警告して継続（LackPlugin 方針）。
//
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "PluginPaths.h"
#include "RailMeshBuilder.h"
#include "StretchZyMeshAssembly.h"
#include "../schema/PluginSchema.h"
#include "../../render/mesh/Mesh.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::legacy_plugin {

// 表示可能な1モデル部品（定義上のパーツ＋ロード済みメッシュ）
struct LoadedModel {
    std::string      name;       // 部品ラベル（Body "Bogie1" など）
    std::string      kind;       // Axle/Body/Object3D
    std::string      modelPath;  // 解決済み絶対 .x パス（UTF-8）
    rs::render::Mesh mesh;
    bool             meshOk = false;
    bool             pbrApplied = false; // RailSim II -k-build PBR が1つ以上適用されたか

    // Interval(枕木等)のcurve/cantプレビュー再構築用（kind=="Interval"のときのみ有効）
    bool             rebuildableInterval = false;
    rs::render::Mesh intervalSource;   // 反復前の1個メッシュ(scale未適用)
    float            intervalScale = 1.0f;
    float            intervalStep = 1.0f;
    bool             intervalIgnoreCant = false;

    // def.parts 内の対応インデックス（Rail押し出し/Interval/Pier等の合成
    // モデルは対応パーツが無いため -1）。可動部ランタイム(PartMoverRuntime)を
    // 引くのに使う。
    int              partDefIndex = -1;

    // ApplyPbr（ChangeTexture の静的1回適用）を掛ける前の素の .x
    // マテリアル値。MaterialRuntime がフレームごとにここへリセットしてから
    // アクティブな ChangeTexture/ChangeMaterial を上書きするための基準状態。
    std::vector<rs::render::Material> baseMaterials;

    // ChangeTexture だけを宣言順に数えた並び順・件数で、base/normal/pbr を
    // 絶対パスへ解決した結果（見つからなければ空文字＝そのファイルは無視）。
    // material_runtime の実行時テクスチャロード解決に使う
    // （ChangeTexture 自体は未解決の相対パスのため）。
    struct ResolvedChangeTexture {
        std::string base, normal, pbr;
    };
    std::vector<ResolvedChangeTexture> resolvedChangeTextures;
};

// ChangeModel の実行時動的解決(候補の1つ)。ApplyPbr 適用済み・ModelScale
// 未焼込みのメッシュを保持する（scale は候補の組み合わせにより実行時に変わり
// うるため、焼込みは ApplyModelVariant() で表示直前に行う）。
struct ModelVariant {
    bool                               ok = false; // ファイル読込・メッシュ生成に成功したか
    rs::render::Mesh                   meshUnscaled;
    std::vector<rs::render::Material>  baseMaterials; // ApplyPbr前の素の値
    std::vector<LoadedModel::ResolvedChangeTexture> resolvedChangeTextures;
};

// 1パーツ分の ChangeModel の候補。読込のあとは書き換えないので、LoadedPlugin を
// 複製しても中身は複製せず共有する(同じプラグインを多数配置すると、候補メッシュの
// 複製が読込時間の大きな割合を占めるため)。
struct PartModelVariants {
    ModelVariant              baseVariant;          // part.baseModelFile 由来
    std::vector<ModelVariant> changeModelVariants;   // part.changeModels と同順
};

// 1パーツ分の ChangeModel 動的解決状態。lp.def.parts と同じ添字
// （part.changeModels が空のパーツは variants が nullptr のまま=対象外）。
struct PartDynamicModel {
    std::shared_ptr<const PartModelVariants> variants;
    // 現在 lp.models[] に焼き込み済みの候補（-1=baseVariant、i>=0=changeModelVariants[i]）
    // とそのスケール。ResolveChangeModel() の結果と比較し、差分があれば
    // ApplyModelVariant() で差し替える（毎フレーム全再構築を避けるための
    // 「現在表示中」キャッシュ。variantIndex が同じでもスケールだけ別の
    // アクティブ候補由来で変わりうるため両方を比較対象にする）。
    int                        currentVariantIndex = -1;
    float                      currentScale = 1.0f;
};

// ModelScale の無効値を、メッシュ焼込みとGeometrySignatureで同じ実効倍率へ正規化する。
float EffectiveModelScale(float scale);

// DefineAnimation の1 frame文につき、UV系(SlideUV/TiledUV/RotationUV)の
// テクスチャパスを絶対解決した結果。Simple/Numbered はUV系ではないため
// 解決対象外（base は空文字のまま）。
struct ResolvedAnimationFrame {
    std::string base;
};

// RailSim II -k-build用の定義ファイル(*2k.txt)は標準版(*2.txt)より高い基準フレームレートで
// 書かれており、AnimationFrame::durationFrames の1フレームの長さが異なる。
constexpr float kAnimationFrameRateStandardFps = 30.0f;
constexpr float kAnimationFrameRateKBuildFps   = 60.0f;

struct LoadedPlugin {
    bool                     ok = false;   // 定義パースに成功
    bool                     noDefinition = false; // 定義ファイル無し（共有リソース等、非プラグイン）
    std::wstring             folder;       // プラグインフォルダ
    std::wstring             defPath;      // 使った定義ファイル
    rs::legacy_pi::PluginDefinition def;
    std::vector<LoadedModel> models;
    std::vector<std::string> log;      // UTF-8 情報/警告
    int                      warnCount = 0;
    int                      missingFiles = 0; // 参照したが見つからないファイル数

    // ChangeModel の実行時動的解決用データ。def.parts と同じ添字・件数
    // （changeModels が空のパーツは既定値のまま=対象外）。PluginSession::
    // UpdateDynamicModels() が毎フレーム ResolveChangeModel() と突き合わせ、
    // 差分があれば ApplyModelVariant() で models[]/assemblyMesh を差し替える。
    std::vector<PartDynamicModel> dynamicModels;

    // def.animations と同じ並び・件数（各要素はさらに animations[i].frames と
    // 同じ並び・件数）。UV アニメのテクスチャ実行時プリロード/バインドに使う。
    std::vector<std::vector<ResolvedAnimationFrame>> resolvedAnimations;

    // Train 等: 全パーツを1メッシュに合成した表示用（partIndexタグ付き）。
    // meshOk なパーツのみを対象にしており、assemblyPlacements は
    // assemblyMesh 内で使われた partIndex の並びと1:1対応する。
    rs::render::Mesh              assemblyMesh;
    // 第1取り付け先のローカル座標で生成済みのStretchZY。合成時は各要素を末尾の配置にする。
    std::vector<StretchZyAssemblyPart> stretchZyParts;
    // 今の合成メッシュがあるか。ChangeModelで全パーツが空のモデルになると false に変わる。
    bool                          hasAssembly = false;
    // 読込直後(全スイッチ0)の時点で合成メッシュがあったか。読込の最後に1回だけ決め、
    // 以後のChangeModelでは変えない。セッションを使い回してスイッチ値を焼き直すとき、
    // 前回の焼き込みの状態に判定が引きずられないようにするため(StaticMeshVariantBake.cpp)。
    bool                          hasAssemblyAtLoad = false;
    struct PartPlacement {
        float offsetZ = 0, offsetY = 0;
        bool  turn = false;
        std::string label;
        // Joint3D 配置(ModelPart::worldMat)を持つパーツはこちらを優先する
        // （Station 等の施設パーツ）。offsetZ/offsetY/turn は JointZY 由来
        // （Train 等）の配置に用いる。
        bool  useJoint3D = false;
        float worldMat[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        // def.parts / moverRuntimes 側の添字（アニメ適用の対応付け用）。
        int   partDefIndex = -1;
        // StretchZYの第1取り付け先。材質・アニメの対応付けには使わない。
        int   anchorPartDefIndex = -1;
        int WorldPartIndex() const {
            return partDefIndex >= 0 ? partDefIndex : anchorPartDefIndex;
        }
        // assemblyMesh 内でこのパーツのマテリアルが始まる添字
        // （BuildAssemblyMesh の matBase と同じ計算。material_runtime の
        // 適用結果を assemblyMesh 側のマテリアル番号へオフセットするのに使う）。
        uint32_t matBase = 0;
    };
    std::vector<PartPlacement>    assemblyPlacements;
};

struct LoadOptions {
    bool loadMeshes = true;     // false なら定義パースのみ（スキャン高速化）
    bool preferKBuild = true;   // *2k.txt を優先
    std::wstring forceDefFile;  // 指定時はこの定義ファイルを使う（空=自動選択）
    float curveRadiusM = 0.0f;  // 0=直線、非0ならプレビュー用に円弧押し出し

    // Pier(橋脚)の接合点(Joint)の地面からの高さ[m]の上書き。<0(既定)なら
    // PluginLoader.cpp内の固定値(6.0m、プラグイン単体プレビュー用)を使う。
    // >=0のときはこの値を起点にJoint→Head→シャフト上端を積み下ろして構築する
    // (実際の設置点の高さを反映する呼び出し側=StaticMeshCache::TryGetPierAssemblyMesh用)。
    float pierHeightOverrideM = -1.0f;
};

// プラグインフォルダを読み込む。
LoadedPlugin LoadPluginFolder(const std::wstring& folder, const LoadOptions& opt = {});

// このプラグインが実際に読み込んだ定義ファイル(lp.defPath)の版から、
// DefineAnimation の durationFrames を実時間へ換算する基準フレームレートを返す。
// 同じ durationFrames でも版によって再生速度が2倍違うため、必ずこれを介すること。
float AnimationFrameRateFps(const LoadedPlugin& lp);

// lp.models[] のうち partDefIndex に対応するエントリを、variantIndex
// （-1=lp.dynamicModels[partDefIndex].baseVariant、i>=0=changeModelVariants[i]）
// の内容へ scale を焼込んで差し替える。lp.hasAssembly なら RebuildAssembly()
// も行う。該当 variant が ok=false（空指定 ChangeModel="" または読込失敗）の
// 場合はメッシュを空にし meshOk=false としてパーツを非表示にする（RailSim II 準拠）。
// partDefIndex が対象外/variantIndex が
// 無効/現在の表示と同一（currentVariantIndexとscale両方一致）の場合は何もせず
// false を返す。呼び出し側(PluginSession)はこの戻り値でGPU再アップロードの
// 要否を判定する。
bool ApplyModelVariant(LoadedPlugin& lp, int partDefIndex, int variantIndex, float scale);

// ApplyModelVariant から RebuildAssembly を除いたもの。1回の切替で複数のパーツが変わるとき、
// パーツごとに組立全体を作り直さないように使う。trueを返したら、最後に RebuildAssembly を1回呼ぶ。
bool SetModelVariantWithoutRebuild(LoadedPlugin& lp, int partDefIndex, int variantIndex, float scale);

// lp.models[] から partDefIndex>=0 かつ meshOk なものを集め、StretchZYを末尾に足して
// assemblyMesh/assemblyPlacements(matBase含む)を再構築する。ApplyModelVariant 等でいずれか
// のパーツの mesh.materials.size() が変わりうる場合に呼び直す（matBase は
// 宣言順の累積マテリアル数のため、1パーツの変更が後続パーツ全ての matBase を
// ずらす）。def.partsもStretchZYも空なら合成結果と配置を空にする。
// LoadPluginFolder 内の初期構築と ApplyModelVariant の両方から呼ぶ。
void RebuildAssembly(LoadedPlugin& lp);

// mesh を lp の合成メッシュとして採用する(パーツ行列1個・def.parts 非対応の配置1件)。
// def.parts を持たないプラグイン(Pier の合成メッシュ、押し出し表示など)を、
// 組立メッシュを前提にする描画経路で出すために使う。頂点の partIndex は0であること。
void AdoptMeshAsAssembly(LoadedPlugin& lp, rs::render::Mesh mesh);

// 断面プロファイルの押し出し/反復メッシュ生成（ComputeCantDegrees / BuildRailMesh /
// BuildRailMeshAlongSpline / BuildIntervalMesh 等）は RailMeshBuilder.h を参照。

} // namespace rs::legacy_plugin
