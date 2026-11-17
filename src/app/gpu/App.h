//
// App.h - DX12レンダリングの薄いファサード
//
// デバイス初期化/フレーム同期/Present は GpuContext、3パス描画の記録+メッシュ
// アップロードは FrameRenderer が担う(「DX12初期化」と「3パス描画」の2責務を
// 別クラスへ分け、App はそれらへ委譲するだけにしている)。
// SRVヒープのレイアウトは SrvLayout.h が正典。
//
#pragma once

#include <d3d12.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "../camera/CameraView.h"
#include "FramePassTable.h"
#include "FrameRenderer.h"
#include "GpuContext.h"
#include "RenderRebuild.h"
#include "SrvLayout.h"
#include "../../core/Math.h"
#include "../../render/postprocess/SceneHdrTarget.h"
#include "../../render/postprocess/AmbientOcclusionPass.h"
#include "../../render/postprocess/AoQuality.h"
#include "../../render/postprocess/PostProcessChain.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../render/postprocess/TaaPass.h"
#include "../../render/postprocess/TonemapPass.h"
#include "../../render/resource/SrvRangeAllocator.h"
#include "../../render/sky/AtmosphereLuts.h"
#include "../../core/DebugDraw.h"

namespace rs::app::rmlui {
class RmlUiIntegration;
}

namespace rs::app {

// kFrameCount/kBackBufferFormat は GpuContext.h が正典(GpuContext/FrameRenderer
// 双方の初期化に必要なため)。ここでは rs::app::kFrameCount 等として引き続き
// 参照できるよう GpuContext.h を include している。

class App {
public:
    // viewerBackground: プラグインビューアーモードの背景色(昼の kViewerBackgroundDaySrgb から始める)にする。
    // HDRシーンターゲットの消去値は作成時に決まるため、Init で渡す。
    bool Init(HWND hwnd, UINT width, UINT height, bool viewerBackground = false);
    void Shutdown();
    // skipMeshPass=trueの間はメッシュ描画一式(FrameRenderer::PrepareFrame参照)を省き、
    // UI(ImGui)のみ描く(レイアウト読込・Scene切替のフレーム分割ジョブ進行中)。
    // 既定はfalse。
    void Render(const CameraView& cam, bool skipMeshPass = false);
    void OnResize(UINT width, UINT height);
    void WaitForGpu() { gpu_.WaitForGpu(); }
    // デバイスロストを検出したか。立ったあとの Render は何もしない(保存と終了は呼び出し側が受け持つ)。
    bool DeviceLost() const { return gpu_.DeviceLost(); }
    // Envプラグインの焼き済みキューブ一式(EnvCubeController がアップロードする)。
    rs::render::EnvCubeSet& EnvCubes() { return renderer_.EnvCubes(); }
    // プラグインの空の Sun.x・Moon.x(EnvCubeController が同じEnvの世代に合わせてアップロードする)。
    rs::render::SkyBodyPass& SkyBodies() { return renderer_.SkyBodies(); }
    rs::render::LensFlarePass& LensFlare() { return renderer_.LensFlare(); }
    rs::render::SunFlareSettings& SunFlare() { return renderer_.SunFlare(); }
    void SetHeadlightLights(std::vector<rs::render::HeadlightLight> lights) { renderer_.SetHeadlightLights(std::move(lights)); }
    rs::render::ParticlePass& Particles() { return renderer_.Particles(); }
    void SetParticles(const rs::render::ParticleFrameInput& input, bool enabled) { renderer_.SetParticles(input, enabled); }

    ID3D12Device* Device() const { return gpu_.Device(); }
    ID3D12DescriptorHeap* SrvHeap() const { return gpu_.SrvHeap(); }
    UINT SrvStride() const { return gpu_.SrvStride(); }
    rs::core::DebugDrawList& DebugDrawList() { return debugDrawList_; }

    // 既にロード済みの Mesh をメインのメッシュパイプラインへアップロードする(エディタの押し出しレール・架線用)。
    bool UploadMeshToGpu(const rs::render::Mesh& mesh) {
        return renderer_.UploadMeshToGpu(gpu_, mesh);
    }
    // メインのメッシュパイプラインにメッシュが載っているか。
    bool HasModel() const { return renderer_.HasMesh(); }

