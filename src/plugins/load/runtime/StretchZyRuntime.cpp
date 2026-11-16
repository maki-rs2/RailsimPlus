#include "StretchZyRuntime.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "../PluginLoader.h"
#include "../../schema/ZyLinkageSolve.h"

namespace rs::legacy_plugin {
namespace {

bool ResolveAttachFrame(const rs::legacy_pi::JointZYXRef& joint,
    const std::vector<rs::legacy_pi::ModelPart>& parts,
    const std::vector<rs::core::Mat4>& partWorlds,
    rs::legacy_pi::ZyAttachFrame& frame, int& partDefIndex,
    const rs::core::Mat4& systemWorld) {
    partDefIndex = -1;
    if (rs::legacy_pi::IsSystemJointTargetName(joint.target)) {
        std::copy(std::begin(systemWorld.m), std::end(systemWorld.m), frame.world);
        return true;
    }
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].label != joint.target) continue;
        if (i >= partWorlds.size()) return false;
        std::copy(std::begin(partWorlds[i].m), std::end(partWorlds[i].m), frame.world);
        frame.turn = parts[i].turn;
        partDefIndex = static_cast<int>(i);
        return true;
    }
    return false;
}

rs::core::Mat4 RelativeToAnchor(const float world[16], const float anchor[16]) {
    rs::core::Mat4 relative = rs::core::Mat4::Identity();
    // 行ベクトルの剛体変換(取り付け先の行列 × 基準の取り付け先の行列の逆)。大きな原点を先にdoubleで引く。
    const rs::core::DVec3 offset{
        static_cast<double>(world[12]) - anchor[12],
        static_cast<double>(world[13]) - anchor[13],
        static_cast<double>(world[14]) - anchor[14]};
    for (size_t axis = 0; axis < 3; ++axis) {
        const rs::core::DVec3 anchorAxis{
            anchor[axis * 4], anchor[axis * 4 + 1], anchor[axis * 4 + 2]};
        for (size_t row = 0; row < 3; ++row) {
            const rs::core::DVec3 worldAxis{
                world[row * 4], world[row * 4 + 1], world[row * 4 + 2]};
            relative.m[row * 4 + axis] = static_cast<float>(rs::core::Dot(worldAxis, anchorAxis));
        }
        relative.m[12 + axis] = static_cast<float>(rs::core::Dot(offset, anchorAxis));
    }
    return relative;
}

} // namespace

bool BuildStretchZyLocalMesh(const rs::legacy_pi::StretchZyDef& stretch,
    const std::vector<rs::legacy_pi::ModelPart>& parts,
    const std::vector<rs::core::Mat4>& partWorlds, StretchZyMeshResult& out,
    int* anchorPartDefIndex, const rs::core::Mat4& systemWorld) {
    if (stretch.jointCount < 2) return false;
    rs::legacy_pi::ZyAttachFrame attachments[2];
    int partIndices[2] = {-1, -1};
    for (int i = 0; i < 2; ++i) {
        if (!ResolveAttachFrame(stretch.joints[i], parts, partWorlds,
                                attachments[i], partIndices[i], systemWorld)) return false;
    }
    const rs::core::Mat4 identity = rs::core::Mat4::Identity();
    const rs::core::Mat4 relative = partIndices[0] == partIndices[1]
        ? identity : RelativeToAnchor(attachments[1].world, attachments[0].world);
    std::copy(std::begin(identity.m), std::end(identity.m), attachments[0].world);
    std::copy(std::begin(relative.m), std::end(relative.m), attachments[1].world);
    StretchZyMeshInput input;
    input.profile = &stretch.profile;
    input.lines = &stretch.lines;
    for (int i = 0; i < 2; ++i) {
        const rs::legacy_pi::ZyAttachFrame& attach = attachments[i];
        const rs::core::Vec3 position = rs::legacy_pi::JointPos(attach, stretch.joints[i]);
        const rs::core::Vec3 right = rs::legacy_pi::JointRight(attach);
        input.joints[i] = {{position.x, position.y, position.z},
                           {right.x, right.y, right.z},
                           {attach.world[4], attach.world[5], attach.world[6]}};
    }
    out = BuildStretchZyMesh(input);
    if (anchorPartDefIndex != nullptr) *anchorPartDefIndex = partIndices[0];
    return true;
}

