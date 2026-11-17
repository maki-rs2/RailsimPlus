//
// FrameRenderer.h - トラック/シーン常駐/GPUインスタンス+WBOITの描画パス管理
//
// 「メッシュのGPUアップロードと描画パスの記録」を受け持つ。
// デバイス初期化・フレーム同期・Present は GpuContext
// が受け持ち、FrameRenderer は各 MeshPipeline/InstanceBatchManager/WboitPass の
// 所有とメッシュアップロード・コマンド記録のみを扱う。
//
#pragma once

#include <array>
#include <chrono>
#include <d3d12.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "../../render/effects/HeadlightFlare.h"
#include "../../render/effects/LensFlarePass.h"
#include "../../render/effects/LensFlareSwitches.h"
#include "../../render/effects/SunFlareSettings.h"
#include "../../render/instancing/InstanceBatchManager.h"
#include "../../render/instancing/TrainInstanceBatchManager.h"
#include "../../render/mesh/Mesh.h"
#include "../../render/pipeline/MeshConstants.h"
#include "../../render/pipeline/MeshFrameRecord.h"
#include "../../render/pipeline/MeshPipeline.h"
#include "../../render/pipeline/ViewConstants.h"
#include "../../render/pipeline/ViewConstantsBuffer.h"
#include "../../render/resource/ComputeSelfTest.h"
#include "../../render/resource/SrvRangeAllocator.h"
#include "../../render/sky/AerialPerspectiveLut.h"
#include "../../render/sky/AtmosphereLuts.h"
#include "../../render/sky/EnvCubeSet.h"
#include "../../render/sky/IblGenerator.h"
#include "../../render/sky/LandscapeCube.h"
#include "../../render/sky/SkyBodyPass.h"
#include "../../render/sky/SkyPass.h"
#include "../../render/overlay2d/Overlay2DPass.h"
#include "../../render/postprocess/AmbientOcclusionPass.h"
#include "../../render/postprocess/AoQuality.h"
#include "../../render/postprocess/SceneHdrTarget.h"
#include "../../render/postprocess/DepthOfFieldPass.h"
#include "../../render/postprocess/LuminanceMeterPass.h"
#include "../../render/postprocess/MotionBlurPass.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../render/postprocess/TaaPass.h"
#include "../../render/effects/VolumetricFogDebug.h"
#include "../../render/effects/VolumetricFogPass.h"
#include "../../render/effects/VolumetricFogSkyPass.h"
#include "../../render/postprocess/VelocityDebug.h"
#include "../../render/shadow/CascadeFit.h"
#include "../../render/shadow/ShadowMap.h"
#include "../../render/shadow/ShadowPass.h"
#include "../../render/shadow/ShadowDistance.h"
#include "../../render/shadow/ShadowQuality.h"
#include "../../render/wboit/WboitPass.h"
#include "../camera/CameraView.h"
#include "../../render/particle/ParticlePass.h"
#include "../diagnostics/CpuRenderBreakdown.h"
#include "CameraMath.h"
#include "FramePassTable.h"
#include "GpuFrameTimer.h"
#include "FrameHighlightState.h"
#include "PlacementPreviewState.h"
#include "SceneSlotState.h"

namespace rs::app {

class GpuContext;

class FrameRenderer {
public:
    // meshPipeline_/scenePipelines_[]/instanceBatches_/wboit_ を
    // 初期化する(GpuContextが作成済みのdevice/srvHeapを使う)。
    bool Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvStride,
              UINT width, UINT height, rs::rhi::RetirementQueue& retirements);

    // 既にロード済みの Mesh をメインのメッシュパイプラインへアップロードする(エディタの押し出しレール・架線用)。
    bool UploadMeshToGpu(GpuContext& gpu, const rs::render::Mesh& mesh);
    bool HasMesh() const { return meshPipeline_.HasMesh(); }

    // 第2描画パス（シーン常駐エンティティ）。単発呼び出し用(内部で専用の
    // ResourceUploadBatchを1回だけ回す。複数エンティティをまとめてアップロード
    // したい場合はEnsureSceneSlotCapacity+UploadSceneMeshWithBatchを使うこと)。
    bool UploadSceneMeshToGpu(GpuContext& gpu, UINT slot, const std::string& geometryKey,
                              const rs::render::Mesh& mesh);

    // scenePipelines_[slot]が未確保(slot >= scenePipelines_.size())なら
    // sceneAllocator_からSRVレンジを新規に払い出してMeshPipelineを初期化する
    // (遅延・成長のみで縮小はしない)。
    // プール枯渇時はfalseを返す。UploadSceneMeshWithBatchの前に呼ぶこと
    // (複数エンティティをまとめて1つのResourceUploadBatchへ積む経路向けに公開)。
    bool EnsureSceneSlotCapacity(GpuContext& gpu, UINT slot);

