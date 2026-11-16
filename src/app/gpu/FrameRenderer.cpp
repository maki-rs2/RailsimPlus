#include "../../render/RenderPrecision.h"
#include "FrameRenderer.h"

#include "../../platform/EnvironmentVariable.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

#include "imgui.h"

#include "../../platform/font/UiFont.h"
#include "../../render/postprocess/PostProcessFormats.h"
#include "../../render/sky/EnvironmentState.h"
#include "../../render/resource/RenderTuning.h"
#include "../../core/Math.h"
#include "App.h"
#include "CameraMath.h"
#include "GpuContext.h"
#include "InitSteps.h"
#include "SrvLayout.h"

namespace rs::app {

using rs::core::Normalize3;

namespace {

// meshPipeline_(主メッシュ=敷設エディタのレール・架線の押し出しメッシュ)の
// 頂点・インデックスをDEFAULTヒープへ載せるためのGeometryManagerキー。
//
// 空キーだとUPLOADヒープ(CPU可視のシステムメモリ)へ直接確保され、頂点・インデックスが
// 毎フレームPCIe越しに読まれる。大規模レイアウトの統合メッシュでは1フレームあたり
// 数百MBの転送になり、不透明パスのほぼ全時間をこれが占める。
//
// 主メッシュは同時に1枚しか存在せず共有の余地が無いため、キーは固定でよい。
// MeshPipeline::UploadMesh は先頭のResetGeometry()とUploadGeometryBuffers()の
// sharedGeometry_.reset()で旧リソースへの参照を必ず手放してからAcquireするため、
// GeometryManagerのweak_ptrキャッシュ側は期限切れとして再生成する(=固定キーでも
// 古い実体が返ることはない)。パス形式のキー(シーン常駐スロット側)と衝突しない
// 名前にしてある。
constexpr const char* kMainMeshGeometryKey = "__frame_renderer_main_mesh__";

} // namespace

bool FrameRenderer::Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvStride,
                         UINT width, UINT height, rs::rhi::RetirementQueue& retirements) {
    meshPipelineShared_.SetRetirementQueue(retirements);
    const rs::rhi::Device rhiDevice(device);
    const rs::rhi::SrvHeap rhiSrvHeap(srvHeap, srvStride);
    // 深度の SRV を書き直すヒープ(TAA・モーションブラーが、パスの初期化の成否に関係なく使う)。
    mainSrvHeap_ = rhiSrvHeap;
    targetWidth_ = width;
    targetHeight_ = height;
    // 第2描画パス（シーン常駐エンティティ）。深度は meshPipeline_ のものを共有するため CreateDepthBuffer は
    // 呼ばない。スロット自体はEnsureSceneSlotCapacity()がUploadSceneMeshToGpu()時に遅延確保する。
    warnedScenePoolExhaustion_ = false;
    scenePipelines_.clear();
    sceneMeshBounds_.clear();
    scenePartWorlds_.clear();
    sceneDynamicVertices_.clear();
    sceneSlotState_.ClearPartWorldDirty();
    sceneSlotState_.ClearPartWorldKeys();
    sceneSlotState_.ClearBounds();
    sceneSlotState_.ClearCulled();
    sceneSlotState_.ResetLastCullStats();
    sceneAllocator_.Init(kSrvLayout.scenePool, kSceneSrvPoolCapacity);

    rs::render::SrvRangeAllocator::Range previewRange;
    rs::render::SrvRangeAllocator::Range overlayRange;
    std::array<FrameHighlightState::PipelineInit, kHighlightSlots> highlightPipelineInits;

    using Importance = InitImportance;
    // 必須: 本編の描画と空・環境光・UI。失敗すると描画できない。
    // 任意: アンビエントオクルージョン・深度プリパス・compute の確認・TAA・モーションブラー・ボリュメトリックフォグ・影。
    // 失敗しても本編は描けるので、警告を出してそのパスだけを使えなくする。
    const InitStep steps[] = {
        // root signature/PSO一式はdevice+rtvFormat(常にkSceneHdrFormat)のみに依存しper-instance状態を持たないため、
        // ここで1回だけ生成して全MeshPipeline/InstanceBatchへ配る(per-slot/per-batchの重複生成を避ける)。
        {"meshPipelineShared", Importance::Required,
         [&] { return meshPipelineShared_.Init(device, rs::render::kSceneHdrFormat); }},
        {"meshPipeline", Importance::Required,
         [&] {
             return meshPipeline_.Init(device, meshPipelineShared_, srvHeap, srvStride, kSrvLayout.mainTextures,
                                       kMaxTextures, kSrvLayout.mainTransBuf, kSrvLayout.mainPartWorld,
                                       kSrvLayout.mainMatOverride, kMaxParts, rs::render::GetTextureManager()) &&
                    meshPipeline_.CreateDepthBuffer(device, width, height);
         }},
        {"placementPreview", Importance::Required,
         [&] {
             previewRange = sceneAllocator_.Allocate(kSceneEntitySrvStride);
             if (!previewRange.ok) return false;
             const UINT firstTex = previewRange.baseIndex;
             const UINT firstTransBuf = firstTex + kSceneEntityMaxTextures;
             const UINT firstPartWorld = firstTransBuf + kTransBufSrvCount;
             if (placementPreviewState_.Init(device, meshPipelineShared_, srvHeap, srvStride, firstTex,
                                             kSceneEntityMaxTextures, firstTransBuf, firstPartWorld,
                                             firstPartWorld + kPartWorldSrvCount, kSceneEntityMaxParts))
                 return true;
             sceneAllocator_.Free(previewRange);
             return false;
         }},
        // GPUインスタンス描画バッチ。深度は meshPipeline_ のものを共有する(scenePipelines_と同じ扱い)。
        {"instanceBatches", Importance::Required,
         [&] {
             return instanceBatches_.Init(device, meshPipelineShared_, srvHeap, srvStride,
                                          kSrvLayout.instanceBatchPool, kInstanceBatchSrvPoolCapacity,
                                          kInstanceBatchMaxTextures, kInstanceBatchInitialInstances);
         }},
        {"trainInstanceBatches", Importance::Required,
         [&] {
             return trainInstanceBatches_.Init(device, meshPipelineShared_, srvHeap, srvStride,
                                               kSrvLayout.trainInstanceBatchPool, kTrainInstanceBatchSrvPoolCapacity,
                                               kInstanceBatchMaxTextures, kInstanceBatchInitialInstances);
         }},
        // WBOIT 合成パス（accum/reveal オフスクリーンRT + 全画面合成）。
        {"wboit", Importance::Required,
         [&] {
             return wboit_.Init(rhiDevice, rhiSrvHeap, kSrvLayout.wboit) &&
                    wboit_.CreateTargets(rhiDevice, width, height);
         }},
        // アンビエントオクルージョン。資源は品質が決まってから(SetAmbientOcclusion)作る。
        {"ambientOcclusion", Importance::Optional,
         [&] { return ambientOcclusion_.Init(rhiDevice, rhiSrvHeap, kSrvLayout.ambientOcclusion); }},
        {"prepassTargets", Importance::Optional, [&] { return InitPrepassTargets(rhiDevice, rhiSrvHeap); }},
        {"computeSelfTest", Importance::Optional,
         [&] {
             computeSelfTest_.Init(rhiDevice, rhiSrvHeap, kSrvLayout.computeSelfTest);
             return true;
         }},
        {"taa", Importance::Optional, [&] { return InitTaa(rhiDevice, rhiSrvHeap); }},
        {"motionBlur", Importance::Optional, [&] { return InitMotionBlur(rhiDevice, rhiSrvHeap); }},
        {"depthOfField", Importance::Optional, [&] { return InitDepthOfField(rhiDevice, rhiSrvHeap); }},
        {"luminanceMeter", Importance::Optional, [&] { return InitLuminanceMeter(rhiDevice, rhiSrvHeap); }},
        {"volumetricFog", Importance::Optional, [&] { return InitVolumetricFog(rhiDevice, rhiSrvHeap); }},
        // 空: 大気LUTと、それを視線方向で引く空パス。
        {"atmosphere", Importance::Required,
         [&] { return atmosphere_.Init(rhiDevice, rhiSrvHeap, kSrvLayout.atmosphere, kSrvLayout.moonSkyView); }},
        {"aerialPerspective", Importance::Required,
         [&] { return aerialPerspective_.Init(rhiDevice, rhiSrvHeap, kSrvLayout.aerialPerspective); }},
        {"skyPass", Importance::Required, [&] { return skyPass_.Init(rhiDevice); }},
        {"ibl", Importance::Required,
         [&] { return ibl_.Init(rhiDevice, rhiSrvHeap, kSrvLayout.ibl, kSrvLayout.exposure); }},
        {"shadow", Importance::Optional, [&] { return InitShadow(rhiDevice, rhiSrvHeap); }},
        {"viewConstants", Importance::Required, [&] { return viewConstants_.Init(rhiDevice, kViewSlotCount); }},
        {"overlay2D", Importance::Required,
         [&] {
             overlayRange = sceneAllocator_.Allocate(1);
             if (!overlayRange.ok) return false;
             const std::string fontPath = rs::platform_font::ResolveUiFontPath();
             const long faceIndex =
                 fontPath.empty() ? 0
                                  : rs::platform_font::FindNamedInstanceFaceIndex(fontPath, rs::platform_font::kUiFontWeight);
             if (overlay2DPass_.Init(device, srvHeap, srvStride, overlayRange.baseIndex, kBackBufferFormat,
                                     fontPath.c_str(), faceIndex))
                 return true;
             sceneAllocator_.Free(overlayRange);
             return false;
         }},
        // Scenery選択ハイライト用(固定2スロット、深度は meshPipeline_ と共有するため CreateDepthBuffer は呼ばない)。
        {"highlight", Importance::Required,
         [&] {
             for (UINT i = 0; i < kHighlightSlots; ++i) {
                 const UINT firstTex = kSrvLayout.highlight + i * kHighlightStride;
                 const UINT firstTransBuf = firstTex + kHighlightMaxTextures;
                 const UINT firstPartWorld = firstTransBuf + kTransBufSrvCount;
                 highlightPipelineInits[i] = {firstTex, firstTransBuf, firstPartWorld, firstPartWorld + kPartWorldSrvCount};
             }
             return highlightState_.Init(device, meshPipelineShared_, srvHeap, srvStride, highlightPipelineInits,
                                         kHighlightMaxTextures, kHighlightMaxParts);
         }},
    };
    return RunInitSteps(steps).ok;
}

