//
// SrvLayout.h - SRVヒープのレイアウト(部品ごとの範囲の割り付け)
//
// 各部品の範囲は SrvHeapReserver へ予約した順に払い出し、前の部品の末尾から番号を連鎖させない。
// 並びの順は BuildSrvLayout の1か所にだけ書く。ヒープの総数 total はデバイスを作る前に要るので、
// レイアウトはコンパイル時に組み立てる(kSrvLayout)。
//
// 容量の定数(kMaxTextures・kSceneEntityMaxTextures など)は、範囲の大きさを決める入力としてここが正典。
//
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "GpuContext.h"
#include "SrvHeapReserver.h"
#include "../../render/effects/LensFlarePass.h"
#include "../../render/postprocess/AmbientOcclusionPass.h"
#include "../../render/postprocess/DepthOfFieldPass.h"
#include "../../render/postprocess/LuminanceMeterPass.h"
#include "../../render/postprocess/MotionBlurPass.h"
#include "../../render/postprocess/PostProcessChain.h"
#include "../../render/postprocess/TaaPass.h"
#include "../../render/effects/VolumetricFogPass.h"
#include "../../render/resource/ComputeSelfTest.h"
#include "../../render/resource/SrvRangeAllocator.h"
#include "../../render/sky/AtmosphereLuts.h"
#include "../../render/sky/EnvCubeSet.h"
#include "../../render/sky/IblGenerator.h"
#include "../../render/sky/SkyBodyPass.h"
#include "../../render/wboit/WboitPass.h"

