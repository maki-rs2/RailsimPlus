#include "../../render/RenderPrecision.h"
#include "FrameRenderer.h"

#include <spdlog/spdlog.h>

#include "../../render/effects/HeadlightFlare.h"
#include "../../render/effects/SunFlareFrame.h"
#include "../../render/resource/RenderTuning.h"
#include "../../render/sky/HeightFogMath.h"
#include "CameraMath.h"
#include "GpuContext.h"

namespace rs::app {

// フレアの書き込み先の数は、フレーム資源の数(実行中のフレームと重ならないように切り替える)と同じ。
static_assert(rs::render::LensFlarePass::kRingSize == kFrameCount);

bool FrameRenderer::InitLensFlare(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                  const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex) {
    if (!lensFlare_.Init(device, srvHeap, queue, firstSrvIndex)) return false;
    lensFlareSrvHeap_ = srvHeap;
    return meshPipeline_.WriteDepthSrv(srvHeap, lensFlare_.DepthSrvIndex());
}

void FrameRenderer::RecordLensFlare(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor rtv,
                                    const CameraView& cam, UINT width, UINT height, UINT frameIndex,
                                    bool includeSun) {
    if (!lensFlare_.Ready()) return;
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    const ViewProjResult viewProj = BuildViewProj(cam, width, height);
    const rs::core::Vec3 eye(cam.eye[0], cam.eye[1], cam.eye[2]);
    const rs::core::Vec3 target(cam.target[0], cam.target[1], cam.target[2]);

    rs::render::SunFlareFrameInput input;
    input.cameraPos = eye;
    input.cameraForward = target - eye;
    input.directionToSun = SunDirectionToSun();
    input.vol = dayAlpha_;  // 太陽高度の昼夜係数(夜は0)。メッシュの直射光・空と同じ値
    if (HeightFogActive()) {
        // 高さフォグを通った太陽は弱くなる(視線が霧の中を無限遠まで進む光学的厚さ)。APは掛けない:
        // 大気の透過率は光源の色に入っている(frame.sunAtmosphere)ので、二重になる。
        const rs::core::Vec3 toSun = SunDirectionToSun();
        const float sunLength = toSun.Length();
        if (sunLength > 0.0f) {
            namespace hf = rs::render::height_fog;
            const double opticalDepth =
                hf::SunOpticalDepth(tuning.heightFogDensity, tuning.heightFogHeightScale, tuning.heightFogBaseHeight,
                                    cam.eye[1], toSun.y / sunLength);
            input.vol *= rs::render::ToGpuFloat(hf::Transmittance(opticalDepth));
        }
    }
    input.frameIndex = static_cast<std::uint32_t>(frameCounter_);
    input.flareEnabled = lensFlareSwitches_.sunFlare;
    input.whiteoutEnabled = lensFlareSwitches_.sunWhiteout;

    rs::render::LensFlareFrame frame;
    if (includeSun) rs::render::BuildSunFlareFrame(sunFlare_, input, frame);
    // 灯具は太陽の後に足す(太陽の光源・要素を先に確保し、上限は残りで数える)。
    if (lensFlareSwitches_.headlights) {
        const std::size_t dropped = rs::render::AppendHeadlightFlares(
            headlights_, eye, input.cameraForward, input.frameIndex, frame);
        if (dropped > 0 && !headlightOverflowWarned_) {
            headlightOverflowWarned_ = true;
            spdlog::warn("[lens_flare] headlight flares exceed the limits (lights {}, elements {}); "
                         "the farther ones are not drawn",
                         rs::render::kMaxFlareLights, rs::render::kMaxFlareInstances);
        }
    }
    if (!frame.HasWork()) return;
    frame.view = viewProj.view;
    frame.viewProj = viewProj.viewProj;
    frame.cameraPos = eye;
    frame.width = width;
    frame.height = height;
    frame.frameIndex = frameIndex;
    frame.flareHdrIntensity = tuning.flareHdrIntensity;
    frame.whiteoutHdrLevel = tuning.whiteoutHdrLevel;
    // 太陽には、大気を通った色と強さを掛ける(低い太陽は弱く橙になる。RailSim II にはない)。
    // プラグインの空は固定の直射光の色を使うので、透過率は掛けない(メッシュの直射光と同じ扱い)。
    frame.sunAtmosphere = atmosphere_.Ready() && !environmentState_.pluginSky;
    frame.transmittanceSrvIndex = atmosphere_.TransmittanceSrvIndex();
    // 光源の可視度がシーンの深度を読むので、描いている間だけ深度を読み取り用の状態にする。
    meshPipeline_.TransitionDepth(commandList, true);
    lensFlare_.Record(commandList, rtv, frame);
    meshPipeline_.TransitionDepth(commandList, false);
}

} // namespace rs::app