    // 第2描画パス（シーン常駐エンティティ）。
    // slot は呼び出し側(SceneEntityDrawUnit相当)が自前で管理する連番(0起点、
    // 空きスロットの再利用込み)。未確保のslotが渡された場合はSrvRangeAllocator
    // 経由でSRVレンジを遅延確保する。プール枯渇時はfalseを返す。
    // geometryKey は GeometryManager での頂点/インデックスバッファ dedup キー
    // (基本は entity.folderUtf8)。同一キーが複数スロットへ渡ると GPU
    // 頂点/インデックスバッファを共有する([[GeometryManager]])。
    bool UploadSceneMeshToGpu(UINT slot, const std::string& geometryKey,
                              const rs::render::Mesh& mesh) {
        return renderer_.UploadSceneMeshToGpu(gpu_, slot, geometryKey, mesh);
    }

    // 複数エンティティ分をまとめてGPUへ流す経路(SceneRenderer::Sync向け)。
    // EnsureSceneSlotCapacityでスロット確保だけ済ませておき、その後
    // RunUploadCommandsの1回のResourceUploadBatch内でUploadSceneMeshWithBatchを
    // 必要数分呼ぶ(エンティティごとの完全GPUフラッシュを1回にまとめるため)。
    bool EnsureSceneSlotCapacity(UINT slot) { return renderer_.EnsureSceneSlotCapacity(gpu_, slot); }
    bool UploadSceneMeshWithBatch(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                  UINT slot, const std::string& geometryKey,
                                  const rs::render::Mesh& mesh) {
        return renderer_.UploadSceneMeshWithBatch(device, upload, slot, geometryKey, mesh);
    }
    void SetScenePartWorlds(UINT slot, const std::vector<rs::render::PartXform>& xforms,
                            uint64_t continuityKey) {
        renderer_.SetScenePartWorlds(slot, xforms, continuityKey);
    }
    bool HasSceneMesh(UINT slot) const { return renderer_.HasSceneMesh(slot); }
    void SetSceneDynamicVertices(UINT slot, const std::vector<rs::render::MeshVertex>& vertices) {
        renderer_.SetSceneDynamicVertices(slot, vertices);
    }
    bool UploadPlacementPreviewMesh(const rs::render::Mesh& mesh) {
        return renderer_.UploadPlacementPreviewMesh(gpu_, mesh);
    }
    void SetPlacementPreviewWorlds(const std::vector<rs::core::Mat4>& worlds) {
        renderer_.SetPlacementPreviewWorlds(worlds);
    }
    void SetPlacementPreviewActive(bool active) { renderer_.SetPlacementPreviewActive(active); }
    // スロットのメッシュをGPUから外す(SceneEntity削除時に呼ぶ)。
    void ClearSceneSlot(UINT slot) { renderer_.ClearSceneSlot(gpu_, slot); }
    bool ClearSceneSlotGeometryOnly(UINT slot, UINT requiredTexSlots) {
        return renderer_.ClearSceneSlotGeometryOnly(gpu_, slot, requiredTexSlots);
    }
    rs::render::MeshPipeline& ScenePipeline(UINT slot) { return renderer_.ScenePipeline(slot); }
    // スロット単位の表示ON/OFF(シーン間ワープで別Sceneに属する編成車両を
    // 一時的に描画から外すために使う)。
    void SetSceneSlotVisible(UINT slot, bool visible) { renderer_.SetSceneSlotVisible(slot, visible); }
    void SetSceneSlotFadeAlpha(UINT slot, float alpha) { renderer_.SetSceneSlotFadeAlpha(slot, alpha); }

    // GPUインスタンス描画バッチ(EnsureBatch/AddInstance で配置する)。
    rs::render::InstanceBatchManager& InstanceBatches() { return renderer_.InstanceBatches(); }
    rs::render::TrainInstanceBatchManager& TrainInstanceBatches() { return renderer_.TrainInstanceBatches(); }