    // 呼び出し側が用意した1つのResourceUploadBatchへこのslot分のUploadMeshを積む。
    // EnsureSceneSlotCapacity()で容量確保済みであること。geometryKeyは常に
    // 非空(GeometryManager経由の共有ジオメトリ)を前提とする経路専用
    // (SceneRenderer::Syncのnon-static SceneEntity群。keepAlive_はこの経路では
    // 使われない=sharedGeometry_側が寿命管理するため、共有ResourceUploadBatch越しに
    // 複数スロット分呼んでも安全。エンティティごとにResetCmdList+CloseAndExecuteCmdList
    // +WaitForGpu×2で完全フラッシュすると、初期ロード時にエンティティ数分のGPU全停止待ちが
    // 直列に積み重なるため、この経路で1回にまとめる。
    // UploadMeshはdevice/uploadのみに依存し共有フレームコマンドリスト
    // cmdList_/cmdAlloc_には触れないため、この経路ではその保護は不要)。
    bool UploadSceneMeshWithBatch(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                  UINT slot, const std::string& geometryKey,
                                  const rs::render::Mesh& mesh);
    // continuityKey は、スロットの中身が入れ替わるたびに変わる値(速度の計算で、前回書いた行列を
    // 前フレームの行列として使ってよいかの判定に使う)。
    void SetScenePartWorlds(UINT slot, const std::vector<rs::render::PartXform>& xforms,
                            uint64_t continuityKey);
    // 書き換えモードで上げたスロット(Mesh::dynamicVertexCount > 0)の、書き換え範囲の頂点を渡す。
    // 個数が合わないとき、書き換えモードでないときは何もしない。GPU へは FlushScenePartWorlds が
    // 枠ごとに書く。カリング用の範囲(AABB)は、渡された頂点の分だけ広げる(縮めない)。
    void SetSceneDynamicVertices(UINT slot, const std::vector<rs::render::MeshVertex>& vertices);
    bool HasSceneMesh(UINT slot) const;
    bool UploadPlacementPreviewMesh(GpuContext& gpu, const rs::render::Mesh& mesh);
    void SetPlacementPreviewWorlds(const std::vector<rs::core::Mat4>& worlds);
    void SetPlacementPreviewActive(bool active) { placementPreviewState_.SetActive(active); }
    // スロットのメッシュをGPUから外す(SceneEntity削除時に呼ぶ)。
    void ClearSceneSlot(GpuContext& gpu, UINT slot);

    // ClearSceneSlot と同じ(WaitForGpu+ジオメトリ解放+可視状態復帰)だが、
    // テクスチャプールは可能なら保持する。同じフォルダのままスイッチ値だけ
    // 変える再利用でのみ使うこと(別フォルダへ差し替える場合は前プラグインの
    // テクスチャがVRAMに残り続けるため ClearSceneSlot を使う)。
    // requiredTexSlots だけ残枠が無ければ保持を諦めて破棄する。
    // 戻り値: テクスチャプールを保持できたか。
    bool ClearSceneSlotGeometryOnly(GpuContext& gpu, UINT slot, UINT requiredTexSlots);

    // スロット単位の表示ON/OFF(シーン間ワープで、別Sceneに属する編成車両の
    // GPUスロットを一時的に描画から外すために使う。既定はtrue。範囲外slotは
    // 無視する)。
    void SetSceneSlotVisible(UINT slot, bool visible);
    void SetSceneSlotFadeAlpha(UINT slot, float alpha);
    rs::render::MeshPipeline& ScenePipeline(UINT slot);

    // GPUインスタンス描画バッチ。
    rs::render::InstanceBatchManager& InstanceBatches() { return instanceBatches_; }
    rs::render::TrainInstanceBatchManager& TrainInstanceBatches() { return trainInstanceBatches_; }

    // Scenery選択ハイライト用。which<kHighlightSlots。
    // mesh識別子が変わった時だけ呼ぶこと(UploadMeshToGpuと同様、内部でWaitForGpuを
    // 伴う同期アップロード)。
    bool UploadHighlightMesh(GpuContext& gpu, UINT which, const rs::render::Mesh& mesh);
    // 毎フレーム呼んでよい(状態を保持するだけ、即時GPU操作は発生しない)。
    void SetHighlightActive(UINT which, bool active, const rs::core::Mat4& world,
                            const float tintColor[4]);

    // 非静的Scenery(SceneRenderer::slots_側)選択ハイライト用。which<kHighlightSlots(0=ホバー,1=選択)。
    // slotはApp::EnsureSceneSlotCapacity等で確保済みのscenePipelines_添字
    // (SceneRenderer::FindHighlightSlotForEntity参照)。アップロード不要
    // (scenePipelines_[slot]は既に自身のジオメトリ/gPartWorldを保持済み)。
    // 毎フレーム呼んでよい(状態を保持するだけ)。
    void SetSlotHighlight(UINT which, bool active, UINT slot, const float tintColor[4]);

    // ChangeTexture/ChangeMaterial の実行時反映用に MeshPipeline を
    // 直接操作したい呼び出し側向け。
    rs::render::MeshPipeline& Pipeline() { return meshPipeline_; }
    // 本編と共有するPSO・ルート署名(読み取り専用)。サムネイルの撮影が同じ描き方をするために使う。
    const rs::render::MeshPipelineSharedState& MeshPipelineShared() const { return meshPipelineShared_; }


