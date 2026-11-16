#include "../RenderPrecision.h"
#include "LandscapeMesh.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include "../../core/Math.h"

namespace rs::render {

namespace {

// 縁の最下行(真下)。ここでは全方位が1点に集まる。
constexpr double kNadirElevationDeg = -90.0;

// 仰角 elevationDeg の1周分(azimuthSegments+1個。縫い目は重複)を verts へ足す。
void AppendRing(std::vector<LandscapeVertex>& verts, int azimuthSegments, double elevationDeg, float v) {
    const double el = elevationDeg * rs::core::kDegToRad;
    const double cosEl = std::cos(el);
    const double sinEl = std::sin(el);
    for (int i = 0; i <= azimuthSegments; ++i) {
        const double u = static_cast<double>(i) / static_cast<double>(azimuthSegments);
        const double az = u * 2.0 * std::numbers::pi;
        LandscapeVertex vertex;
        vertex.position[0] = rs::render::ToGpuFloat(std::sin(az) * cosEl);
        vertex.position[1] = rs::render::ToGpuFloat(sinEl);
        vertex.position[2] = rs::render::ToGpuFloat(std::cos(az) * cosEl);
        vertex.uv[0] = rs::render::ToGpuFloat(u);
        vertex.uv[1] = v;
        verts.push_back(vertex);
    }
}

} // namespace

LandscapeMeshData BuildLandscapeBandMesh(int azimuthSegments, int bandSegments, int skirtSegments) {
    azimuthSegments = std::max(azimuthSegments, 3);
    bandSegments = std::max(bandSegments, 1);
    skirtSegments = std::max(skirtSegments, 1);

    LandscapeMeshData mesh;
    const double top = kLandscapeTopElevationDeg;
    const double bottom = kLandscapeBottomElevationDeg;
    const int ringCount = bandSegments + 1 + skirtSegments;
    const std::size_t ringSize = static_cast<std::size_t>(azimuthSegments) + 1;
    mesh.vertices.reserve(ringSize * static_cast<std::size_t>(ringCount));

    for (int r = 0; r <= bandSegments; ++r) {
        const double t = static_cast<double>(r) / static_cast<double>(bandSegments);
        AppendRing(mesh.vertices, azimuthSegments, std::lerp(top, bottom, t), rs::render::ToGpuFloat(t));
    }
    for (int s = 1; s <= skirtSegments; ++s) {
        const double t = static_cast<double>(s) / static_cast<double>(skirtSegments);
        AppendRing(mesh.vertices, azimuthSegments, std::lerp(bottom, static_cast<double>(kNadirElevationDeg), t), 1.0f);
    }

    mesh.indices.reserve(static_cast<std::size_t>(ringCount - 1) * static_cast<std::size_t>(azimuthSegments) * 6);
    for (int r = 0; r + 1 < ringCount; ++r) {
        for (int i = 0; i < azimuthSegments; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(static_cast<std::size_t>(r) * ringSize + i);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = a + static_cast<std::uint32_t>(ringSize);
            const std::uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    return mesh;
}

const LandscapeFaceBasis& LandscapeCubeFaceBasis(int face) {
    static const LandscapeFaceBasis kBases[6] = {
        {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},   // +X
        {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}},   // -X
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},   // +Y
        {{1, 0, 0}, {0, 0, 1}, {0, -1, 0}},   // -Y
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},    // +Z
        {{-1, 0, 0}, {0, 1, 0}, {0, 0, -1}},  // -Z
    };
    return kBases[std::clamp(face, 0, 5)];
}

} // namespace rs::render
