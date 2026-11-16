#include "FrameRenderer.h"

#include <spdlog/spdlog.h>

#include "../../platform/EnvironmentVariable.h"
#include "../../render/sky/EnvironmentState.h"
#include "../../render/resource/RenderTuning.h"
#include "App.h"
#include "CameraMath.h"
#include "GpuContext.h"
#include "SceneSlotCulling.h"

// FrameRenderer の1フレームの準備(CPU)と、空・メッシュなど表の項目の記録。
// 準備は FramePassTable.h の FramePrepare、記録は App がその表の順に呼ぶ。

namespace rs::app {

void FrameRenderer::PrepareFrame(const CameraView& cam, rs::rhi::CpuDescriptor rtv, UINT width, UINT height,
                                 bool skipMeshPass, FramePassFlags& flags) {
    cpuBreakdown_ = CpuRenderBreakdown{};
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    frame_ = FrameState{};
    frame_.cam = cam;
    frame_.rtv = rtv;
    frame_.width = width;
    frame_.height = height;
    frame_.frameIndex = static_cast<UINT>(frameCounter_);
    frame_.counter = frameCounter_;
    frame_.viewProj = BuildViewProj(cam, width, height);
    UpdateMotionBlurClock();

    // 環境(IBL・太陽色・プラグインの空・写真の空の層)。空パス・メッシュ・トーンマップが同じ結果を見るよう、
    // 毎フレームここ1か所で組み立てる(UpdateIbl も同じ関数で組み立てる)。
    BuildEnvironment();

    // 空は、メッシュの有無に関係なく毎フレーム描く。skyEnabled_=false(プラグインビューワーモード)、
    // またはskipMeshPass=true(読込ジョブ進行中、半端な読込状態のままUIだけ描く画面)の間は描かない。
    flags.Set(FramePassFlag::DrawSky, ShouldDrawSky(skipMeshPass));
    flags.Set(FramePassFlag::LensFlare, ShouldDrawLensFlare(skipMeshPass));
    flags.Set(FramePassFlag::Particle, particlesEnabled_ && !particleBatch_.Instances().empty());
    // ISO オートの測光は、HDR のシーンだけを読む。空だけのシーンも測るので、メッシュの有無には依らない。
    // 読込ジョブ進行中(半端な状態のシーン)は測らない。
    flags.Set(FramePassFlag::LuminanceMeter, luminanceMeterEnabled_ && !skipMeshPass && luminanceMeter_.Ready());

    bool anySceneMesh = false;
    for (const auto& sp : scenePipelines_) {
        if (sp.HasMesh()) {
            anySceneMesh = true;
            break;
        }
    }
    frame_.anyInstanceBatch = instanceBatches_.BatchCount() > 0;
    frame_.anyTrainInstanceBatch = trainInstanceBatches_.BatchCount() > 0;

    // メッシュ描画（トラック側 + シーン常駐側 + インスタンスバッチ側。同一 view/light/depth を使う）。
    const bool meshReady = !skipMeshPass && (meshPipeline_.HasMesh() || anySceneMesh || frame_.anyInstanceBatch ||
                                             frame_.anyTrainInstanceBatch);
    flags.Set(FramePassFlag::MeshReady, meshReady);
    if (meshReady) PrepareMeshFrame(flags);
    // 今フレームに走らなかったときは、履歴を引き継がない。
    if (temporalAa_ && !flags.Test(FramePassFlag::TemporalAa)) taaHistoryValid_ = false;
    ++frameCounter_;
}

void FrameRenderer::PrepareMeshFrame(FramePassFlags& flags) {
    const CameraView& cam = frame_.cam;
    const ViewProjResult& viewProj = frame_.viewProj;
    const UINT width = frame_.width;
    const UINT height = frame_.height;
    const UINT fc = frame_.frameIndex;
    {
        const ScopedAccumulate cullScope(cpuBreakdown_.cullMs);
        sceneSlotState_.LastCullStats() = UpdateSceneSlotCullFlags(
            rs::core::ExtractFrustum(viewProj.viewProj), sceneMeshBounds_, scenePartWorlds_,
            sceneSlotState_.Bounds(), sceneSlotState_.Culled());
    }
    BuildDrawEntries();

    // 環境光(IBL)・太陽色。組み立て済みの環境を、全パイプライン・全バッチへbase定数として配る。
    const float lightDir[3] = {sunDirection_.x, sunDirection_.y, sunDirection_.z};
    frame_.constants = rs::render::MakeBaseMeshConstants(lightDir, dayAlpha_, environmentState_);
    frame_.mainView = rs::render::MakeViewConstants(viewProj.viewProj.m, cam.eye);
    PrepareTaaView(cam, viewProj, width, height, frame_.mainView);
    ApplyAerialPerspectiveConstants(frame_.constants, frame_.mainView);
    ApplyHeightFogConstants(frame_.constants);
    rs::render::ApplyViewToMeshConstants(frame_.constants, viewConstants_.Write(fc, kViewSlotMain, frame_.mainView), cam.eye);
    {
        const ScopedAccumulate syncScope(cpuBreakdown_.batchSyncMs);
        FlushScenePartWorlds(fc);
        if (frame_.anyInstanceBatch) instanceBatches_.Sync(fc);
        if (frame_.anyTrainInstanceBatch) trainInstanceBatches_.Sync(fc);
    }

    // 深度プリパス: AO を掛ける、または速度を使うときに描く。速度パスはプリパスが走るときだけ走る
    // (速度は、プリパスの深度に沿って書くため)。
    const bool depthReady =
        prepassDepth_.Ready() && prepassDepth_.Width() == width && prepassDepth_.Height() == height;
    const bool aoOn = aoQuality_ != rs::render::AoQuality::None && ambientOcclusion_.Ready(width, height) && depthReady;
    const bool prepass = depthReady && (aoOn || VelocityWanted());
    const bool velocity = prepass && VelocityWanted() && velocity_.Ready() && velocity_.Width() == width &&
                          velocity_.Height() == height;
    flags.Set(FramePassFlag::Prepass, prepass);
    flags.Set(FramePassFlag::Velocity, velocity);
    flags.Set(FramePassFlag::AmbientOcclusion, prepass && aoOn);

    // 太陽光の影。影を落とすものの一覧とカスケードの行列を用意する。
    bool shadow = false;
    {
        const ScopedAccumulate shadowScope(cpuBreakdown_.shadowCpuMs);
        shadow = PrepareShadow(cam, viewProj, width, height, frame_.constants, fc);
    }
    flags.Set(FramePassFlag::Shadow, shadow);
    // フォグの格子は、影のマップを引いて作る。
    flags.Set(FramePassFlag::VolumetricFog, shadow && VolumetricFogReady(width, height));

    // TAA・モーションブラーは、速度とプリパスの深度が今フレームに書かれたときだけ走る。
    flags.Set(FramePassFlag::TemporalAa, prepass && velocity && TemporalAaReady());
    flags.Set(FramePassFlag::MotionBlur, prepass && velocity && MotionBlurReady());
    // 被写界深度は本編の深度だけを読むので、プリパス・速度には依存しない(メッシュを描いたフレームで走る)。
    flags.Set(FramePassFlag::DepthOfField, DepthOfFieldReady());

    static const bool kProfileFrame = rs::platform::ReadEnvironmentVariable("RS_PROFILE_FRAME") != nullptr;
    if (kProfileFrame && frameCounter_ % 60 == 0)
        spdlog::info("[cull] slots={}/{}", sceneSlotState_.LastCullStats().drawn,
                     sceneSlotState_.LastCullStats().total);
}

void FrameRenderer::RecordSky(const rs::rhi::CommandList& commandList) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    const CameraView& cam = frame_.cam;
    const ViewProjResult& viewProj = frame_.viewProj;
    // sunDirection_は光が進む向きなので、太陽そのものの方向は符号反転。
    const float aspectRatio = EffectiveAspect(cam, frame_.width, frame_.height);
    rs::render::SkyPassParams skyParams = rs::render::MakeSkyPassParams(
        viewProj.view, cam.fovYRadians, aspectRatio, SunDirectionToSun(), dayAlpha_,
        rs::render::RenderTuning::Embedded());
    for (int i = 0; i < 3; ++i) skyParams.moonToMoon[i] = environmentState_.moonDirection[i];
    skyParams.moonRatio = environmentState_.moonRatio;
    skyParams.skyRadianceScale = environmentState_.skyRadianceScale;
    for (int i = 0; i < 3; ++i) skyParams.moonDiskToMoon[i] = environmentState_.moonDiskDirection[i];
    skyParams.moonDiskEnable = environmentState_.moonDiskVisible ? 1.0f : 0.0f;
    if (environmentState_.adaptation) {
        skyParams.nightBlue = 0.0f;  // 標準の空の夜は星明かりと月明かりが受け持つ
        skyParams.useAdaptation = 1.0f;  // 空の出力へ暗順応の倍率(プリ露出)を掛ける
    }
    if (environmentState_.pluginSky) {
        // プラグインの空も標準の空と同じ大気で描く。太陽と月の円盤は SkyBodyPass が Sun.x・Moon.x で足すので描かず、
        // 夜の青も足さない(月・暗順応は環境の組み立てで既に無効)。
        skyParams.sunDiskIntensity = 0.0f;
        skyParams.nightBlue = 0.0f;
    }
    skyPass_.Record(commandList, atmosphere_, skyParams,
                    environmentState_.adaptation ? ibl_.ExposureSrvGpuHandle() : 0);
    // プラグインの空: 大気の背景の上へ Sun.x・Moon.x を時刻の位置に足す(RailSim II と同じ。月は標準の空の円盤と違い
    // 満ち欠けのテクスチャを持つので、月の方向は明るさに関係なく常に渡す)。
    if (environmentState_.pluginSky && rs::render::RenderTuning::Embedded().pluginSkyBodies) {
        rs::render::SkyBodyFrame bodyFrame;
        bodyFrame.view = viewProj.view;
        bodyFrame.proj = viewProj.proj;
        bodyFrame.directionToSun = SunDirectionToSun();
        bodyFrame.directionToMoon = moonDirection_;
        for (int i = 0; i < 3; ++i) bodyFrame.landscapeLight[i] = environmentState_.pluginLandscapeLight[i];
        skyBodies_.Record(commandList, bodyFrame);
    }
}

void FrameRenderer::RecordComputeSelfTest(const rs::rhi::CommandList& commandList) {
    computeSelfTest_.Step(commandList);
}

void FrameRenderer::RecordMesh(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    const ScopedAccumulate meshScope(cpuBreakdown_.meshRecordMs);
    RecordMeshPasses(commandList, timer);
}

} // namespace rs::app