    // Scenery選択ハイライト用。which=0:ホバー
    // 1:選択。mesh識別子が変わった時だけ呼ぶこと(UploadMeshToGpuと同様に内部で
    // WaitForGpuを伴う同期アップロードのため、毎フレーム呼ぶと重い)。
    bool UploadHighlightMesh(UINT which, const rs::render::Mesh& mesh) {
        return renderer_.UploadHighlightMesh(gpu_, which, mesh);
    }
    // active=falseならその回のRender()で描かない(直近のUploadHighlightMeshは
    // 保持されるため、再度trueにした際に再アップロード不要)。tintColor=
    // rgb塗り色+a濃さ。毎フレーム呼んでよい(状態を保持するだけで即時GPU操作は
    // 発生しない、SceneRenderer::Tick→SetScenePartWorldsと同じ扱い)。
    void SetHighlightActive(UINT which, bool active, const rs::core::Mat4& world,
                            const float tintColor[4]) {
        renderer_.SetHighlightActive(which, active, world, tintColor);
    }
    // 非静的Scenery(SceneRenderer::slots_側)選択ハイライト用。slotはSceneRenderer::FindHighlightSlotForEntity参照。
    void SetSlotHighlight(UINT which, bool active, UINT slot, const float tintColor[4]) {
        renderer_.SetSlotHighlight(which, active, slot, tintColor);
    }

    // ChangeTexture/ChangeMaterial の実行時反映用に MeshPipeline を
    // 直接操作したい呼び出し側（main.cpp の material_runtime 結線）向け。
    rs::render::MeshPipeline& Pipeline() { return renderer_.Pipeline(); }
    // 本編と共有するPSO・ルート署名(読み取り専用。サムネイルの撮影用)。
    const rs::render::MeshPipelineSharedState& MeshPipelineShared() const { return renderer_.MeshPipelineShared(); }

    // 空(SkyPass)を描くかどうか。プラグインビューワーモードでは
    // 起動時に1回falseを設定する(単体プラグイン表示に空背景は不要)。
    void SetSkyEnabled(bool enabled) { renderer_.SetSkyEnabled(enabled); }
    // 影を落とすか。プラグインビューアーモードでは起動時に1回falseを設定する(単体表示に影は不要)。
    void SetShadowsAllowed(bool allowed) { renderer_.SetShadowsAllowed(allowed); }
    // 影を実際に描く設定か(システムスイッチ_SHADOWの値)。
    bool ShadowsEnabled() const { return renderer_.ShadowsEnabled(); }
    // IBLを実際に使っているか(システムスイッチ_ENVMAPの値)。
    bool EnvMapEnabled() const { return renderer_.EnvMapEnabled(); }
    // 以下の資源に関わる設定(シャドウ品質・AO・ポストプロセス・ビューアー背景)は、要求値を即座に反映し、
    // 資源の作り直しは dirty フラグ(RenderRebuild.h)に溜めて、次の Render の先頭で GPU の完了を1回待ってまとめて行う。
    // ユーザー設定のシャドウ品質(起動時の読込直後と設定変更時に呼ぶ)。
    void SetShadowQuality(rs::render::ShadowQuality quality);
    // ユーザー設定のシャドウ距離の選択肢(起動時の読込直後と設定変更時に呼ぶ)。
    void SetShadowDistance(rs::render::ShadowDistance distance) { renderer_.SetShadowDistance(distance); }
    // ユーザー設定のレンズフレア・白飛びのスイッチ(起動時の読込直後と設定変更時に呼ぶ)。
    void SetLensFlareSwitches(const rs::render::LensFlareSwitches& switches) { renderer_.SetLensFlareSwitches(switches); }
    // ユーザー設定のアンビエントオクルージョンの品質(起動時の読込直後と設定変更時に呼ぶ)。
    void SetAmbientOcclusion(rs::render::AoQuality quality);
    // ユーザー設定のポストプロセスのスイッチ(起動時の読込直後と設定変更時に呼ぶ。次のフレームから反映)。
    void SetPostProcessSettings(const rs::render::PostProcessSettings& settings);
    // ユーザー設定のVSync(起動時の読込直後と設定変更時に呼ぶ。次のPresentから反映)。
    void SetVSync(bool enabled) { gpu_.SetVSync(enabled); }
    bool VSyncEnabled() const { return gpu_.VSyncEnabled(); }
    // ユーザー設定のFPS上限(0=無制限)。VSyncがオフのときだけフレームループが使う。
    void SetFpsLimit(int fpsLimit) { fpsLimit_ = fpsLimit; }
    int FpsLimit() const { return fpsLimit_; }
    // プラグインビューアーの背景色をライトに合わせる(ON=昼の色、OFF=夜の色)。毎フレーム呼んでよく、
    // 変わったときだけHDRシーンターゲットを作り直す。ビューアーモード以外では何もしない。
    void SetViewerBackground(bool lightOn);