bool FrameRenderer::UploadMeshToGpu(GpuContext& gpu, const rs::render::Mesh& mesh) {
    // [profile] モデル切替のヒッチ計測(RS_PROFILE_LOAD 指定時のみ)。record(CPU側の
    // UploadMesh) と GPU 実行待ち(WaitForGpu)を分けて測る。
    using ProfClock = std::chrono::steady_clock;
    ProfClock::time_point t0 = ProfClock::now();
    gpu.WaitForGpu();
    mesh_ = mesh;
    keepAlive_.clear();
    gpu.ResetCmdList();
    DirectX::ResourceUploadBatch upload(gpu.Device());
    upload.Begin();
    ProfClock::time_point tRec0 = ProfClock::now();
    bool ok = meshPipeline_.UploadMesh(gpu.Device(), upload, mesh_, keepAlive_, kMainMeshGeometryKey);
    ProfClock::time_point tRec1 = ProfClock::now();
    gpu.CloseAndExecuteCmdList();
    std::future<void> uploadFinished = upload.End(gpu.Queue());
    uploadFinished.wait();
    gpu.WaitForGpu();
    keepAlive_.clear();
    if (rs::platform::ReadEnvironmentVariable("RS_PROFILE_LOAD") != nullptr) {
        ProfClock::time_point tEnd = ProfClock::now();
        double totalMs = std::chrono::duration<double, std::milli>(tEnd - t0).count();
        double recordMs = std::chrono::duration<double, std::milli>(tRec1 - tRec0).count();
        double gpuWaitMs = std::chrono::duration<double, std::milli>(tEnd - tRec1).count();
        spdlog::info("[profile] uploadMeshToGpu total={:.2f}ms (record={:.2f}ms "
                     "gpuWait={:.2f}ms)",
                     totalMs, recordMs, gpuWaitMs);
    }
    return ok;
}