    // 空(SkyPass)を描くかどうか。プラグインビューワーモードでは
    // 単体プラグイン表示に空背景が不要なため、起動時に1回falseを設定する用途。
    // 読込ジョブ進行中(PrepareFrame()のskipMeshPass)は、この値に関係なく常に空を
    // 描かない(半端な読込状態のままUIだけ描く画面に空だけ残るのを防ぐ)。
    void SetSkyEnabled(bool enabled) { skyEnabled_ = enabled; }
    // 太陽光の影を落とすか。プラグインビューワーモードでは起動時に1回falseを設定する用途。
    void SetShadowsAllowed(bool allowed) { shadowsAllowed_ = allowed; }
    // 影を実際に描く設定か(許可されていて、品質が「なし」でない)。システムスイッチ_SHADOWの値もこれで決める。
    // 影のマップ・パスを初期化できなかったときは、設定にかかわらず描かない。
    bool ShadowsEnabled() const {
        return shadowsAllowed_ && shadowQuality_ != rs::render::ShadowQuality::None && shadowMap_.Ready() &&
               shadowPass_.Ready();
    }
    // IBLを実際に使っているか(システムスイッチ_ENVMAPの値)。判定は BuildEnvironmentState の1か所で、
    // ここでは直近のフレームの結果を返す(描画の1フレーム目より前は偽)。
    bool EnvMapEnabled() const { return environmentState_.iblEnabled; }
    // ユーザー設定のシャドウ品質(起動時の読込直後と設定変更時に呼ぶ)。即時に反映する。
    // シャドウマップの大きさ・枚数が品質で変わるときは、呼び出し側が先に RebuildShadowMapFor で作り直す。
    void SetShadowQuality(rs::render::ShadowQuality quality) {
        shadowQuality_ = quality;
    }
    // 以下の4つの設定は、値を記録するだけで資源は作らない(設定変更を App がまとめて反映するため)。
    // 資源は EnsureSizedResources が、画面の大きさと設定に合わせて作る。
    // ユーザー設定のアンビエントオクルージョンの品質(起動時の読込直後と設定変更時に呼ぶ)。
    void SetAmbientOcclusion(rs::render::AoQuality quality) { aoQuality_ = quality; }
    rs::render::AoQuality AmbientOcclusionQuality() const { return aoQuality_; }
    // TAA を使うか(ユーザー設定の方式が TAA のとき)。
    void SetTemporalAa(bool enabled);
    bool TemporalAaEnabled() const { return temporalAa_; }
    // モーションブラーを使うか(ユーザー設定がオンのとき)。
    void SetMotionBlur(bool enabled) { motionBlurEnabled_ = enabled; }
    bool MotionBlurEnabled() const { return motionBlurEnabled_; }
    // ファインダー中の撮影カメラのシャッター速度[秒]。値があれば、モーションブラーの長さをシャッター速度に連動させる
    // (無ければ調整値のシャッターの割合)。モーションブラーを掛けるかどうかはユーザー設定のまま変えない。
    void SetPhotoShutterSeconds(std::optional<float> seconds) { photoShutterSeconds_ = seconds; }
    // ボリュメトリックフォグを使うか(ユーザー設定)。
    void SetVolumetricFog(bool enabled) { volumetricFogEnabled_ = enabled; }
    bool VolumetricFogEnabled() const { return volumetricFogEnabled_; }
    // 画面の大きさ・AO の品質・TAA/モーションブラー/フォグの有無に合わせて、深度プリパス・速度・AO・TAA・
    // モーションブラー・フォグの資源を作り直す(足りているものは何もしない)。呼ぶ前に GPU がそれらを使い終えていること。
    void EnsureSizedResources();
    // フォトモードの被写界深度(FrameRendererDepthOfField.cpp)。撮影カメラの値は、ファインダーが出ているときだけ渡す
    // (出ていないときは nullopt。開発用の環境変数 RS_DOF があれば、nullopt のときにその値を使う)。
    // 値は毎フレーム渡してよい。掛ける・掛けないの切り替わりだけが資源の作り直しの対象(呼び出し側が dirty を立てる)。
    void SetDepthOfFieldCamera(const std::optional<rs::render::DepthOfFieldCamera>& camera);
    // クリックでフォーカス: 描画した画面(ファインダーの枠の縦横比)の点(u, v。左上が 0、右下が 1)の奥行きを読み戻す。
    // 次に被写界深度が走るフレームで深度を1画素写し、PollDepthProbe が数フレーム後に奥行き[m]を返す(空は無限大)。
    void RequestDepthProbe(double u, double v);
    std::optional<double> PollDepthProbe();
    bool DepthOfFieldEnabled() const { return rs::render::DepthOfFieldActive(depthOfFieldCamera_.has_value()); }
    // TemporalAa の直後・モーションブラーの前に走る。ぼかし後の HDR(RenderTarget 状態。以降の段の入力)を返す。
    rs::render::SceneHdrTarget* ResolveDepthOfField(const rs::rhi::CommandList& commandList,
                                                    rs::render::SceneHdrTarget& scene, GpuFrameTimer* timer);
    // フォトモードの ISO オートの測光(FrameRendererLuminanceMeter.cpp)。使う間だけ true にする(切り替わりだけが資源を作る対象で、
    // 呼び出し側が dirty を立てる)。パスはレンズフレアの直後・ポストプロセスの前に走り、撮影カメラの露出を掛ける前のシーンを測る。
    void SetLuminanceMeter(bool enabled) { luminanceMeterEnabled_ = enabled; }
    bool LuminanceMeterEnabled() const { return luminanceMeterEnabled_; }
    void RecordLuminanceMeter(const rs::rhi::CommandList& commandList, rs::render::SceneHdrTarget& scene);
    // 測光の結果(数フレームに1回)。毎フレーム呼んで読み戻しの待ちを進める。
    std::optional<rs::render::LuminanceMeterGrid> PollLuminanceMeter();
    // モーションブラー(FrameRendererMotionBlur.cpp)。TemporalAa の直後・レンズフレアの前に走る。
    // ぼかし後の HDR(RenderTarget 状態。以降の段の入力)を返す。
    rs::render::SceneHdrTarget* ResolveMotionBlur(const rs::rhi::CommandList& commandList,
                                                  rs::render::SceneHdrTarget& scene, GpuFrameTimer* timer);
    // TAA の解決。シーンの記録の後・レンズフレアの前に走る。解決後の HDR(RenderTarget 状態。以降の段の入力)を返す。
    rs::render::SceneHdrTarget* ResolveTaa(const rs::rhi::CommandList& commandList, rs::render::SceneHdrTarget& hdr);
    // 品質に合う大きさ・枚数にシャドウマップを作り直す必要があるか(「なし」は今のマップを使う)。
    bool ShadowMapNeedsRebuild(rs::render::ShadowQuality quality) const;
    // シャドウマップを品質に合わせて作り直す。呼ぶ前に、GPUがマップを使い終わっていること。
    bool RebuildShadowMapFor(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                             rs::render::ShadowQuality quality);
    // ユーザー設定のシャドウ距離の選択肢(起動時の読込直後と設定変更時に呼ぶ)。即時に反映する。
    void SetShadowDistance(rs::render::ShadowDistance distance) { shadowDistance_ = distance; }
    // この回に空を描くか(LUT更新と空パスの両方がこの1判定に従う)。
    bool ShouldDrawSky(bool skipMeshPass) const { return skyEnabled_ && !skipMeshPass; }
    // 大気LUTとIBLを更新するか。空を描かないモード(プラグインビューワー)でも、環境光・反射に
    // IBLが要るので更新する。読込ジョブ進行中(skipMeshPass)だけ省く。
    bool ShouldUpdateEnvironment(bool skipMeshPass) const { return !skipMeshPass; }
    // レンズフレアのパスを記録するか。灯具のフレアは空を描かないモードでも出すので、読込ジョブ進行中だけ省く。
    // 太陽のフレアと白飛びは空があるときだけ足す(ShouldDrawSky)。
    bool ShouldDrawLensFlare(bool skipMeshPass) const { return !skipMeshPass; }

