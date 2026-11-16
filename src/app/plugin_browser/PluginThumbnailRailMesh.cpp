#include "PluginThumbnailRailMesh.h"

#include "BrowserTuning.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "../../plugins/load/RailMeshBuilder.h"
#include "../../render/mesh/MeshAssembler.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app::plugin_browser {
namespace {

// 直線を弧長サンプリングする間隔[m]。敷設済みの線形と同じく、テクスチャのV座標(弧長に比例)
// や反復配置がサンプル列から求められるよう、両端だけでなく途中にも点を置く。

// 原点から+Z方向へ長さ trackLengthM[m]伸びる、カントなしの直線のサンプル列。
std::vector<rs::world::TrackNodeSample> MakeStraightSamples(float trackLengthM) {
    const int segmentCount = static_cast<int>(std::ceil(trackLengthM / BrowserTuning::Embedded().thumbnailSampleSpacingM));
    std::vector<rs::world::TrackNodeSample> samples;
    samples.reserve(static_cast<std::size_t>(segmentCount) + 1);
    for (int i = 0; i <= segmentCount; ++i) {
        const float s = std::min(trackLengthM, static_cast<float>(i) * BrowserTuning::Embedded().thumbnailSampleSpacingM);
        rs::world::TrackNodeSample sample;
        sample.pos = rs::core::Vec3(0.0f, 0.0f, s);
        sample.tangent = rs::core::Vec3(0.0f, 0.0f, 1.0f);
        sample.up = rs::core::Vec3(0.0f, 1.0f, 0.0f);
        sample.cant = 0.0f;
        sample.s = s;
        samples.push_back(sample);
    }
    return samples;
}

// 組み上げたメッシュを撮影用にそろえる(全頂点を partIndex=0、AABBを計算)。
bool FinishMesh(rs::render::MeshAssembler& assembler, rs::render::Mesh& meshOut) {
    meshOut = assembler.Build();
    for (rs::render::MeshVertex& vertex : meshOut.vertices) vertex.partIndex = 0;
    meshOut.ComputeAabb();
    return !meshOut.Empty();
}

} // namespace

bool BuildExtrudedThumbnailMesh(const rs::legacy_plugin::LoadedPlugin& plugin, float trackLengthM,
                                  rs::render::Mesh& meshOut, float extraOffsetY) {
    // 敷設時(ViewportRailMeshBuild)と同じく、押し出し情報を持つものだけを押し出す。
    if (!plugin.ok || !plugin.def.hasRailInfo) return false;
    const std::vector<rs::world::TrackNodeSample> samples = MakeStraightSamples(trackLengthM);
    std::vector<std::string> log;
    int missing = 0;
    rs::render::MeshAssembler assembler;

    const rs::render::Mesh extruded =
        rs::legacy_plugin::BuildRailMeshAlongSpline(plugin.def, plugin.folder, samples, log, missing, extraOffsetY);
    if (!extruded.Empty()) assembler.Append(extruded);

    // まくらぎ等のInterval反復モデル。押し出しと合わせて表示する(敷設時と同じ)。
    for (const rs::legacy_plugin::LoadedIntervalModel& model :
         rs::legacy_plugin::LoadIntervalModels(plugin.def, plugin.folder)) {
        const rs::legacy_pi::RailInterval& interval = *model.interval;
        const rs::render::Mesh repeated = rs::legacy_plugin::BuildIntervalMeshAlongSpline(
            model.mesh, interval.modelScale, interval.interval, interval.offset, samples, plugin.def.gauge,
            interval.ignoreCant, extraOffsetY);
        if (!repeated.Empty()) assembler.Append(repeated);
    }
    return FinishMesh(assembler, meshOut);
}

bool BuildLineThumbnailMesh(const rs::legacy_plugin::LoadedPlugin& plugin, float trackLengthM,
                                  rs::render::Mesh& meshOut, float liftY) {
    if (!plugin.ok) return false;
    // ワイヤーの基準点は支持点ではなくトロリ線の高さ(TrolleyAlt)に、レールの積み上げ(liftY)を足した高さ。敷設時と同じ。
    const float trolleyAlt = liftY + plugin.def.line.trolleyAlt;
    const rs::core::Vec3 up(0.0f, 1.0f, 0.0f);
    const rs::core::Vec3 baseA(0.0f, trolleyAlt, 0.0f);
    const rs::core::Vec3 baseB(0.0f, trolleyAlt, trackLengthM);
    std::vector<std::string> log;
    int missing = 0;
    rs::render::MeshAssembler assembler;
    const rs::render::Mesh wire =
        rs::legacy_plugin::BuildLineWireMesh(plugin.def.line, plugin.folder, baseA, up, baseB, up, log, missing);
    if (!wire.Empty()) assembler.Append(wire);
    return FinishMesh(assembler, meshOut);
}

bool BuildTrackKindMesh(ThumbnailMethod method, const rs::legacy_plugin::LoadedPlugin& plugin,
                        float trackLengthM, rs::render::Mesh& meshOut, float extrusionOffsetY, float lineLiftY) {
    switch (method) {
        case ThumbnailMethod::Extrude:
            return BuildExtrudedThumbnailMesh(plugin, trackLengthM, meshOut, extrusionOffsetY);
        case ThumbnailMethod::Line:
            return BuildLineThumbnailMesh(plugin, trackLengthM, meshOut, lineLiftY);
        case ThumbnailMethod::Model:
            break;
    }
    return false;
}

bool BuildViewerExtrudedMesh(const rs::legacy_plugin::LoadedPlugin& plugin, ViewerDisplayMesh& out) {
    const std::optional<BrowserKind> browserKind = BrowserKindFromPluginType(plugin.def.header.type);
    if (!browserKind.has_value()) return false;
    const BrowserKindTraits& traits = GetBrowserKind(*browserKind);
    if (traits.thumbnailMethod == ThumbnailMethod::Model) return false;
    rs::render::Mesh extruded;
    if (!BuildTrackKindMesh(traits.thumbnailMethod, plugin, traits.ViewerTrackLengthM(), extruded)) return false;

    const float focusCenterZ = traits.ViewerTrackLengthM() * 0.5f;
    const float focusHalfLength = traits.ThumbnailTrackLengthM() * BrowserTuning::Embedded().viewerCameraFocusScale * 0.5f;
    for (int axis = 0; axis < 2; ++axis) {
        out.focusMin[axis] = extruded.aabbMin[axis];
        out.focusMax[axis] = extruded.aabbMax[axis];
    }
    out.focusMin[2] = focusCenterZ - focusHalfLength;
    out.focusMax[2] = focusCenterZ + focusHalfLength;
    out.mesh = std::move(extruded);
    out.extruded = true;
    return true;
}

int FindFirstDisplayableModel(const rs::legacy_plugin::LoadedPlugin& plugin) {
    for (size_t i = 0; i < plugin.models.size(); ++i) {
        if (plugin.models[i].meshOk) return static_cast<int>(i);
    }
    return -1;
}

bool BuildViewerDisplayMesh(const rs::legacy_plugin::LoadedPlugin& plugin, ViewerDisplayMesh& out) {
    out = ViewerDisplayMesh{};
    if (BuildViewerExtrudedMesh(plugin, out)) return true;
    const int modelIndex = FindFirstDisplayableModel(plugin);
    if (modelIndex < 0) return false;
    out.mesh = plugin.models[static_cast<size_t>(modelIndex)].mesh;
    return true;
}

} // namespace rs::app::plugin_browser
