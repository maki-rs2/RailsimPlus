#include "../scene/ParsedPluginCache.h"
#include "PluginThumbnailRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "../../core/Math.h"
#include "../../plugins/load/PluginLoader.h"
#include "../../render/mesh/MeshAssembler.h"
#include "../gpu/CameraMath.h"
#include "../plugin_session/PluginSession.h"
#include "../scene/StaticMeshVariantBake.h"
#include "PluginThumbnailRailMesh.h"
#include "ThumbnailViewCamera.h"

namespace rs::app::plugin_browser {
namespace {

// モデル系(Station/Struct/Pole/信号機/Pier)の作り方: 読み込んだプラグインの組立メッシュを焼き込む。失敗は false。
// 焼き込み後の assemblyMesh を meshOut へ入れる。
bool BuildModelMesh(PluginSession& session, rs::render::Mesh& meshOut) {
    if (!session.plugin.ok || !session.plugin.hasAssembly) return false;

    // 本編の静的インスタンス・配置プレビューと同じ焼き込み(スイッチ既定値でのChangeModel・ChangeTexture・
    // ChangeMaterialの解決、パーツのオフセットと、Move/Rotateの可動部の姿勢)を共用する。
    session.ApplySwitchOptionsAndBake({}, std::nullopt, std::nullopt);
    if (!session.plugin.hasAssembly) return false;
    meshOut = BakeSessionAssemblyMesh(session);
    return !meshOut.Empty();
}


// メッシュをY軸まわりに回して、位置・法線・サブメッシュの中心・AABBを更新する。
void RotateMeshAboutY(rs::render::Mesh& mesh, float yawRadians) {
    for (rs::render::MeshVertex& vertex : mesh.vertices) {
        RotateAboutY(vertex.pos, yawRadians, vertex.pos);
        RotateAboutY(vertex.normal, yawRadians, vertex.normal);
    }
    for (rs::render::SubMesh& submesh : mesh.submeshes) RotateAboutY(submesh.center, yawRadians, submesh.center);
    mesh.ComputeAabb();
}

} // namespace

// カメラを決めて result へ書く。result.mesh のAABBが有効であること。
// 方位はモデルのほうを回して0(+Z 側から -Z 向き)に合わせるので、メッシュも回す。
void FillThumbnailCamera(ThumbnailRecipeResult& result, ThumbnailView thumbnailView, std::uint32_t width,
                         std::uint32_t height) {
    const rs::render::Mesh& mesh = result.mesh;
    const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    const ThumbnailCameraPlan plan = ComputeThumbnailCamera(thumbnailView, mesh.aabbMin, mesh.aabbMax, aspect);

    const float yaw = ThumbnailModelYawRadians(thumbnailView);
    rs::app::CameraView view{};
    RotateAboutY(plan.target, yaw, view.target);
    RotateAboutY(plan.eye, yaw, view.eye);
    view.fovYRadians = plan.fovYRadians;
    view.renderDistance = plan.renderDistance;
    const rs::app::ViewProjResult viewProj = rs::app::BuildViewProj(view, width, height);
    std::memcpy(result.viewProj, viewProj.viewProj.m, sizeof(result.viewProj));
    std::memcpy(result.cameraPos, view.eye, sizeof(result.cameraPos));
    RotateMeshAboutY(result.mesh, yaw);
}

bool IsPluginWithoutModel(const rs::legacy_plugin::LoadedPlugin& plugin) {
    if (!plugin.ok || plugin.noDefinition || plugin.missingFiles != 0) return false;
    // 読込時に、モデルを持つパーツ・ChangeModel候補を持つパーツ・組み上がったPier/レール断面は
    // どれも models か assemblyMesh に痕跡が残る(読込失敗のパーツも空メッシュで残る)。
    // 真のダミーだけが何も残らない。
    if (!plugin.models.empty() || plugin.hasAssembly || !plugin.assemblyMesh.Empty()) return false;
    // Pier の参照・レール断面があるのに組めなかった場合は、ダミーではなく読込の失敗として扱う。
    const rs::legacy_pi::PierDef& pier = plugin.def.pier;
    if (pier.hasBase || pier.hasHead || pier.hasJoint || !plugin.def.railProfiles.empty()) return false;
    return true;
}

ThumbnailRecipeResult BuildThumbnailRecipe(const std::string& folderUtf8, BrowserKind kind, ThumbnailView view,
                                           std::uint32_t width, std::uint32_t height) {
    ThumbnailRecipeResult result;
    PluginSession session;
    std::vector<std::string> loadLog;
    rs::app::GetParsedPluginCache().LoadInto(session, folderUtf8, loadLog);
    const rs::legacy_plugin::LoadedPlugin& plugin = session.plugin;
    const BrowserKindTraits& traits = GetBrowserKind(kind);
    bool built = false;
    if (traits.thumbnailMethod == ThumbnailMethod::Model) {
        result.noModel = IsPluginWithoutModel(plugin);
        built = BuildModelMesh(session, result.mesh);
    } else {
        built = BuildTrackKindMesh(traits.thumbnailMethod, plugin, traits.ThumbnailTrackLengthM(), result.mesh);
    }
    if (!built) return result;
    FillThumbnailCamera(result, view, width, height);
    result.ok = true;
    return result;
}

ThumbnailRecipeResult BuildLayingSetRecipe(const std::vector<std::string>& folders,
                                           const std::optional<rs::app::RailTieHeights>& heightsOrNone,
                                           ThumbnailView view, std::uint32_t width, std::uint32_t height,
                                           std::stop_token stopToken) {
    ThumbnailRecipeResult result;
    // 直線の長さが含まれる種別すべての最大で決まるので、先に全部読んでから作る。
    struct Target {
        std::unique_ptr<PluginSession> session;
        const BrowserKindTraits* kind = nullptr;
    };
    std::vector<Target> targets;
    float trackLengthM = 0.0f;
    for (const std::string& folder : folders) {
        if (stopToken.stop_requested()) return result;
        std::unique_ptr<PluginSession> session = std::make_unique<PluginSession>();
        std::vector<std::string> loadLog;
        rs::app::GetParsedPluginCache().LoadInto(*session, folder, loadLog);
        if (!session->plugin.ok) continue;
        const rs::legacy_pi::PluginType type = session->plugin.def.header.type;
        if (!rs::legacy_pi::GetPluginTypeTraits(type).trackPlugin) continue;
        const std::optional<BrowserKind> browserKind = BrowserKindFromPluginType(type);
        if (!browserKind.has_value()) continue;
        const BrowserKindTraits& kind = GetBrowserKind(*browserKind);
        if (kind.thumbnailMethod == ThumbnailMethod::Model) continue;
        trackLengthM = std::max(trackLengthM, kind.ThumbnailTrackLengthM());
        targets.push_back({std::move(session), &kind});
    }
    // 高さを渡されなければ、読めたプラグインから求める(敷設と同じ、Rail・Tieの最大のHeight)。
    rs::app::RailTieHeights heights;
    if (heightsOrNone.has_value()) {
        heights = *heightsOrNone;
    } else {
        for (const Target& target : targets) {
            rs::app::AccumulateRailTieHeight(heights, target.kind->pluginType, target.session->plugin.def);
        }
    }

    rs::render::MeshAssembler assembler;
    for (const Target& target : targets) {
        const rs::legacy_plugin::LoadedPlugin& plugin = target.session->plugin;
        const rs::legacy_pi::PluginType type = target.kind->pluginType;
        rs::render::Mesh mesh;
        if (!BuildTrackKindMesh(target.kind->thumbnailMethod, plugin, trackLengthM, mesh,
                                rs::app::ExtrusionOffsetY(type, heights), heights.TopLift())) {
            continue;
        }
        // Rail は実際の敷設と同じく薄い影の束にする。
        assembler.Append(mesh, rs::legacy_pi::GetPluginTypeTraits(type).profileSupport ==
                                   rs::legacy_pi::ProfileSupport::Rail);
    }
    result.mesh = assembler.Build();
    result.mesh.ComputeAabb();
    if (result.mesh.Empty()) return result;
    FillThumbnailCamera(result, view, width, height);
    result.ok = true;
    return result;
}

} // namespace rs::app::plugin_browser