    // GameClock/SolarPosition(rs::world)由来の太陽方向を反映する。
    // dayAlpha: rs::world::ComputeDayAlpha由来の昼夜係数(1=昼、0=nightThresholdSin
    // 以下の完全な夜)。光源強度・空色描画の両方に使う(既定1.0=常時昼相当)。
    // 毎フレーム呼んでよい(SetHighlightActiveと同様、状態を保持するだけ)。
    void SetSunDirection(const rs::core::Vec3& direction, float dayAlpha = 1.0f) {
        renderer_.SetSunDirection(direction, dayAlpha);
    }
    // 月の方向(観測者から見た向き)と満月を1とする明るさの比を反映する。毎フレーム呼んでよい。
    // 月の光を使うか(標準の空・太陽が低いとき)は描画側が決める(FrameRenderer::SetMoon参照)。
    void SetMoon(const rs::core::Vec3& directionToMoon, float brightness) {
        renderer_.SetMoon(directionToMoon, brightness);
    }
    // プラグインの空の入力(現在のEnvのLighting.Set補間結果と、キューブ一式を読み終えたか)。毎フレーム呼んでよい。
    void SetPluginSkyInput(const rs::render::PluginSkyInput& input) { renderer_.SetPluginSkyInput(input); }
    // プラグインの空を選ぶか(既定は真)。--setting skyMode の指定があるときだけ呼ぶ。実際の空は、シーンの環境がキューブ付きかで決まる。
    void SetPluginSkyPreferred(bool preferred) { renderer_.SetPluginSkyPreferred(preferred); }
    // ユーザー設定の「大気遠近を使うか」(起動時の読込直後と設定変更時に呼ぶ)。
    void SetAtmosphericPerspective(bool enabled) { renderer_.SetAtmosphericPerspective(enabled); }
    // ユーザー設定の「高さフォグを使うか」(起動時の読込直後と設定変更時に呼ぶ)。
    void SetHeightFog(bool enabled) { renderer_.SetHeightFog(enabled); }

    // トーンマップ(HDR→バックバッファ)。サムネイルの撮影も同じPSO・設定を共用する。
    // 読み取り専用で、別スレッドの記録と並行して使ってよい。
    const rs::render::TonemapPass& Tonemap() const { return tonemap_; }
    // ユーザー設定のトーンマップの曲線(起動時の読込直後と設定変更時に呼ぶ。露出は変えない)。
    void SetTonemapMode(rs::render::TonemapMode mode) {
        rs::render::TonemapParams params = tonemap_.Params();
        params.mode = mode;
        tonemap_.SetParams(params);
    }
    // 色をバイト値で判定するヘッドレス検証用に、パススルー(saturateのみ)へ切り替える。
    // 通常の実行では呼ばない(設定画面または --setting tonemap が決める)。
    void SetTonemapPassthrough() {
        rs::render::TonemapParams params = tonemap_.Params();
        params.mode = rs::render::TonemapMode::Off;
        tonemap_.SetParams(params);
    }

    // テクスチャ事前ロード用の一時アップロードバッチを回す。呼び出し側は fn 内で
    // meshPipeline_.PreloadTexture() 等を呼ぶ（テクスチャのみなので cmdList_ は
    // 使わず、DirectXTK12 の ResourceUploadBatch が自前のコマンドリストで
    // 完結する。1回の Begin()...End() に全テクスチャをまとめて積むことで、
    // テクスチャ数分のキュー submission/fence 待ちを避ける）。
    template <typename Fn>
    void RunUploadCommands(Fn&& fn, UploadCompletion completion = UploadCompletion::Wait) {
        gpu_.RunUploadCommands(std::forward<Fn>(fn), completion);
    }

    template <typename Fn>
    void RunUploadCommandsChunked(size_t itemCount, size_t itemsPerBatch, Fn&& fn) {
        gpu_.RunUploadCommandsChunked(itemCount, itemsPerBatch, std::forward<Fn>(fn));
    }