StretchZyVertexRange FindStretchZyVertexRange(const rs::render::Mesh& assembly,
                                            uint32_t placementIndex) {
    StretchZyVertexRange range;
    for (size_t i = 0; i < assembly.vertices.size(); ++i) {
        if (assembly.vertices[i].partIndex != placementIndex) continue;
        if (range.count == 0) range.first = i;
        else if (i != range.first + range.count) return {};
        ++range.count;
    }
    return range;
}

int FindStretchZyPartIndex(const LoadedPlugin& plugin, uint32_t placementIndex) {
    if (plugin.stretchZyParts.size() > plugin.assemblyPlacements.size() ||
        placementIndex >= plugin.assemblyPlacements.size()) return -1;
    const size_t first = plugin.assemblyPlacements.size() - plugin.stretchZyParts.size();
    if (placementIndex < first) return -1;
    const size_t partIndex = placementIndex - first;
    const LoadedPlugin::PartPlacement& placement = plugin.assemblyPlacements[placementIndex];
    // システム名を第1取り付け先に持つ場合は、両方のanchorPartDefIndexが-1になる。
    if (placement.partDefIndex != -1 ||
        placement.anchorPartDefIndex != plugin.stretchZyParts[partIndex].anchorPartDefIndex) return -1;
    return static_cast<int>(partIndex);
}

StretchZyVertexRange FindStretchZyTailRange(const LoadedPlugin& plugin) {
    const size_t placementCount = plugin.assemblyPlacements.size();
    if (plugin.stretchZyParts.empty() || plugin.stretchZyParts.size() > placementCount) return {};
    const std::vector<rs::render::MeshVertex>& vertices = plugin.assemblyMesh.vertices;
    size_t first = vertices.size();
    for (size_t i = plugin.stretchZyParts.size(); i > 0; --i) {
        const uint32_t placement = static_cast<uint32_t>(placementCount - plugin.stretchZyParts.size() + i - 1);
        if (FindStretchZyPartIndex(plugin, placement) != static_cast<int>(i - 1)) return {};
        const size_t count = plugin.stretchZyParts[i - 1].mesh.vertices.size();
        if (count > first) return {};
        for (size_t j = 0; j < count; ++j) {
            if (vertices[--first].partIndex != placement) return {};
        }
    }
    return {first, vertices.size() - first};
}

std::vector<rs::render::MeshVertex> BuildStretchZyDynamicVertices(
    const LoadedPlugin& plugin, const std::vector<rs::core::Mat4>& partWorlds,
    const rs::core::Mat4& systemWorld) {
    const StretchZyVertexRange tail = FindStretchZyTailRange(plugin);
    if (tail.count == 0) return {};
    rs::render::Mesh mesh;
    mesh.vertices.assign(plugin.assemblyMesh.vertices.begin() + static_cast<std::ptrdiff_t>(tail.first),
                         plugin.assemblyMesh.vertices.end());
    size_t first = 0;
    for (const StretchZyAssemblyPart& part : plugin.stretchZyParts) {
        const StretchZyVertexRange range{first, part.mesh.vertices.size()};
        first += range.count;
        if (part.defIndex < 0 || static_cast<size_t>(part.defIndex) >= plugin.def.stretchZys.size()) continue;
        StretchZyMeshResult geometry;
        if (BuildStretchZyLocalMesh(plugin.def.stretchZys[static_cast<size_t>(part.defIndex)],
                                    plugin.def.parts, partWorlds, geometry, nullptr, systemWorld))
            UpdateStretchZyVertices(mesh, range, geometry);
    }
    return std::move(mesh.vertices);
}

bool UpdateStretchZyVertices(rs::render::Mesh& mesh, StretchZyVertexRange range,
                            const StretchZyMeshResult& geometry) {
    if (range.count != geometry.vertices.size() || range.first > mesh.vertices.size() ||
        range.count > mesh.vertices.size() - range.first) return false;
    for (size_t i = 0; i < range.count; ++i) {
        rs::render::MeshVertex& vertex = mesh.vertices[range.first + i];
        const rs::render::MeshVertex& source = geometry.vertices[i];
        std::copy(std::begin(source.pos), std::end(source.pos), vertex.pos);
        std::copy(std::begin(source.normal), std::end(source.normal), vertex.normal);
    }
    return true;
}

} // namespace rs::legacy_plugin
