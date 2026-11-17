#include "StretchZyMeshAssembly.h"

#include <cstring>
#include <utility>

#include "ProfileMaterial.h"
#include "runtime/StretchZyRuntime.h"
#include "../schema/PluginSchema.h"

namespace rs::legacy_plugin {
namespace {

void AppendTriangles(rs::render::Mesh& mesh, const StretchZyMeshResult& geometry,
                     uint32_t materialIndex) {
    const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(), geometry.vertices.begin(), geometry.vertices.end());
    std::vector<uint32_t> opaque, translucent;
    // BuildRailMeshの(a0,b1,b0)/(a0,a1,b1)と帯の(a0,a1,b0)/(b0,a1,b1)は同じ向き。
    // +Zへの押し出しでは断面の辺(dx,dy)に対し法線(-dy,dx,0)を向くので反転しない。
    for (size_t i = 0; i + 2 < geometry.indices.size(); i += 3) {
        const uint32_t a = geometry.indices[i];
        const uint32_t b = geometry.indices[i + 1];
        const uint32_t c = geometry.indices[i + 2];
        constexpr uint8_t kOpaqueAlpha = 255; // 頂点色RGBAのアルファ上限。
        const bool vertexAlpha = geometry.vertices[a].color[3] < kOpaqueAlpha ||
            geometry.vertices[b].color[3] < kOpaqueAlpha ||
            geometry.vertices[c].color[3] < kOpaqueAlpha;
        std::vector<uint32_t>& indices = vertexAlpha ? translucent : opaque;
        indices.insert(indices.end(), {vertexBase + a, vertexBase + b, vertexBase + c});
    }
    for (const bool vertexAlpha : {false, true}) {
        const std::vector<uint32_t>& indices = vertexAlpha ? translucent : opaque;
        if (indices.empty()) continue;
        rs::render::SubMesh submesh;
        submesh.materialIndex = materialIndex;
        submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
        submesh.indexCount = static_cast<uint32_t>(indices.size());
        submesh.vertexAlpha = vertexAlpha;
        mesh.indices.insert(mesh.indices.end(), indices.begin(), indices.end());
        mesh.submeshes.push_back(submesh);
    }
}

} // namespace

std::vector<StretchZyAssemblyPart> BuildStretchZyAssemblyMesh(const rs::legacy_pi::PluginDefinition& def,
    const std::wstring& baseDir, std::vector<std::string>& log, int& missing) {
    std::vector<StretchZyAssemblyPart> parts;
    std::vector<rs::core::Mat4> partWorlds(def.parts.size());
    for (size_t i = 0; i < def.parts.size(); ++i)
        std::memcpy(partWorlds[i].m, def.parts[i].worldMat, sizeof(partWorlds[i].m));
    for (size_t i = 0; i < def.stretchZys.size(); ++i) {
        const rs::legacy_pi::StretchZyDef& stretch = def.stretchZys[i];
        if (stretch.jointCount < 2) {
            log.push_back("[plugin][warn] StretchZY '" + stretch.label + "': JointZYXが2つ未満のため省略");
            continue;
        }
        StretchZyAssemblyPart part;
        StretchZyMeshResult geometry;
        if (!BuildStretchZyLocalMesh(stretch, def.parts, partWorlds, geometry, &part.anchorPartDefIndex)) {
            log.push_back("[plugin][warn] StretchZY '" + stretch.label + "': 取り付け先が見つからないため省略");
            continue;
        }
        part.defIndex = static_cast<int>(i);
        rs::render::Mesh& mesh = part.mesh;
        rs::render::Material mat;
        mat.diffuse[0] = mat.diffuse[1] = mat.diffuse[2] = mat.diffuse[3] = 1.0f;
        const rs::legacy_pi::RailProfile& profile = stretch.profile;
        ApplyProfileTextures(mat, profile.texFileName, profile.useTexture, profile.materialExtra,
                             baseDir, "stretchzy", log, missing);
        const uint32_t materialIndex = static_cast<uint32_t>(mesh.materials.size());
        mesh.materials.push_back(std::move(mat));
        AppendTriangles(mesh, geometry, materialIndex);
        mesh.ComputeAabb();
        parts.push_back(std::move(part));
    }
    return parts;
}

} // namespace rs::legacy_plugin