bool FrameRenderer::UploadHighlightMesh(GpuContext& gpu, UINT which,
                                        const rs::render::Mesh& mesh) {
    return highlightState_.UploadMesh(gpu, which, mesh, keepAlive_);
}

void FrameRenderer::SetHighlightActive(UINT which, bool active, const rs::core::Mat4& world,
                                       const float tintColor[4]) {
    highlightState_.SetActive(which, active, world, tintColor);
}

void FrameRenderer::SetSlotHighlight(UINT which, bool active, UINT slot,
                                     const float tintColor[4]) {
    highlightState_.SetSlotHighlight(which, active, slot, tintColor);
}

D3D12_CPU_DESCRIPTOR_HANDLE FrameRenderer::DepthDsv() const {
    return meshPipeline_.DepthDsv();
}

void FrameRenderer::SetParticles(const rs::render::ParticleFrameInput& input, bool enabled) {
    particlesEnabled_ = enabled && particlePass_.Ready();
    if (!particlesEnabled_) return;
    const std::size_t dropped = particleBatch_.Build(
        input.pool, input.cameraPosition, input.cameraForward,
        [this](std::uint32_t slot) { return particlePass_.HasTexture(slot); },
        input.maxParticles);
    if (dropped > 0 && !particleOverflowWarned_) {
        particleOverflowWarned_ = true;
        spdlog::warn("[particle] render limit exceeded; {} farther particles are not drawn", dropped);
    }
}