    // 大気LUT。App::Renderが、BeginFrameの後・HDRターゲットのBeginSceneより前にUpdateを呼ぶ
    // (LUT生成がRTVとビューポートを書き換えるため)。太陽の方向は観測者から見た向き。
    rs::render::AtmosphereLuts& Atmosphere() { return atmosphere_; }
    rs::render::IblGenerator& Ibl() { return ibl_; }
    // 標準の空の遠景キューブ。App::Init が Init を呼ぶ。
    rs::render::LandscapeCube& Landscape() { return landscape_; }
    // 遠景キューブの焼き込み(初回だけ)→IBLの更新(環境ソースへ遠景と写真の空の層を入れる)の順に記録する。
    // 環境(EnvironmentState)をその場で組み立て直し、写真の空の層を使うかはその結果に従う。
    // 大気LUTの更新の後に呼ぶ。何か記録したら true(RTV・ビューポート・シザーを書き換えている)。
    bool UpdateIbl(const rs::rhi::CommandList& commandList);
    // 大気LUT(透過率・多重散乱・Sky-View、月が出ているときは月用のSky-View)を更新する。月の有無と比は
    // 環境(EnvironmentState)の組み立て結果に従う(Render・UpdateIbl と同じ判定)。UpdateIbl より前に呼ぶ。
    // 何か記録したら true(RTV・ビューポート・シザーを書き換えている)。
    // cam は観測者の高さ(カメラの高さに追従)を決めるのに使う。
    bool UpdateAtmosphere(const rs::rhi::CommandList& commandList, const CameraView& cam);
    // 大気遠近(AP)のLUT(FrameRendererAerial.cpp)。カメラの向きに依存するので、毎フレーム作り直す。
    // カメラ・射影はメインの描画と同じものを渡す。大気LUTの更新(UpdateAtmosphere)の後に呼ぶ。
    // 何か記録したら true(RTV・ビューポート・シザーを書き換えている)。
    bool UpdateAerialPerspective(const rs::rhi::CommandList& commandList, const CameraView& cam, UINT width,
                                 UINT height);
    rs::render::AerialPerspectiveLut& AerialPerspective() { return aerialPerspective_; }
    // ユーザー設定の「大気遠近を使うか」(起動時の読込直後と設定変更時に呼ぶ)。
    // 偽のあいだはLUTを作らず、メッシュのシェーダも何も引かない。
    void SetAtmosphericPerspective(bool enabled) { atmosphericPerspective_ = enabled; }
    // ユーザー設定の「高さフォグを使うか」。大気遠近のスイッチとは独立。
    void SetHeightFog(bool enabled) { heightFog_ = enabled; }
    rs::render::EnvCubeSet& EnvCubes() { return envCubes_; }
    // プラグインの空の Sun.x・Moon.x。App::Init が Init を呼ぶ。
    rs::render::SkyBodyPass& SkyBodies() { return skyBodies_; }
    // 太陽のレンズフレアと白飛び。App::Init が InitLensFlare を呼ぶ。設定は Env が変わるたびに差し替える。
    bool InitLensFlare(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                       const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex);
    rs::render::LensFlarePass& LensFlare() { return lensFlare_; }
    rs::render::SunFlareSettings& SunFlare() { return sunFlare_; }
    // 今フレームに描く灯具のレンズフレア(アプリ層が集めた一覧。毎フレーム差し替える)。
    // レンズフレア・白飛びの効果スイッチ(起動時の読込直後と設定変更時に呼ぶ)。即時に反映する。
    void SetLensFlareSwitches(const rs::render::LensFlareSwitches& switches) { lensFlareSwitches_ = switches; }
    void SetHeadlightLights(std::vector<rs::render::HeadlightLight> lights) { headlights_ = std::move(lights); }
    rs::render::ParticlePass& Particles() { return particlePass_; }
    bool InitParticles(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                       const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex, std::size_t maxParticles);
    void SetParticles(const rs::render::ParticleFrameInput& input, bool enabled);
    void RecordParticles(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                         std::uint32_t frameIndex);
    // トーンマップの後に、rtv(表示用のUNORM形式)へ太陽のフレアと白飛びを足す。描くものが無ければ何もしない。
    // frameIndex は実行中のフレームと重ならない側の書き込み先を選ぶ番号。
    // includeSun が偽のときは太陽のフレアと白飛びを足さず、灯具のフレアだけを描く。
    void RecordLensFlare(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv, const CameraView& cam,
                         UINT width, UINT height, UINT frameIndex, bool includeSun);
    rs::core::Vec3 SunDirectionToSun() const { return sunDirection_ * -1.0f; }
    // 直近の Render が使った環境(EnvironmentState)で、プラグインの空が有効か。空パス・メッシュ・
    // トーンマップが同じ判定を見るための1か所(PrepareFrame で作る。App::Render がトーンマップの切替に読む)。
    bool PluginSkyActive() const { return environmentState_.pluginSky; }

    // 光源方向(光が進む向き。太陽が高いほどY成分は負に近づく)を設定する。
    // dayAlpha: rs::world::ComputeDayAlpha由来の昼夜係数(1=昼、0=完全な夜)。
    // メッシュの直射光・空描画の両方でこの値を使う。
    // SetHighlightActive と同様、毎フレーム問題なく呼べる(状態を保持するだけで
    // 即時GPU操作は発生しない)。GameClock/SolarPosition(rs::world)由来の
    // double値からのfloat変換は呼び出し側(main.cpp)の1箇所に集約する。
    void SetSunDirection(const rs::core::Vec3& direction, float dayAlpha = 1.0f) {
        sunDirection_ = direction.Normalized();
        dayAlpha_ = std::clamp(dayAlpha, 0.0f, 1.0f);
    }

