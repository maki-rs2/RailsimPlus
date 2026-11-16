//
// AppFramePasses.cpp - App の1フレームの記録(FramePassTable.h の表の順に回し、各項目を記録する)
//
#include "App.h"

#include <chrono>

#include <spdlog/spdlog.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"

#include "../../render/postprocess/TonemapMath.h"
#include "../../render/resource/RenderTuning.h"
#include "../rmlui/RmlUiIntegration.h"

namespace rs::app {

namespace {

// HDRターゲットが未準備のとき、作り直しを試す間隔(フレーム数)。約1秒おき。
constexpr std::uint32_t kHdrRetryIntervalFrames = 60;

// ポストプロセスの段ごとのGPU計測区間を、GpuFrameTimer の区間へ対応させる。
class GpuStageTimer final : public rs::render::PostProcessTimer {
public:
    GpuStageTimer(GpuFrameTimer* timer, ID3D12GraphicsCommandList* cmdList) : timer_(timer), cmdList_(cmdList) {}
    void Begin(rs::render::PostProcessStage stage) override {
        if (timer_) timer_->Begin(cmdList_, RegionOf(stage));
    }
    void End(rs::render::PostProcessStage stage) override {
        if (timer_) timer_->End(cmdList_, RegionOf(stage));
    }

private:
    static size_t RegionOf(rs::render::PostProcessStage stage) {
        switch (stage) {
        case rs::render::PostProcessStage::Bloom: return kGpuRegionBloom;
        case rs::render::PostProcessStage::Fxaa: return kGpuRegionFxaa;
        case rs::render::PostProcessStage::Tonemap: break;
        }
        return kGpuRegionTonemap;
    }
    GpuFrameTimer* timer_;
    ID3D12GraphicsCommandList* cmdList_;
};

} // namespace

// 1フレームの記録の間、表の各項目が共有する入力と途中の状態。
struct App::FramePassContext {
    const CameraView& cam;
    bool skipMeshPass;
    GpuContext::FrameTargets ft;
    rs::rhi::CommandList commandList;
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv;
    CpuRenderBreakdown& breakdown;
    FramePassFlags flags;
    // 大気LUTの更新が何か記録したか。
    bool environmentRecorded = false;
    // 以降の段(レンズフレア・ポストプロセス)が読むシーン色。TAA・モーションブラーが差し替える。
    rs::render::SceneHdrTarget* sceneTarget = nullptr;
};

void App::RenderTimed(const CameraView& cam, bool skipMeshPass, CpuRenderBreakdown& breakdown) {
    // 設定変更や画面の大きさの変更で溜まった資源の作り直しを、フレームの先頭でまとめて済ませる。
    FlushRebuilds();
    // HDRターゲットの再作成に失敗して未準備のままだと、シーンが描かれない。未準備の間は資源が
    // 存在せずGPUも使っていないので、一定フレームおきに作り直しを試す(ログが毎フレーム出ないように間引く)。
    if (!hdrTarget_.Ready()) {
        if (hdrRetryFrames_ == 0) hdrTarget_.Resize(gpu_.Width(), gpu_.Height());
        hdrRetryFrames_ = (hdrRetryFrames_ + 1) % kHdrRetryIntervalFrames;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = renderer_.DepthDsv();
    const std::chrono::steady_clock::time_point beginStart = std::chrono::steady_clock::now();
    GpuContext::FrameTargets ft = gpu_.BeginFrame(depthDsv);
    breakdown.beginFrameMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - beginStart).count();

    FramePassContext ctx{cam, skipMeshPass, ft, rs::rhi::CommandList(ft.cmdList), depthDsv, breakdown, {}};
    ctx.sceneTarget = &hdrTarget_;
    // 表の条件のうち、描画側(FrameRenderer::PrepareFrame)が決めるもの以外をここで立てる。
    ctx.flags.Set(FramePassFlag::HdrReady, hdrTarget_.Ready());
    ctx.flags.Set(FramePassFlag::EnvironmentUpdate, renderer_.ShouldUpdateEnvironment(skipMeshPass));
    ctx.flags.Set(FramePassFlag::RmlUi, rmlUiIntegration_ != nullptr);

    breakdown.overlayPolylines = static_cast<double>(debugDrawList_.Polylines().size());
    for (const rs::core::DebugPolyline& line : debugDrawList_.Polylines())
        breakdown.overlayPolylinePoints += static_cast<double>(line.points.size());
    breakdown.overlayCircles = static_cast<double>(debugDrawList_.Circles().size());
    breakdown.overlayRects = static_cast<double>(debugDrawList_.Rects().size());
    breakdown.overlayTexts = static_cast<double>(debugDrawList_.Texts().size());

    // 記録の順と有効条件は FramePassTable.h の表が決める。
    for (const FramePassDesc& pass : kFramePasses) {
        if (PassEnabled(pass, ctx.flags)) RecordFramePass(pass.id, ctx);
    }

    {
        const ScopedAccumulate endScope(breakdown.endFrameMs);
        gpu_.EndFrame();
        if (rmlUiIntegration_ != nullptr) rmlUiIntegration_->SubmitFrame(gpu_.FrameIndex());
    }
    debugDrawList_.Clear();
}

void App::RecordFramePass(FramePassId id, FramePassContext& ctx) {
    const rs::rhi::CommandList& commandList = ctx.commandList;
    ID3D12GraphicsCommandList* cmdList = ctx.ft.cmdList;
    GpuFrameTimer* timer = ctx.ft.timer;
    const UINT width = gpu_.Width();
    const UINT height = gpu_.Height();
    switch (id) {
    // 大気LUTとIBLの更新(空を描くかに関わらず、読込ジョブ進行中以外は常に。IBLは空を描かないモードでも
    // 環境光・反射に要る)。大気LUTの生成が済んでからIBLを作る(環境ソースはSky-View LUTを引く)。
    // どれもRTV・ビューポート・シザーを書き換えるので、シーンのBeginSceneより前に済ませる。
    case FramePassId::AtmosphereLut: {
        const ScopedAccumulate environmentScope(ctx.breakdown.environmentUpdateMs);
        if (timer) timer->Begin(cmdList, kGpuRegionAtmosphere); // 大気遠近まで含めて1区間
        ctx.environmentRecorded |= renderer_.UpdateAtmosphere(commandList, ctx.cam);
        break;
    }
    case FramePassId::AerialPerspective: {
        const ScopedAccumulate environmentScope(ctx.breakdown.environmentUpdateMs);
        ctx.environmentRecorded |= renderer_.UpdateAerialPerspective(commandList, ctx.cam, width, height);
        if (timer) timer->End(cmdList, kGpuRegionAtmosphere);
        break;
    }
    case FramePassId::Ibl: {
        const ScopedAccumulate environmentScope(ctx.breakdown.environmentUpdateMs);
        if (timer) timer->Begin(cmdList, kGpuRegionIbl);
        ctx.environmentRecorded |= renderer_.UpdateIbl(commandList);
        if (timer) timer->End(cmdList, kGpuRegionIbl);
        ctx.flags.Set(FramePassFlag::EnvironmentRecorded, ctx.environmentRecorded);
        break;
    }
    case FramePassId::EnvironmentViewportReset: {
        // ビューポートとシザーはバックバッファの大きさへ張り直す(RTVはBeginSceneが張り直す)。
        const D3D12_VIEWPORT viewport = {0.0f, 0.0f, float(width), float(height), 0.0f, 1.0f};
        const D3D12_RECT scissor = {0, 0, LONG(width), LONG(height)};
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);
        break;
    }
    // シーンはHDRターゲットへ描く(BeginFrameがバインドしたバックバッファからここで切り替える)。
    // バックバッファへ書くのは、直後のトーンマップとその後のUIだけ。
    case FramePassId::SceneBegin:
        hdrTarget_.BeginScene(commandList, rs::rhi::CpuDescriptor{ctx.depthDsv.ptr});
        break;
    case FramePassId::FramePrepare:
        renderer_.PrepareFrame(ctx.cam, hdrTarget_.Rtv(), width, height,
                               ctx.skipMeshPass, ctx.flags);
        break;
    case FramePassId::Sky:
        renderer_.RecordSky(commandList);
        break;
    case FramePassId::Prepass:
        renderer_.RecordPrepass(commandList, timer);
        break;
    case FramePassId::Velocity:
        renderer_.RecordVelocity(commandList, timer);
        break;
    case FramePassId::AmbientOcclusion:
        renderer_.RecordAmbientOcclusion(commandList, timer);
        break;
    case FramePassId::MainTargetReset:
        renderer_.ResetMainTargets(commandList);
        break;
    case FramePassId::Shadow:
        renderer_.RecordShadow(commandList, timer);
        break;
    case FramePassId::VolumetricFog:
        renderer_.RecordVolumetricFog(commandList, timer);
        break;
    case FramePassId::ComputeSelfTest:
        renderer_.RecordComputeSelfTest(commandList);
        break;
    case FramePassId::MeshScene:
        renderer_.RecordMesh(commandList, timer);
        break;
    // TAA: ジッタ付きで描いたシーンを履歴と重ねる。以降(レンズフレア・ポストプロセス)は解決後の HDR を使う。
    case FramePassId::TemporalAa:
        ctx.sceneTarget = renderer_.ResolveTaa(commandList, hdrTarget_);
        break;
    // 被写界深度: 解決後(または描いたまま)の HDR を、撮影カメラの錯乱円に合わせてぼかす。結果が以降の入力になり、
    // TAA の履歴には入らない。
    case FramePassId::DepthOfField:
        ctx.sceneTarget = renderer_.ResolveDepthOfField(commandList, *ctx.sceneTarget, timer);
        break;
    // モーションブラー: 解決後(または描いたまま)の HDR を速度の向きにぼかす。ぼかしの結果が以降の入力になり、
    // TAA の履歴には入らない。
    case FramePassId::MotionBlur:
        ctx.sceneTarget = renderer_.ResolveMotionBlur(commandList, *ctx.sceneTarget, timer);
        break;
    case FramePassId::Particle:
        renderer_.RecordParticles(commandList, ctx.sceneTarget->Rtv(), ctx.ft.frameIndex);
        break;
    // レンズフレア: リニアHDRのシーンターゲットへ足す(トーンマップの前)。F12・PNGのキャプチャにも写る。
    // 読込ジョブ進行中(半端な状態のシーンを描かない間)は出さない。太陽のフレアと白飛びは、
    // 空を描かない間(ビューアー等)は足さない。灯具のフレアは空に関係なく出す。
    case FramePassId::LensFlare:
        renderer_.RecordLensFlare(commandList, ctx.sceneTarget->Rtv(), ctx.cam, width, height, ctx.ft.frameIndex,
                                  renderer_.ShouldDrawSky(ctx.skipMeshPass));
        break;
    // ISO オートの測光: 撮影カメラの露出を掛ける前のシーン(ここまでの結果)を測る。結果は数フレーム後に読み戻す。
    case FramePassId::LuminanceMeter:
        renderer_.RecordLuminanceMeter(commandList, *ctx.sceneTarget);
        break;
    // プラグインの空が有効で、空パスが実際に描かれるときはトーンマップを通さない(パススルー)。
    // それ以外は設定どおり(ACES、--setting tonemap=off ならパススルー)。
    // tonemap_ の設定は書き換えない(サムネイルが共用するため)。
    // ブルームの調整値と露出は、読み直した調整値に追従できるよう毎フレーム取り直す(露出はこのコピーだけ上書きする)。
    case FramePassId::PostProcess: {
        const rs::render::RenderTuning& frameTuning = rs::render::RenderTuning::Embedded();
        postProcess_.SetTuning({frameTuning.bloomStrength, frameTuning.bloomThreshold, frameTuning.bloomScatter});
        rs::render::TonemapParams tonemapParams = tonemap_.Params();
        tonemapParams.exposure = rs::render::TonemapExposureFromTuning(frameTuning);
        // 撮影カメラの露出。ブルームの重みはこの露出を読むので、ここで掛ければ両方に反映される。
        if (photoExposureMultiplier_) tonemapParams.exposure *= *photoExposureMultiplier_;
        if (renderer_.PluginSkyActive() && renderer_.ShouldDrawSky(ctx.skipMeshPass))
            tonemapParams.mode = rs::render::TonemapMode::Off;
        GpuStageTimer stageTimer(timer, cmdList);
        postProcess_.Record(commandList, *ctx.sceneTarget, tonemap_, tonemapParams,
                            rs::rhi::CpuDescriptor{ctx.ft.sceneRtv.ptr}, &stageTimer,
                            cameraFinder_.has_value() ? &*cameraFinder_ : nullptr);
        break;
    }
    // F12の3Dだけスクリーンショット: トーンマップ後・UI描画前のバックバッファ(RENDER_TARGET状態)を読み戻す。
    case FramePassId::SceneCapture:
        gpu_.RecordSceneOnlyCaptureIfRequested(commandList);
        break;
    case FramePassId::Overlay2D: {
        cmdList->OMSetRenderTargets(1, &ctx.ft.uiRtv, FALSE, nullptr);
        const ScopedAccumulate overlayScope(ctx.breakdown.overlay2dMs);
        if (timer) timer->Begin(cmdList, kGpuRegionOverlay2D);
        // UIを隠している間は、世界に重ねる2D注記も描かない。
        if (rmlUiIntegration_ == nullptr || !rmlUiIntegration_->UiHidden()) {
            renderer_.RecordOverlay2D(cmdList, ctx.ft.uiRtv, debugDrawList_, ctx.cam, width, height, ctx.ft.frameIndex);
        }
        if (timer) timer->End(cmdList, kGpuRegionOverlay2D);
        break;
    }
    // RmlUi統合(--rmlui-proto専用)。Overlay2Dの直後・ImGuiのRenderより前に記録する。RmlUiは描画のたびに
    // 自前のSRVヒープへSetDescriptorHeapsするため、直後に共有ヒープへ戻す(ImGui_ImplDX12_RenderDrawDataは
    // SetDescriptorHeapsを自分で呼ばないため)。
    case FramePassId::RmlUi: {
        const ScopedAccumulate rmlUiScope(ctx.breakdown.rmlUiRecordMs);
        rmlUiIntegration_->RecordFrame(ctx.ft.frameIndex, ctx.ft.uiRtv);
        ID3D12DescriptorHeap* heaps[] = {gpu_.SrvHeap()};
        cmdList->SetDescriptorHeaps(1, heaps);
        break;
    }
    // ImGui(Present前のバリア/読み戻しを行うEndFrame()の前に描く)。
    case FramePassId::Imgui: {
        const ScopedAccumulate imguiScope(ctx.breakdown.imguiRecordMs);
        if (timer) timer->Begin(cmdList, kGpuRegionImgui);
        ImGui::Render();
        if (rmlUiIntegration_ == nullptr || !rmlUiIntegration_->UiHidden()) {
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
        }
        if (timer) timer->End(cmdList, kGpuRegionImgui);
        break;
    }
    case FramePassId::Count:
        break;
    }
}

} // namespace rs::app