bool FrameRenderer::InitParticles(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                   const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex,
                                   std::size_t maxParticles) {
    if (!particlePass_.Init(device, srvHeap, queue, firstSrvIndex, maxParticles)) return false;
    return meshPipeline_.WriteDepthSrv(srvHeap, particlePass_.DepthSrvIndex());
}

void FrameRenderer::BuildEnvironment() {
    rs::render::EnvironmentSrvIndices envSrv;
    envSrv.diffuse = ibl_.DiffuseSrvIndex();
    envSrv.specular = ibl_.SpecularSrvIndex();
    envSrv.brdfLut = ibl_.BrdfLutSrvIndex();
    envSrv.exposure = ibl_.ExposureSrvIndex();
    envSrv.transmittance = atmosphere_.TransmittanceSrvIndex();
    // プラグインの空: 色はアプリ層から、焼き済みキューブのSRVは envCubes_ から。GPUに上がっていなければ無効。
    rs::render::PluginSkyInput pluginSky = pluginSkyInput_;
    pluginSky.available = pluginSky.available && envCubes_.Ready();
    pluginSky.srv.diffuse = envCubes_.DiffuseSrvIndex();
    pluginSky.srv.specular = envCubes_.SpecularSrvIndex();
    pluginSky.srv.brdfLut = envCubes_.BrdfLutSrvIndex();
    pluginSky.specularMipCount = envCubes_.SpecularMipCount();
    const rs::core::Vec3 sunToSun = SunDirectionToSun();
    environmentState_ = rs::render::BuildEnvironmentState(rs::render::RenderTuning::Embedded(), envSrv,
                                                          ibl_.Ready() && atmosphere_.Ready(), pluginSky,
                                                          pluginSkyPreferred_,
                                                          rs::render::SunToSunInput{sunToSun.x, sunToSun.y, sunToSun.z},
                                                          rs::render::MoonInput{moonDirection_.x, moonDirection_.y,
                                                                                moonDirection_.z, moonBrightness_});
}