    // 月の方向(観測者から見た向き。光が進む向きではない)と、満月を1とする明るさの比(rs::world::MoonState由来)。
    // 毎フレーム呼んでよい(状態を保持するだけ)。double→floatの変換は呼び出し側の1箇所に集約する。
    // 月の光を使うか(太陽高度・標準の空かどうか)は BuildEnvironmentState が1か所で決める。
    void SetMoon(const rs::core::Vec3& directionToMoon, float brightness) {
        moonDirection_ = directionToMoon.Normalized();
        moonBrightness_ = std::max(brightness, 0.0f);
    }

    // プラグインの空の入力(現在のEnvのLighting.Setを補間した色と、キューブ一式を読み終えたか)。
    // 毎フレーム呼べる。キューブのSRV番号・ミップ数は envCubes_ から描画時に補う。
    // 実際に有効になるかは BuildEnvironmentState(ユーザー設定・RS_IBL・available)が1か所で決める。
    void SetPluginSkyInput(const rs::render::PluginSkyInput& input) { pluginSkyInput_ = input; }
    // プラグインの空を選ぶか(既定は真。--setting skyMode=standard のときだけ偽)。真でも、シーンの環境がキューブ付きでなければ標準の空になる。
    void SetPluginSkyPreferred(bool preferred) { pluginSkyPreferred_ = preferred; }

    // meshPipeline_ が持つ深度バッファのDSV(トラック/列車/シーン/インスタンス
    // バッチの全パスが共有する)。GpuContext::BeginFrame() へ渡す。
    D3D12_CPU_DESCRIPTOR_HANDLE DepthDsv() const;

    // 1フレームの記録は、FramePassTable.h の表の順に App::Render が回す。ここは各パスの記録と、その前の
    // CPU側の準備(PrepareFrame)を受け持つ。GpuContext::BeginFrame() で描画対象・ビューポート・SRVヒープを
    // 設定済みのコマンドリストへ、HDRシーンターゲット(SceneHdrTarget::BeginScene済み)のrtvへ書く。
    // バックバッファへ書くのは App::Render のトーンマップ以降だけで、ここでは書かない。timer は性能調査用の
    // パス別GPU計測(nullptrなら計測しない、GpuContext::BeginFrame()が返す FrameTargets::timer をそのまま渡す)。
    // 半透明の合成が背景として読むシーンターゲット(rtv の実体)。使い終わるまで生きていること。
    void SetSceneHdrTarget(rs::render::SceneHdrTarget* target) { sceneHdrTarget_ = target; }
    // 環境の組み立て・カリング・定数・影の準備(CPUのみ)をして、今フレームの有効条件のうち描画側が決めるもの
    // (空・メッシュ・プリパス・速度・AO・影・フォグ・TAA・モーションブラー・レンズフレア)を flags へ立てる。
    // skipMeshPass=trueの間(レイアウト読込・Scene切替のフレーム分割ジョブ進行中)は、空だけを描き、
    // メッシュ描画一式を省く(半端に更新途中のスロットを描いてしまうのを避ける)。
    void PrepareFrame(const CameraView& cam, rs::rhi::CpuDescriptor rtv, UINT width, UINT height,
                      bool skipMeshPass, FramePassFlags& flags);
    void RecordSky(const rs::rhi::CommandList& commandList);
    // 主視点の深度プリパス。本編が深度を書く枠だけ(フェード中の枠・配置プレビュー・ハイライトは含めない)。
    void RecordPrepass(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);
    // 速度パス(FrameRendererVelocity.cpp)。深度プリパスの直後に、動いているパーツだけをプリパスの深度に沿って
    // 速度バッファへ書く(動く物が無くても、速度バッファは毎フレーム0でクリアする)。
    void RecordVelocity(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);
    void RecordAmbientOcclusion(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);
    // プリパス系が書き換えた出力先とビューポートを、本描画の分(HDRシーン+主深度、画面の大きさ)へ戻す。
    void ResetMainTargets(const rs::rhi::CommandList& commandList);
    void RecordShadow(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);
    void RecordVolumetricFog(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);
    void RecordComputeSelfTest(const rs::rhi::CommandList& commandList);
    // 不透明・半透明(WBOIT)・後段不透明・選択ハイライト。
    void RecordMesh(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);
    void RecordOverlay2D(ID3D12GraphicsCommandList* cmdList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                         const rs::core::DebugDrawList& list, const CameraView& cam,
                         UINT width, UINT height, UINT frameIndex);

    // ウィンドウリサイズ時、深度バッファ+WBOITオフスクリーンRTを作り直す。
    void OnResize(ID3D12Device* device, UINT width, UINT height);

    // 直近のフレームのCPU区間の内訳(rendererMs・cullMs・batchSyncMs・shadowCpuMs・meshRecordMsだけ埋まる)。
    const CpuRenderBreakdown& LastCpuBreakdown() const { return cpuBreakdown_; }

private:
    bool warnedScenePoolExhaustion_ = false;
    CpuRenderBreakdown cpuBreakdown_;

    // PrepareFrame が作り、同じフレームの各パスの記録が読む今フレームの入力。
    struct FrameState {
        CameraView cam{};
        ViewProjResult viewProj;
        rs::render::MeshConstants constants;      // 環境・視点・影・AO・フォグの値を、パスが順に書き足していく
        rs::render::ViewConstants mainView;
        rs::rhi::CpuDescriptor rtv;               // HDRシーンのRTV
        UINT width = 0;
        UINT height = 0;
        UINT frameIndex = 0;                      // 定数バッファ・バッチのリングの番号
        std::uint64_t counter = 0;                // フレームの通し番号(増やす前の値)
        bool anyInstanceBatch = false;
        bool anyTrainInstanceBatch = false;
        float motionBlurFrameSeconds = 0.0f;      // 前回からの時間(モーションブラーのシャッター)
    };
    FrameState frame_;