    // ヘッドレス検証(--scene-instance-smoke)専用: 次の Render() 呼び出しが描いた
    // バックバッファをRGBA8で読み戻す。Render() 内でRENDER_TARGET状態のまま
    // READBACKヒープへコピーし、Present後にブロッキング待機してCPU側へ取り込む
    // (GUI目視の代わりに描画結果を自動検証するための経路。通常フレームでは
    // 一切コストをかけない)。
    // 性能調査用(FrameProfiler): 直近のEndFrame()でのPresent所要時間/GPU待ち
    // フェンス待機時間(ms)。vsync予算を超えたフレームの切り分け用。
    double LastPresentMs() const { return gpu_.LastPresentMs(); }
    double LastFrameWaitMs() const { return gpu_.LastFrameWaitMs(); }

    // パス別GPU実時間計測(性能調査用、GpuFrameTimer.h)。Init失敗時は常に0を返す。
    double LastGpuFrameMs() const { return gpu_.LastGpuMs(kGpuRegionFrame); }
    double LastGpuOpaqueMs() const { return gpu_.LastGpuMs(kGpuRegionOpaque); }
    double LastGpuTransparentMs() const { return gpu_.LastGpuMs(kGpuRegionTransparent); }
    double LastGpuCompositeMs() const { return gpu_.LastGpuMs(kGpuRegionComposite); }
    double LastGpuImguiMs() const { return gpu_.LastGpuMs(kGpuRegionImgui); }
    double LastGpuTonemapMs() const { return gpu_.LastGpuMs(kGpuRegionTonemap); }
    double LastGpuBloomMs() const { return gpu_.LastGpuMs(kGpuRegionBloom); }
    double LastGpuFxaaMs() const { return gpu_.LastGpuMs(kGpuRegionFxaa); }
    double LastGpuPrepassMs() const { return gpu_.LastGpuMs(kGpuRegionPrepass); }
    double LastGpuVelocityMs() const { return gpu_.LastGpuMs(kGpuRegionVelocity); }
    double LastGpuAoMs() const { return gpu_.LastGpuMs(kGpuRegionAo); }
    double LastGpuMotionBlurMs() const { return gpu_.LastGpuMs(kGpuRegionMotionBlur); }
    double LastGpuDepthOfFieldMs() const { return gpu_.LastGpuMs(kGpuRegionDepthOfField); }
    double LastGpuVolumetricFogMs() const { return gpu_.LastGpuMs(kGpuRegionVolumetricFog); }
    double LastGpuAtmosphereMs() const { return gpu_.LastGpuMs(kGpuRegionAtmosphere); }
    double LastGpuIblMs() const { return gpu_.LastGpuMs(kGpuRegionIbl); }
    double LastGpuShadowMs() const { return gpu_.LastGpuMs(kGpuRegionShadow); }

    // 直近の Render のCPU区間の内訳(App の分と FrameRenderer の分を合わせたもの)。
    const CpuRenderBreakdown& LastCpuRenderBreakdown() const { return cpuBreakdown_; }

    void RequestBackBufferCapture(BackBufferCaptureStage stage = BackBufferCaptureStage::AfterUi) {
        gpu_.RequestBackBufferCapture(stage);
    }
    bool TakeCapturedBackBuffer(std::vector<uint8_t>& outPixels, UINT& outWidth,
                                UINT& outHeight, UINT& outRowPitch) const {
        return gpu_.TakeCapturedBackBuffer(outPixels, outWidth, outHeight, outRowPitch);
    }

    // RmlUi統合(--rmlui-proto専用)。非nullの間、
    // Render()がFrameRenderer::RecordOverlay2Dの直後・ImGuiのRenderより前にRecordFrameを
    // 呼ぶ。所有権は持たない(呼び出し側=AppRuntimeが生存管理する)。
    void SetRmlUiIntegration(rs::app::rmlui::RmlUiIntegration* integration) { rmlUiIntegration_ = integration; }
    GpuContext& Gpu() { return gpu_; }

