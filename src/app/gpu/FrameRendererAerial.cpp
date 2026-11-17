#include "FrameRenderer.h"

#include "CameraMath.h"
#include "../../render/pipeline/EnvFlag.h"
#include "../../render/pipeline/MeshConstantsEnvironment.h"
#include "../../render/resource/RenderTuning.h"
#include "../../render/sky/EnvironmentState.h"

namespace rs::app {

bool FrameRenderer::AerialPerspectiveActive() const {
    return atmosphericPerspective_;
}

bool FrameRenderer::UpdateAerialPerspective(const rs::rhi::CommandList& commandList, const CameraView& cam,
                                            UINT width, UINT height) {
    // 切っているときはLUTを作らない(コストを払わない)。
    aerialPerspective_.SetEnabled(AerialPerspectiveActive());
    if (!aerialPerspective_.Ready() || !atmosphere_.Ready()) return false;
    // メインの描画と同じ view・縦の視野角・縦横比から視線の定数を作る(画素の視線が一致する)。
    const ViewProjResult viewProj = BuildViewProj(cam, width, height);
    const float aspectRatio = EffectiveAspect(cam, width, height);
    const rs::render::atmosphere::AerialCameraFrame camera =
        rs::render::atmosphere::MakeAerialCameraFrame(viewProj.view, cam.fovYRadians, aspectRatio);
    const bool recorded = aerialPerspective_.Update(commandList, atmosphere_, SunDirectionToSun(), camera,
                                                    ObserverHeightKm(cam));
    if (recorded) {
        // メッシュが引くLUTは、このカメラで作ったもの。LUTが古いままのフレームも、組になったこのカメラで引く。
        lastAerialCamera_ = camera;
        aerialCameraValid_ = true;
    }
    return recorded;
}

void FrameRenderer::ApplyAerialPerspectiveConstants(rs::render::MeshConstants& constants,
                                                    rs::render::ViewConstants& view) const {
    rs::render::AerialPerspectiveFrame frame;
    // 一度もLUTを作っていない間(起動直後など)は何も引かない。
    frame.enabled = AerialPerspectiveActive() && aerialPerspective_.Ready() && aerialCameraValid_;
    if (frame.enabled) {
        frame.srvIndex = aerialPerspective_.SrvIndex();
        frame.sliceCount = aerialPerspective_.SliceCount();
        frame.tileSize = aerialPerspective_.TileSize();
        frame.maxDistanceKm = aerialPerspective_.MaxDistanceKm();
        frame.camera = lastAerialCamera_;
        frame.skyRadianceScale = environmentState_.skyRadianceScale;  // 空パスと同じ倍率
    }
    rs::render::ApplyAerialPerspectiveToMeshConstants(frame, constants, view);
}

bool FrameRenderer::HeightFogActive() const {
    return heightFog_;
}

void FrameRenderer::ApplyHeightFogConstants(rs::render::MeshConstants& constants) const {
    const rs::render::RenderTuning& tuning = rs::render::RenderTuning::Embedded();
    rs::render::HeightFogFrame frame = rs::render::MakeHeightFogFrame(tuning, HeightFogActive());
    // 開発用: RS_FOG_DENSITY(数値)があれば、調整値より優先する(再ビルドなしで見比べるため。ゲームの設定には出さない)。
    static const std::optional<float> kDensityOverride = rs::render::ReadEnvFloat("RS_FOG_DENSITY");
    frame.density = kDensityOverride.value_or(frame.density);
    rs::render::ApplyHeightFogToMeshConstants(frame, constants);
}

} // namespace rs::app