    // PrepareFrame のうち、メッシュを描くフレームだけの準備(カリング・定数・バッチ同期・プリパス/影/TAA/フォグの判定)。
    void PrepareMeshFrame(FramePassFlags& flags);

    // 環境(IBL・太陽色・プラグインの空・写真の空の層)を environmentState_ へ組み立てる。Render と UpdateIbl が
    // 同じフレームで同じ結果を見るよう、組み立てはここ1か所(判定は BuildEnvironmentState の中)。
    void BuildEnvironment();

    // 大気遠近(FrameRendererAerial.cpp)。使うかの判定(設定と環境変数)はここ1か所。
    bool AerialPerspectiveActive() const;
    // 空とAPのLUTを積分する観測者の高さ[km、地表から]。追従を切っていれば基準の高さ。
    float ObserverHeightKm(const CameraView& cam) const;
    // LUTを引く入力(直近に作ったLUTとそのカメラ)を MeshConstants と ViewConstants(カメラの基底)へ詰める。
    // 使わないときは何も引かない値にする。
    void ApplyAerialPerspectiveConstants(rs::render::MeshConstants& constants, rs::render::ViewConstants& view) const;
    // 高さフォグ(同じ FrameRendererAerial.cpp)。使うかの判定(設定と環境変数)はここ1か所。
    bool HeightFogActive() const;
    void ApplyHeightFogConstants(rs::render::MeshConstants& constants) const;