float FrameRenderer::ObserverHeightKm(const CameraView& cam) const {
    if (!rs::render::RenderTuning::Embedded().atmosphereFollowCameraHeight) return rs::render::atmosphere::kObserverHeightKm;
    return rs::render::ToGpuFloat(rs::render::atmosphere::ObserverHeightKmFromCameraMeters(cam.eye[1]));
}

bool FrameRenderer::UpdateAtmosphere(const rs::rhi::CommandList& commandList, const CameraView& cam) {
    BuildEnvironment();
    return atmosphere_.Update(commandList, SunDirectionToSun(),
                              rs::core::Vec3(environmentState_.moonDirection[0], environmentState_.moonDirection[1],
                                             environmentState_.moonDirection[2]),
                              environmentState_.moonRatio, ObserverHeightKm(cam));
}

bool FrameRenderer::UpdateIbl(const rs::rhi::CommandList& commandList) {
    bool recorded = landscape_.Bake(commandList);
    // 環境ソースは遠景と写真の空の層を含めて作る(プラグインの空の間は環境ソースを使わないので、重ねたままでよい。
    // 写真の空の層は標準の空のときだけ。使うかは Render と同じ組み立て結果の photoSky に従う)。
    BuildEnvironment();
    rs::render::IblPhotoSky photoSky;
    if (environmentState_.photoSky) {
        photoSky.specularCubeSrvGpuHandle = envCubes_.SpecularSrvGpuHandle();
        photoSky.cubeSerial = envCubes_.ContentSerial();
        photoSky.sunFound = environmentState_.photoSunFound;
        for (int i = 0; i < 3; ++i) {
            photoSky.skyMean[i] = environmentState_.photoSkyMean[i];
            photoSky.sunDirection[i] = environmentState_.photoSunDirection[i];
        }
        photoSky.yaw = environmentState_.photoYaw;
        photoSky.fullSphere = environmentState_.photoFullSphere;
    }
    rs::render::IblLandscape landscape;
    if (landscape_.Ready()) {
        landscape.srvGpuHandle = landscape_.SrvGpuHandle();
        landscape.contrast = landscape_.ContrastStats();
    }
    rs::render::IblMoon moon;
    moon.directionToMoon = rs::core::Vec3(environmentState_.moonDirection[0], environmentState_.moonDirection[1],
                                          environmentState_.moonDirection[2]);
    moon.ratio = environmentState_.moonRatio;
    recorded |= ibl_.Update(commandList, atmosphere_, SunDirectionToSun(), rs::render::RenderTuning::Embedded(),
                            landscape, photoSky, moon);
    return recorded;
}

void FrameRenderer::OnResize(ID3D12Device* device, UINT width, UINT height) {
    meshPipeline_.CreateDepthBuffer(device, width, height);
    wboit_.CreateTargets(rs::rhi::Device(device), width, height);
    targetWidth_ = width;
    targetHeight_ = height;
    // 深度の資源が新しくなったので、レンズフレアが読むSRVを書き直す。
    if (lensFlare_.Ready()) meshPipeline_.WriteDepthSrv(lensFlareSrvHeap_, lensFlare_.DepthSrvIndex());
    if (particlePass_.Ready()) meshPipeline_.WriteDepthSrv(mainSrvHeap_, particlePass_.DepthSrvIndex());
    EnsureSizedResources();
    prevViewProjValid_ = false; // 前フレームと画面の大きさが違うので、前の viewProj は使わない(前 = 今)
}

} // namespace rs::app
