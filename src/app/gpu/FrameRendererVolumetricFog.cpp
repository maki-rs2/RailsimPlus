#include "FrameRenderer.h"

#include <cstring>
#include <optional>

#include <spdlog/spdlog.h>

#include "../../render/effects/VolumetricFogMath.h"
#include "../../render/pipeline/EnvFlag.h"
#include "../../render/resource/RenderTuning.h"
#include "../../render/sky/AtmosphereMath.h"
#include "App.h"
#include "CameraMath.h"
#include "GpuFrameTimer.h"
#include "PassSizing.h"

// FrameRenderer のボリュメトリックフォグ(資源の管理とパスの呼び出し)。

namespace rs::app {

bool FrameRenderer::InitVolumetricFog(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap) {
    if (!volumetricFog_.Init(device, srvHeap, kSrvLayout.volumetricFog) || !volumetricFogSky_.Init(device)) {
        spdlog::warn("[volumetric_fog] ボリュメトリックフォグを初期化できなかったため、使えません");
        return false;
    }
    return true;
}

namespace {

// 画面の大きさとフォグの調整値から決まる格子の大きさ。
struct FogGridSize {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t slices = 0;
};

FogGridSize FogGridFor(const rs::render::RenderTuning& tuning, TargetSize size) {
    const std::uint32_t cell = static_cast<std::uint32_t>(tuning.volumetricFogGridPixelSize);
    return {rs::render::volumetric_fog::GridCellCount(size.width, cell),
            rs::render::volumetric_fog::GridCellCount(size.height, cell),
            static_cast<std::uint32_t>(tuning.volumetricFogSliceCount)};
}

} // namespace

bool FrameRenderer::VolumetricFogActive() const {
    return volumetricFogEnabled_ && HeightFogActive() && environmentState_.iblEnabled &&
           !environmentState_.pluginSky && ShadowsEnabled();
}

bool FrameRenderer::VolumetricFogReady(UINT width, UINT height) const {
    if (!VolumetricFogActive()) return false;
    const FogGridSize grid = FogGridFor(rs::render::RenderTuning::Embedded(), TargetSize{width, height});
    return volumetricFog_.Ready(grid.width, grid.height, grid.slices);
}

void FrameRenderer::UpdateVolumetricFogResources() {
    if (!volumetricFogEnabled_ || targetWidth_ == 0 || targetHeight_ == 0) return;
    const FogGridSize grid = FogGridFor(rs::render::RenderTuning::Embedded(), TargetSize{targetWidth_, targetHeight_});
    if (!volumetricFog_.Ready(grid.width, grid.height, grid.slices))
        volumetricFog_.Resize(grid.width, grid.height, grid.slices);
}

void FrameRenderer::RecordVolumetricFog(const rs::rhi::CommandList& commandList, GpuFrameTimer* timer) {
    const ScopedAccumulate rendererScope(cpuBreakdown_.rendererMs);
    rs::render::MeshConstants& constants = frame_.constants;
    const CameraView& cam = frame_.cam;
    const ViewProjResult& viewProj = frame_.viewProj;
    const UINT width = frame_.width;
    const UINT height = frame_.height;
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();

    rs::render::VolumetricFogFrameParams params;
    std::memcpy(params.cameraPos, cam.eye, sizeof(params.cameraPos));
    const float aspectRatio = EffectiveAspect(cam, width, height);
    params.camera = rs::render::atmosphere::MakeAerialCameraFrame(viewProj.view, cam.fovYRadians, aspectRatio);
    const rs::core::Vec3 toSun = SunDirectionToSun();
    params.toSun[0] = toSun.x;
    params.toSun[1] = toSun.y;
    params.toSun[2] = toSun.z;
    params.maxDistance = tuning.volumetricFogDistance;
    params.anisotropy = tuning.volumetricFogAnisotropy;
    // 開発用: RS_VF_SUN(数値)があれば、調整値より優先する(再ビルドなしで見比べるため。ゲームの設定には出さない)。
    static const std::optional<float> kSunOverride = rs::render::ReadEnvFloat("RS_VF_SUN");
    params.sunIntensity = kSunOverride.value_or(tuning.volumetricFogSunIntensity);
    // 媒質・環境光・露出は、メッシュの定数に組み立て済みの値をそのまま使う(式の高さフォグと同じ値になる)。
    params.fogDensity = constants.apScaleAndFog[1];
    params.fogHeightScale = constants.apScaleAndFog[2];
    params.fogBaseHeight = constants.apScaleAndFog[3];
    params.fogAlbedo = constants.hfAlbedo;
    params.ambientScale = constants.iblParams[1];
    params.diffuseCubeSrvIndex = constants.iblIndex[0];
    params.transmittanceSrvIndex = constants.iblIndex[3];
    params.exposureSrvIndex = constants.adaptation[0];
    params.adaptationEnabled = constants.adaptation[1] != 0u;
    params.shadowConstantsAddress = constants.shadowConstantsAddress;
    std::memcpy(params.viewProj, viewProj.viewProj.m, sizeof(params.viewProj));
    params.historyWeight = tuning.volumetricFogHistoryWeight;
    params.frameNumber = frame_.counter;
    params.discardHistory = viewJumped_;
    if (timer) timer->Begin(commandList.Native(), kGpuRegionVolumetricFog);
    volumetricFog_.Record(commandList, params);
    if (timer) timer->End(commandList.Native(), kGpuRegionVolumetricFog);
    volumetricFogDebug_.Step(commandList, volumetricFog_, params);
    rs::render::ApplyVolumetricFogToMeshConstants(constants, volumetricFog_.IntegratedSrvIndex(), params.maxDistance,
                                                  params.anisotropy, params.sunIntensity);
}

void FrameRenderer::RecordVolumetricFogSky(const rs::rhi::CommandList& commandList,
                                           const rs::render::MeshConstants& constants) const {
    if (constants.volumetricFogIndex[0] != 0u) volumetricFogSky_.Record(commandList, constants.volumetricFogIndex[0]);
}

} // namespace rs::app