    // シャドウ(FrameRendererShadow.cpp)。
    // 影のマップ・パスを作る。マップは起動時の既定の品質の大きさ。
    bool InitShadow(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    // このフレームに影パスを描くかを決める(影を使うかの判定はここ1か所)。描く場合は、影定数(b1)を書いて
    // constants.shadowConstantsAddress へ入れ、影を落とすものの一覧とカスケードの行列を shadow* メンバーに用意して true を返す。
    bool PrepareShadow(const CameraView& cam, const ViewProjResult& viewProj, UINT width, UINT height,
                       rs::render::MeshConstants& constants, UINT frameIndex);

    // 深度プリパス・速度パス・アンビエントオクルージョン(FrameRendererAmbientOcclusion.cpp)。
    // プリパスは「AO を掛ける または 速度を使う」のとき、速度パスは速度を使うとき(VelocityWanted)に走る。
    // 判定は PrepareFrame の1か所で、プリパスが走るときだけ速度パスも走る。
    // 速度バッファの入れ物と深度プリパスの出力の入れ物を作る(資源は EnsureSizedResources が必要なときに作る)。
    bool InitPrepassTargets(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    // TAA(FrameRendererTaa.cpp)。解決パスの初期化・資源・ジッタと履歴の管理。
    bool InitTaa(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    bool InitMotionBlur(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    bool InitDepthOfField(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    bool InitLuminanceMeter(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    void UpdateTaaResources();
    // TAA を解決できる状態か(TAA の設定がオンで、今フレームの入力の逆行列が求まり、解決パスの資源が揃っている)。
    bool TemporalAaReady() const;
    // モーションブラーをかけられる状態か(設定・シャッター・視点の瞬間移動・資源。速度の有無は呼び出し側が見る)。
    bool MotionBlurReady() const;
    // 前回の呼び出しからの時間を測る(フレーム時間の算出用)。毎フレーム1回、PrepareFrame が呼ぶ。
    void UpdateMotionBlurClock();
    // 主視点の ViewConstants を仕上げる: 前フレームの viewProj を入れ、TAA のときはジッタをかけて解決パスの入力を作る。
    void PrepareTaaView(const CameraView& cam, const ViewProjResult& viewProj, UINT width, UINT height,
                        rs::render::ViewConstants& mainView);
    // 速度を使うか(TAA・モーションブラー、または開発用の RS_DEBUG_VELOCITY=1)。
    bool VelocityWanted() const;
    // 速度を使う効果があるか(AO を除く。TAA・モーションブラー)。
    bool PrepassEffectActive() const { return temporalAa_ || motionBlurEnabled_; }
    void UpdateMotionBlurResources();
    void UpdateDepthOfFieldResources();
    void UpdateLuminanceMeterResources();
    // 被写界深度をかけられる状態か(撮影カメラの値・今フレームの入力の逆行列・資源)。
    bool DepthOfFieldReady() const;
    // ボリュメトリックフォグ(FrameRendererVolumetricFog.cpp)。
    bool InitVolumetricFog(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap);
    bool VolumetricFogActive() const;
    // 画面の大きさに合う格子があり、フォグを作れるか。
    bool VolumetricFogReady(UINT width, UINT height) const;
    void UpdateVolumetricFogResources();
    // 格子を作ったフレームだけ、不透明の中で空へ当てる。
    void RecordVolumetricFogSky(const rs::rhi::CommandList& commandList, const rs::render::MeshConstants& constants) const;
    // 出力先とビューポートを本描画の分(HDRシーン+主深度、画面の大きさ)へ張り直す。
    void RestoreMainTargets(const rs::rhi::CommandList& commandList) const;

    void ClearSceneSlotInternal(GpuContext& gpu, UINT slot, bool discardTextures);
    void FlushScenePartWorlds(UINT frameIndex);
    void RecordHighlights(ID3D12GraphicsCommandList* cmdList, const rs::render::MeshConstants& constants,
                          UINT frameIndex);
    // このフレームに描く枠(メインのメッシュ+描画対象のスロット)を drawEntries_ に組み立てる。
    // カリング結果の更新後に1回だけ呼ぶ。不透明・半透明・後段不透明の各パスはこの一覧を1本のループで回す。
    void BuildDrawEntries();
    void RecordMeshPasses(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer);

    // root signature/PSO一式(1回だけInit、全MeshPipeline/InstanceBatchが共有参照し、per-slot/per-batchの重複生成を避ける)。
    rs::render::MeshPipelineSharedState meshPipelineShared_;
    rs::render::MeshPipeline          meshPipeline_;
    rs::render::Mesh                  mesh_;
    std::vector<rs::render::MeshPipeline> scenePipelines_; // 第2描画パス（シーン常駐）
    std::vector<rs::render::MeshDrawEntry> drawEntries_;    // BuildDrawEntries が毎フレーム作り直す(確保は使い回す)
    std::vector<SceneSlotMeshBounds>      sceneMeshBounds_;
    std::vector<std::vector<rs::render::PartXform>> scenePartWorlds_;
    // 書き換えモードのスロットへ毎フレーム書く頂点の写し(スロットごと)。dirty は GPU の枠ごとの書き直し待ち。
    struct SceneDynamicVertices {
        std::vector<rs::render::MeshVertex> vertices;
        std::array<bool, 2>                 dirty = {false, false};
        uint32_t                            expectedCount = 0; // 上げたメッシュの dynamicVertexCount
    };
    std::vector<SceneDynamicVertices>  sceneDynamicVertices_;
    SceneSlotState                    sceneSlotState_;
    rs::render::SrvRangeAllocator     sceneAllocator_; // シーン常駐スロットのSRVレンジ払い出し元
    PlacementPreviewState             placementPreviewState_;
    rs::render::InstanceBatchManager  instanceBatches_; // GPUインスタンス描画
    rs::render::TrainInstanceBatchManager trainInstanceBatches_;
    rs::render::SceneHdrTarget*       sceneHdrTarget_ = nullptr; // 所有しない
    rs::render::WboitPass             wboit_;          // 半透明の順序非依存合成
    rs::render::AmbientOcclusionPass  ambientOcclusion_; // GTAO+ぼかし(深度は prepassDepth_ を読む)
    rs::render::AoQuality             aoQuality_ = rs::render::AoQuality::None; // SetAmbientOcclusion参照
    rs::render::PrepassDepthTarget    prepassDepth_;   // 主視点の深度プリパスの出力。AO と TAA(速度パス)が使う
    rs::render::SceneHdrTarget        velocity_;       // 速度バッファ(物体の動きだけ。Rg16Float、画面と同じ大きさ)
    rs::render::VelocityDebug         velocityDebug_;  // 速度バッファの読み戻し(RS_DEBUG_VELOCITY=1のときだけ)
    rs::render::ComputeSelfTest       computeSelfTest_; // compute の基盤の確認(RS_DEBUG_COMPUTE=1のときだけ)
    bool                              temporalAa_ = false; // TAA を使うか(SetTemporalAa 参照)
    std::chrono::steady_clock::time_point lastMotionBlurTime_{}; // 前回 ResolveMotionBlur を呼んだ時刻(フレーム時間の算出用)
    bool                              motionBlurEnabled_ = false; // モーションブラーを使うか(SetMotionBlur 参照)
    std::optional<float>              photoShutterSeconds_;       // 撮影カメラのシャッター速度(SetPhotoShutterSeconds 参照)
    rs::render::MotionBlurPass        motionBlur_;     // モーションブラーのパス
    struct DepthProbeState {
        bool requested = false; // 写す要求が残っている
        bool recorded = false;  // 写して、読み戻し待ち
        double u = 0.0, v = 0.0;
        unsigned x = 0, y = 0;  // 写した画素
        unsigned width = 0, height = 0;
        float invViewProj[16] = {}; // 写したフレームの逆行列(あとで奥行きにするため)
    } depthProbe_;
    std::optional<rs::render::DepthOfFieldCamera> depthOfFieldCamera_; // 被写界深度を掛けるときの撮影カメラの値(SetDepthOfFieldCamera 参照)
    rs::render::DepthOfFieldPass      depthOfField_;   // 被写界深度のパス
    bool                              luminanceMeterEnabled_ = false; // ISO オートの測光を走らせるか(SetLuminanceMeter 参照)
    rs::render::LuminanceMeterPass    luminanceMeter_; // ISO オートの測光のパス
    bool                              volumetricFogEnabled_ = false; // ボリュメトリックフォグを使うか
    rs::render::VolumetricFogPass     volumetricFog_;  // ボリュメトリックフォグの格子
    rs::render::VolumetricFogSkyPass  volumetricFogSky_; // 空へ当てる全画面のパス
    rs::render::VolumetricFogDebug    volumetricFogDebug_; // 格子の読み戻し(RS_DEBUG_VOLUMETRIC_FOG=1のときだけ)
    float                             framePrevViewProj_[16] = {}; // 今フレームの主視点の前フレームの viewProj(ジッタなし)
    bool                              viewJumped_ = false; // 視点が瞬間移動した(前フレームからの移動が taaCutDistanceM 超)
    rs::render::TaaPass               taa_;            // TAA の解決パスと履歴
    rs::render::TaaFrameParams        taaFrame_;       // PrepareTaaView が作り、ResolveTaa が使う今フレームの入力
    rs::core::Mat4                    taaInvViewProj_; // ジッタなしの viewProj の逆行列
    bool                              taaFrameOk_ = false;      // taaFrame_ が使えるか(逆行列が求まった)
    bool                              taaHistoryValid_ = false; // 履歴が使えるか(画面の大きさの変更・瞬間移動・入力なしのフレームで偽)
    float                             prevEye_[3] = {};  // 前フレームの視点位置
    bool                              prevEyeValid_ = false;
    rs::rhi::SrvHeap                  mainSrvHeap_;    // 本編の深度の SRV を書くための SRV ヒープ
    float                             prevViewProj_[16] = {}; // 前フレームの主視点の viewProj(ジッタなし)
    bool                              prevViewProjValid_ = false; // prevViewProj_ が使えるか(初回・画面の大きさ変更で偽)
    UINT                              targetWidth_ = 0;   // 描画先の大きさ(Init・OnResizeで更新)
    UINT                              targetHeight_ = 0;
    rs::render::AtmosphereLuts        atmosphere_;     // 大気LUT(透過率・多重散乱・Sky-View)
    rs::render::AerialPerspectiveLut  aerialPerspective_; // 大気遠近(AP)のLUT(カメラの視錐台を距離で区切る)
    bool                              atmosphericPerspective_ = true; // SetAtmosphericPerspective参照(設定の既定と同じ)
    bool                              heightFog_ = true;              // SetHeightFog参照(設定の既定と同じ)
    rs::render::atmosphere::AerialCameraFrame lastAerialCamera_; // 直近に作ったLUTのカメラ(メッシュが同じ視線で引く)
    bool                              aerialCameraValid_ = false;    // LUTを一度でも作ったか
    rs::render::SkyPass               skyPass_;        // 大気LUTを視線方向で引く空
    rs::render::IblGenerator          ibl_;            // 環境ソース・拡散・鏡面キューブとBRDF LUT
    rs::render::LandscapeCube         landscape_;      // 標準の空の組み込み遠景(環境ソースへ焼き込む)
    rs::render::EnvCubeSet            envCubes_;       // Envプラグインの焼き済みキューブ一式
    rs::render::SkyBodyPass           skyBodies_;      // プラグインの空の Sun.x・Moon.x(空の直後に描く)
    rs::render::LensFlarePass         lensFlare_;      // 太陽のレンズフレアと白飛び(トーンマップの後に描く)
    rs::render::SunFlareSettings      sunFlare_;       // lensFlare_ が描く内容(SunFlare参照)
    rs::render::LensFlareSwitches lensFlareSwitches_;      // SetLensFlareSwitches参照
    std::vector<rs::render::HeadlightLight> headlights_;  // 灯具のレンズフレア(SetHeadlightLights)
    bool                              headlightOverflowWarned_ = false;
    rs::render::ParticlePass           particlePass_;
    rs::render::ParticleBatchBuilder   particleBatch_;
    bool                              particlesEnabled_ = false;
    bool                              particleOverflowWarned_ = false;
    rs::rhi::SrvHeap                  lensFlareSrvHeap_; // 深度のSRVを書き直す(リサイズ)ためのヒープ
    rs::render::Overlay2DPass         overlay2DPass_;
    bool                              skyEnabled_ = true; // SetSkyEnabled参照
    // 太陽光のシャドウ。品質は起動時に決まり(render_tuning.json)、資源は品質が「なし」でなければ作る。
    bool                              shadowsAllowed_ = true; // SetShadowsAllowed参照
    rs::render::ShadowQuality         shadowQuality_ = rs::render::kDefaultShadowQuality; // SetShadowQuality参照
    rs::render::ShadowDistance        shadowDistance_ = rs::render::kDefaultShadowDistance; // SetShadowDistance参照
    rs::render::ShadowMap             shadowMap_;
    rs::render::ShadowPass            shadowPass_;
    // 視点の定数(b2)。1フレームに、メインの画面・影のカスケード・プローブ撮影用の予約の順に枠を使う。
    rs::render::ViewConstantsBuffer   viewConstants_;
    static constexpr std::uint32_t kViewSlotMain = 0;
    static constexpr std::uint32_t kViewSlotShadowFirst = 1;
    static constexpr std::uint32_t kViewSlotCount = kViewSlotShadowFirst + rs::render::kMaxShadowCascades + 1;
    // カスケード1枚ぶんの影パスの入力。PrepareShadow が毎フレーム作る。
    struct ShadowCascadeDraw {
        rs::core::Mat4 viewProj = rs::core::Mat4::Identity();
        rs::core::Frustum lightFrustum; // バッチの範囲判定用
        std::vector<rs::render::ShadowCaster> casters;
        bool update = false;
    };
    std::array<ShadowCascadeDraw, rs::render::kMaxShadowCascades> shadowCascades_;
    std::array<rs::render::CascadeFitCascade, rs::render::kMaxShadowCascades> retainedShadowCascades_;
    std::uint32_t                     shadowCascadeCount_ = 0;
    std::uint32_t                     shadowScheduleCascadeCount_ = 0;
    std::uint64_t                     shadowFrameCounter_ = 0;
    bool                              shadowScheduleHistoryValid_ = false;
    rs::core::Vec3                    lastFullShadowSunDirection_;
    // 既定値は{-0.4,-0.8,-0.4}(GameClock未接続時の見た目を決める初期値。
    // SetSunDirection未呼び出しの経路でもこの値を使う)。
    rs::core::Vec3                    sunDirection_ = rs::core::Vec3(-0.4f, -0.8f, -0.4f).Normalized();
    // 昼夜係数(1=昼、0=完全な夜)。SetSunDirection未呼び出しの経路では
    // 常時昼になるよう既定1.0。
    float                              dayAlpha_ = 1.0f;
    // 月(SetMoon参照)。明るさ0=月なし(SetMoon未呼び出しの経路は月なし)。
    rs::core::Vec3                     moonDirection_ = rs::core::Vec3(0.0f, 1.0f, 0.0f);
    float                              moonBrightness_ = 0.0f;
    rs::render::PluginSkyInput         pluginSkyInput_;  // SetPluginSkyInput参照
    bool                               pluginSkyPreferred_ = true; // SetPluginSkyPreferred参照
    rs::render::EnvironmentState       environmentState_; // PrepareFrame で毎フレーム作る(PluginSkyActive参照)

    // Scenery選択ハイライト用。0=ホバー, 1=選択。
    // SceneEntity用の可変長スロットとは独立した固定パイプライン(App.h
    // kHighlightSlotCount参照)。
    static constexpr UINT kHighlightSlots = FrameHighlightState::kHighlightSlots;
    FrameHighlightState                highlightState_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> keepAlive_; // アップロード用の生存管理
    UINT64                            frameCounter_ = 0;

    // scenePipelines_[slot]が現在描画対象かどうか(範囲外slotはtrue扱い)。
    bool IsSceneSlotVisible(size_t slot) const;
    bool IsSceneSlotDrawable(size_t slot) const;
};

} // namespace rs::app