namespace rs::app {

// base/normal/orm の3枚1組×多マテリアル対応。メッシュ自身の材質数+ChangeTexture/
// SetAnimation の事前ロード全候補がここに一緒に入る点に注意(事前ロードが多い
// プラグインでは、事前ロードと大型モデルの材質で768(=256組)を超え、
// 事前ロードが黙って失敗する)。2048=682組。
constexpr UINT kMaxTextures = 2048;
// 半透明パスの構造化バッファ SRV（gTriMat/gMats をフレームごとに2枚1組で配置）。
constexpr UINT kTransBufSrvCount = 2 * 2; // (gTriMat + gMats) × 2 フレーム
// パーツ単位ワールド行列（gPartWorld）。フレームごとに1枚確保する。
constexpr UINT kPartWorldSrvCount = kFrameCount; // フレームごとに1枚
constexpr UINT kInstanceMatOverrideSrvCount = kFrameCount * 2;
constexpr UINT kMaxParts = 128; // 車両アセンブリの最大パーツ数想定

// 第2描画パス: シーン常駐エンティティ。「1配置=1MeshPipeline+1SRVレンジ」
// の実装で、スロットはSrvRangeAllocator経由で使われた分だけ遅延初期化される
// (FrameRenderer::EnsureSceneSlotCapacity)。常駐数に固定の上限は置かず、
// kSceneSrvPoolCapacityの総枠のみを規定する可変長プールにしている
// (大規模レイアウトでは装飾の組み合わせが数十種に達するため)。
// 「1配置=1スロット」は変わらない(真のGPUインスタンシングは
// InstanceBatchManager側の役目)。
// 1エンティティ分。列車プラグイン(テクスチャ参照110件)を編成配置に使うとスロットが
// 枯渇し、部品の一部がgPartWorld範囲外を参照して表示されなくなるため、256/128にしている。
constexpr UINT kSceneEntityMaxTextures = 256;
constexpr UINT kSceneEntityMaxParts = 128;
constexpr UINT kSceneEntitySrvStride =
    kSceneEntityMaxTextures + kTransBufSrvCount + kPartWorldSrvCount + kInstanceMatOverrideSrvCount;
// 同ストライドのスロットを可変長で何個確保できるかの総枠。全スロットが同一
// ストライド(262)を要求する。SrvRangeAllocatorにはストライドちょうど(262)の
// サイズクラスがあり、切り上げが起きないため、約1563スロットが確保できる
// (切り上げが起きると各スロットが1024スロットを消費し、400スロットしか取れず、
// 大規模レイアウトの動的配置がSRV枯渇で非表示になる)。
constexpr UINT kSceneSrvPoolCapacity = 1024u * 400u;
// 上のスロット数拡大は「ストライドがサイズクラスと完全一致していること」に依存する。
// どちらかを変更して一致が崩れると黙って1024へ切り上げられ枠が1/4に戻るため、
// コンパイル時に検出する。
static_assert(std::find(rs::render::SrvRangeAllocator::kSizeClasses.begin(),
                        rs::render::SrvRangeAllocator::kSizeClasses.end(),
                        kSceneEntitySrvStride) !=
                  rs::render::SrvRangeAllocator::kSizeClasses.end(),
              "kSceneEntitySrvStride must exactly match one of "
              "SrvRangeAllocator::kSizeClasses (otherwise the scene SRV pool "
              "silently loses 3/4 of its slots to size-class rounding)");

// GPUインスタンス描画バッチ(InstanceBatchManager)。
// SRVレンジは SrvRangeAllocator(サイズクラス/スラブ方式)による可変長プールで、
// kInstanceBatchSrvPoolCapacity は総枠のみを規定する。ユニークモデル数の上限は
// 「1バッチが要求するスロット数(=batchMaxTextures+trans/partWorldぶん)」と
// 「総枠の残量」で決まる。配置数(インスタンス数)はバッファのバイト数のみで
// 制限され、SRV枠には依存しない点が、スロット常駐(kSceneEntity*)と根本的に異なる。
// このプールは静的シーナリー(InstanceBatchManager)だけでなく、同一contentKey+
// ジオメトリ署名の車両が2両以上揃った際にスロット描画から移されるTrainInstanceBatch
// (SceneTrainBatching.cpp)とも共有する。車両プラグイン(テクスチャ参照100件超)が
// この経路へ入ってもスロットが枯渇しないよう、256にしている。
constexpr UINT kInstanceBatchMaxTextures = 256;
// 1バッチのgPartWorldバッファの初期容量(配置数)。上限ではない: 超えた分は
// MeshPipeline::EnsurePartWorldCapacity が Sync 時にバッファを倍々で作り直して
// 収容する。固定上限にすると超過分を取りこぼし(大規模レイアウトでは樹木のような
// 繰り返し配置プラグインが1バッチ数千配置に達する)、固定4096では1バッチあたり
// sizeof(PartXform)=128B × 4096 × 2フレーム = 1MB を Init時に無条件確保することに
// なる。実配置数の大半は数十件なので小さく始める。
constexpr UINT kInstanceBatchInitialInstances = 256;
// 1バッチのストライド(=64+trans/partWorldぶん≒67-70)はSrvRangeAllocatorの
// サイズクラス(16/64/256/1024/4096/16384)により256スロットへ切り上げられる。
// Pier/Poleに加え.rs2の個別Scenery配置(Struct/Station等)もこの経路を使い、
// 大規模レイアウトではユニークモデルが百数十種に達する。バッチが作れないと
// 「[instance_batch_mgr] SRV pool exhausted」で該当配置が丸ごと非表示になるため、
// 256スロット×512バッチ分の余裕を持たせる。
constexpr UINT kInstanceBatchSrvPoolCapacity = 256u * 512u;
constexpr UINT kTrainInstanceBatchSrvPoolCapacity = 256u * 128u;

// Scenery選択ハイライト用。ホバー/選択の2つを固定パイプラインとして持つ
// (FrameHighlightState)。SceneEntity用の可変長スロットとは独立: 1回に描くのは
// 常に1エンティティ(gPartWorldは1エントリのみ)なので maxParts=1 で足りる。
// ストライドの内訳はkSceneEntitySrvStrideと同じ規約(テクスチャ+transBuf+partWorld)。
// テクスチャ枠数はkSceneEntityMaxTextures(=256、本編描画と同じ実績上限)に合わせる。
// 64では、テクスチャ数が64を超える車両プラグインを選択すると
// texture slot budget exhaustedで一部テクスチャが登録できず、ハイライトの
// シルエットが崩れる。
constexpr UINT kHighlightMaxTextures = kSceneEntityMaxTextures;
constexpr UINT kHighlightMaxParts = 1;
constexpr UINT kHighlightStride = kHighlightMaxTextures + kTransBufSrvCount + kPartWorldSrvCount + kInstanceMatOverrideSrvCount;
constexpr UINT kHighlightSlotCount = 2; // 0=ホバー, 1=選択

// SRVヒープの部品ごとの範囲の先頭番号。ImGui は 0 に固定する(ImGui バックエンドが先頭を使う)。
struct SrvLayout {
    // ImGuiのフォント(1枠)。
    UINT imgui = 0;
    // 本編のメッシュ(MeshPipeline)。テクスチャ・半透明の構造化バッファ・gPartWorld・材質の上書き。
    UINT mainTextures = 0;
    UINT mainTransBuf = 0;
    UINT mainPartWorld = 0;
    UINT mainMatOverride = 0;
    // シーン常駐スロットの可変長プール・静的インスタンス・列車インスタンス(いずれもSrvRangeAllocatorが払い出す総枠)。
    UINT scenePool = 0;
    UINT instanceBatchPool = 0;
    UINT trainInstanceBatchPool = 0;
    // WBOIT 合成の入力(accum + reveal + 合成前のシーンのコピー)。
    UINT wboit = 0;
    // Scenery選択ハイライトの固定2スロット(kHighlightStride ずつ)。
    UINT highlight = 0;
    // HDRシーンターゲット(トーンマップが読む)。
    UINT sceneHdr = 0;
    // 大気LUT(透過率・多重散乱・Sky-View)の直後に、IBL(環境ソース・拡散・鏡面・BRDF LUT・上半球平均)が連続する。
    // 空パスのデバッグ経路が、大気LUTの先頭から4枠のテーブルで環境ソースをt3として引くため、隣り合わせにする。
    UINT atmosphere = 0;
    UINT ibl = 0;
    // Envプラグインの焼き済みキューブ(拡散・鏡面・BRDF LUT)。
    UINT envCube = 0;
    // 標準の空の遠景キューブ(TextureCube、全ミップ)。
    UINT landscape = 0;
    // 月用のSky-View LUT。
    UINT moonSkyView = 0;
    // 暗順応の結果(1x1の倍率・照度)。
    UINT exposure = 0;
    // プラグインの空の Sun.x・Moon.x・Landscape.x のテクスチャ(連続3枠)。
    UINT skyBody = 0;
    // 太陽光のシャドウマップ。
    UINT shadowMap = 0;
    // 太陽のレンズフレア(先頭がシーンの深度、続いてテクスチャ)。
    UINT lensFlare = 0;
    // 大気遠近のLUT。
    UINT aerialPerspective = 0;
    // ブルーム・FXAAの作業用RT。
    UINT postProcess = 0;
    // アンビエントオクルージョン。先頭が深度プリパスの深度(AO とプリパスは同じ範囲を分け合う)。
    UINT ambientOcclusion = 0;
    // 速度バッファ(Rg16Float)。
    UINT velocity = 0;
    UINT taa = 0;
    UINT motionBlur = 0;
    // フォトモードの被写界深度。
    UINT depthOfField = 0;
    // フォトモードの ISO オートの測光(出力の1枚)。
    UINT luminanceMeter = 0;
    // computeの基盤の確認(RS_DEBUG_COMPUTE=1のときだけ使う)。
    UINT computeSelfTest = 0;
    UINT volumetricFog = 0;
    // ヒープの総ディスクリプタ数。
    UINT total = 0;
    // 予約の記録(重なり・隙間の検査と診断用)。
    SrvHeapReserver reservations;
};

constexpr SrvLayout BuildSrvLayout() {
    SrvLayout layout;
    SrvHeapReserver& heap = layout.reservations;
    layout.imgui = heap.Reserve("imgui", 1);
    layout.mainTextures = heap.Reserve("mainTextures", kMaxTextures);
    layout.mainTransBuf = heap.Reserve("mainTransBuf", kTransBufSrvCount);
    layout.mainPartWorld = heap.Reserve("mainPartWorld", kPartWorldSrvCount);
    layout.mainMatOverride = heap.Reserve("mainMatOverride", kInstanceMatOverrideSrvCount);
    layout.scenePool = heap.Reserve("scenePool", kSceneSrvPoolCapacity);
    layout.instanceBatchPool = heap.Reserve("instanceBatchPool", kInstanceBatchSrvPoolCapacity);
    layout.trainInstanceBatchPool = heap.Reserve("trainInstanceBatchPool", kTrainInstanceBatchSrvPoolCapacity);
    layout.wboit = heap.Reserve("wboit", rs::render::WboitPass::kSrvCount);
    layout.highlight = heap.Reserve("highlight", kHighlightStride * kHighlightSlotCount);
    layout.sceneHdr = heap.Reserve("sceneHdr", 1);
    layout.atmosphere = heap.Reserve("atmosphere", rs::render::AtmosphereLuts::kSrvCount);
    layout.ibl = heap.Reserve("ibl", rs::render::IblGenerator::kSrvCount);
    layout.envCube = heap.Reserve("envCube", rs::render::EnvCubeSet::kSrvCount);
    layout.landscape = heap.Reserve("landscape", 1);
    layout.moonSkyView = heap.Reserve("moonSkyView", 1);
    layout.exposure = heap.Reserve("exposure", 1);
    layout.skyBody = heap.Reserve("skyBody", rs::render::SkyBodyPass::kSrvCount);
    layout.shadowMap = heap.Reserve("shadowMap", 1);
    layout.lensFlare = heap.Reserve("lensFlare", rs::render::LensFlarePass::kSrvCount);
    layout.aerialPerspective = heap.Reserve("aerialPerspective", 1);
    layout.postProcess = heap.Reserve("postProcess", rs::render::PostProcessChain::kSrvCount);
    layout.ambientOcclusion = heap.Reserve("ambientOcclusion", rs::render::AmbientOcclusionPass::kSrvCount);
    layout.velocity = heap.Reserve("velocity", 1);
    layout.taa = heap.Reserve("taa", rs::render::TaaPass::kSrvCount);
    layout.motionBlur = heap.Reserve("motionBlur", rs::render::MotionBlurPass::kSrvCount);
    layout.depthOfField = heap.Reserve("depthOfField", rs::render::DepthOfFieldPass::kSrvCount);
    layout.luminanceMeter = heap.Reserve("luminanceMeter", rs::render::LuminanceMeterPass::kSrvCount);
    layout.computeSelfTest = heap.Reserve("computeSelfTest", rs::render::ComputeSelfTest::kSrvCount);
    layout.volumetricFog = heap.Reserve("volumetricFog", rs::render::VolumetricFogPass::kSrvCount);
    layout.total = heap.Total();
    return layout;
}

inline constexpr SrvLayout kSrvLayout = BuildSrvLayout();

static_assert(!kSrvLayout.reservations.Overflowed(), "SrvHeapReserver::kMaxReservations を増やすこと");
static_assert(kSrvLayout.imgui == 0, "ImGuiのフォントはヒープの先頭");
static_assert(kSrvLayout.ibl == kSrvLayout.atmosphere + rs::render::AtmosphereLuts::kSrvCount,
              "環境ソースは大気LUTの直後");

} // namespace rs::app