    // カメラ画面のファインダーの枠(バックバッファのピクセル座標)。値があるときは、3Dを描いたあとの
    // 最終の出力をこの枠へ縮めて書く(枠の外はUIの幕が覆う)。値なしなら画面いっぱい。
    void SetCameraFinder(std::optional<rs::render::OutputRect> finder) { cameraFinder_ = finder; }
    // フォトモードの被写界深度に使う撮影カメラの値。ファインダーが出ているときだけ値を渡し、出ていないときは nullopt
    // (被写界深度を掛けず、資源も作らない)。毎フレーム呼んでよい。掛ける・掛けないが切り替わったときだけ、
    // 次の Render の先頭で資源を作り直す。
    void SetDepthOfFieldCamera(const std::optional<rs::render::DepthOfFieldCamera>& camera);
    // フォトモードの露出。ファインダーが出ているときだけ、トーンマップの露出に掛ける倍率を渡す(出ていなければ nullopt)。
    // ブルームの重みにも同じ露出が使われる。毎フレーム呼んでよい。
    void SetPhotoExposureMultiplier(std::optional<float> multiplier) { photoExposureMultiplier_ = multiplier; }
    // ファインダー中の撮影カメラのシャッター速度[秒]。モーションブラーの長さを連動させる(FrameRenderer::SetPhotoShutterSeconds)。
    void SetPhotoShutterSeconds(std::optional<float> seconds) { renderer_.SetPhotoShutterSeconds(seconds); }
    // フォトモードの ISO オートの測光(撮影カメラの露出を掛ける前のシーンの明るさ)を走らせるか。毎フレーム呼んでよい。
    // 走る・走らないが切り替わったときだけ、次の Render の先頭で資源を作る。結果は毎フレーム Render のあとに Poll で取る
    // (数フレームに1回、対数輝度の格子が返る)。
    void SetLuminanceMeter(bool enabled);
    std::optional<rs::render::LuminanceMeterGrid> PollLuminanceMeter() { return renderer_.PollLuminanceMeter(); }
    // クリックでフォーカス用の奥行きの読み戻し(FrameRenderer::RequestDepthProbe)。毎フレーム Render のあとに Poll を呼ぶ。
    void RequestDepthProbe(double u, double v) { renderer_.RequestDepthProbe(u, v); }
    std::optional<double> PollDepthProbe() { return renderer_.PollDepthProbe(); }

private:
    struct FramePassContext;
    // dirty フラグに溜まった作り直しを、GPU の完了を1回待ってまとめて行う(RenderRebuild.h の表の順)。
    void FlushRebuilds();
    // FramePassTable.h の表の1項目を記録する。
    void RecordFramePass(FramePassId id, FramePassContext& ctx);

    GpuContext gpu_;
    int fpsLimit_ = 0; // SetFpsLimit参照
    RenderDirty dirty_;
    rs::render::ShadowQuality pendingShadowQuality_ = rs::render::kDefaultShadowQuality; // ShadowMap の作り直し先
    UINT pendingWidth_ = 0;
    UINT pendingHeight_ = 0; // Resize の作り直し先
    // Render の本体。区間の所要時間を breakdown へ書く。
    void RenderTimed(const CameraView& cam, bool skipMeshPass, CpuRenderBreakdown& breakdown);

    CpuRenderBreakdown cpuBreakdown_;
    FrameRenderer renderer_;
    // シーンの描画先(HDR)とトーンマップ。バックバッファへ書くのはトーンマップ以降だけ。
    rs::render::SceneHdrTarget hdrTarget_;
    // 再作成に失敗して未準備のとき、Renderが何フレームおきに作り直しを試すか数えるカウンタ。
    std::uint32_t hdrRetryFrames_ = 0;
    bool viewerBackground_ = false;       // Init でビューアーの背景色にしたか
    bool viewerBackgroundLightOn_ = true; // いまの背景色がライトON(昼)の色か
    rs::render::TonemapPass tonemap_;
    rs::render::PostProcessChain postProcess_;
    rs::core::DebugDrawList debugDrawList_;
    rs::app::rmlui::RmlUiIntegration* rmlUiIntegration_ = nullptr;
    std::optional<rs::render::OutputRect> cameraFinder_;
    std::optional<float> photoExposureMultiplier_; // ファインダー中の撮影カメラの露出倍率(SetPhotoExposureMultiplier 参照)
};

} // namespace rs::app
