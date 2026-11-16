#include "StretchZyMeshBuilder.h"

#include "ProfileMaterial.h"

namespace rs::legacy_plugin {
namespace {

using rs::core::DVec3;
using rs::legacy_pi::LineWireVertex;
using rs::legacy_pi::RailProfileFace;
using rs::legacy_pi::RailProfileVertex;

struct SectionFrame {
    DVec3 position;
    DVec3 right;
    DVec3 up;
};

bool Equal(const DVec3& a, const DVec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

DVec3 Interpolate(const DVec3& a, const DVec3& b, double t) {
    return Add(Scale(a, 1.0 - t), Scale(b, t));
}

DVec3 InBasis(const DVec3& v, const DVec3& right, const DVec3& up, const DVec3& forward) {
    return Add(Add(Scale(right, v.x), Scale(up, v.y)), Scale(forward, v.z));
}

SectionFrame BuildSectionFrame(const StretchZyMeshInput& in, const LineWireVertex& v,
                               const DVec3& endpointRight) {
    const double x = v.x, y = v.y, t = v.z;
    const StretchZyJointFrame& a = in.joints[0];
    const StretchZyJointFrame& b = in.joints[1];
    const DVec3 offsetA = Add(a.position, Add(Scale(a.right, x), Scale(a.up, y)));
    const DVec3 offsetB = Add(b.position, Add(Scale(b.right, x), Scale(b.up, y)));
    DVec3 position = Interpolate(offsetA, offsetB, t);

    // 長さ0の軸はNormalizeでゼロベクトルにする。RailSim II の退化時の既定値は未確認。
    const DVec3 right = Normalize(Interpolate(a.right, b.right, t));
    const DVec3 up = Normalize(Interpolate(a.up, b.up, t));
    const DVec3 forward = Normalize(Cross(right, up));
    if constexpr (kStretchZyOffsetApplications == 2)
        position = Add(position, Add(Scale(right, x), Scale(up, y)));

    const DVec3 dir{v.dir.x, v.dir.y, v.dir.z};
    const DVec3 localUp{v.up.x, v.up.y, v.up.z};
    const DVec3 dw = Normalize(InBasis(dir, right, up, forward));
    DVec3 sectionRight;
    if (Equal(dir, {0.0, 0.0, 1.0})) sectionRight = right;
    else if (Equal(dir, {0.0, 0.0, -1.0})) sectionRight = Scale(right, -1.0);
    else sectionRight = Normalize(Cross(dw, forward));
    const DVec3 sectionUp = Normalize(Cross(dw, sectionRight));
    const DVec3 upw = Normalize(InBasis(localUp, sectionRight, sectionUp, dw));
    if (Equal(upw, dw))
        return {position, Scale(sectionRight, -1.0), Normalize(Cross(endpointRight, dw))};
    if (Equal(upw, Scale(dw, -1.0)))
        return {position, sectionRight, Normalize(Cross(dw, sectionRight))};
    return {position, Normalize(Cross(upw, dw)), upw};
}

DVec3 EdgeNormal(const RailProfileVertex& a, const RailProfileVertex& b) {
    const double dx = static_cast<double>(b.coordX) - static_cast<double>(a.coordX);
    const double dy = static_cast<double>(b.coordY) - static_cast<double>(a.coordY);
    return Normalize(DVec3{-dy, dx, 0.0});
}

DVec3 ProfileNormal(const RailProfileFace& face, size_t k) {
    const RailProfileVertex& v = face.verts[k];
    if (v.hasNormal) return {v.normalX, v.normalY, 0.0};
    DVec3 normal;
    if (k > 0) normal = EdgeNormal(face.verts[k - 1], v);
    if (k + 1 < face.verts.size()) normal = Add(normal, EdgeNormal(v, face.verts[k + 1]));
    return Normalize(normal);
}

rs::render::MeshVertex BuildVertex(const RailProfileVertex& v, const DVec3& profileNormal,
                                  const SectionFrame& frame, double t, uint32_t partIndex) {
    const DVec3 position = Add(frame.position,
        Add(Scale(frame.right, static_cast<double>(v.coordX)),
            Scale(frame.up, static_cast<double>(v.coordY))));
    // RailSim II は変換後の法線を正規化しない。UpにDir方向の成分がある場合の差を許容し、
    // 単位長を前提とするシェーダへ渡すため、MeshVertexでは正規化する。
    const DVec3 normal = Normalize(Add(Scale(frame.right, profileNormal.x),
                                       Scale(frame.up, profileNormal.y)));
    rs::render::MeshVertex result;
    result.pos[0] = static_cast<float>(position.x);
    result.pos[1] = static_cast<float>(position.y);
    result.pos[2] = static_cast<float>(position.z);
    result.normal[0] = static_cast<float>(normal.x);
    result.normal[1] = static_cast<float>(normal.y);
    result.normal[2] = static_cast<float>(normal.z);
    result.uv[0] = v.texU;
    result.uv[1] = static_cast<float>(t);
    result.partIndex = partIndex;
    CopyProfileVertexColor(result, v);
    return result;
}

} // namespace

StretchZyMeshResult BuildStretchZyMesh(const StretchZyMeshInput& in) {
    StretchZyMeshResult result;
    if (in.profile == nullptr || in.lines == nullptr) return result;
    for (const rs::legacy_pi::LineWirePolyline& line : *in.lines) {
        for (size_t segment = 0; segment + 1 < line.verts.size(); ++segment) {
            const LineWireVertex& a = line.verts[segment];
            const LineWireVertex& b = line.verts[segment + 1];
            const SectionFrame frameA = BuildSectionFrame(in, a, in.joints[0].right);
            const SectionFrame frameB = BuildSectionFrame(in, b, in.joints[1].right);
            for (const RailProfileFace& face : in.profile->faces) {
                if (face.verts.size() < 2) continue;
                const uint32_t base = static_cast<uint32_t>(result.vertices.size());
                for (size_t k = 0; k < face.verts.size(); ++k) {
                    const DVec3 normal = ProfileNormal(face, k);
                    result.vertices.push_back(BuildVertex(face.verts[k], normal, frameA,
                                                          a.z, in.partIndex));
                    result.vertices.push_back(BuildVertex(face.verts[k], normal, frameB,
                                                          b.z, in.partIndex));
                }
                const uint32_t count = static_cast<uint32_t>(face.verts.size() * 2);
                for (uint32_t i = 0; i + 2 < count; ++i) {
                    if (i % 2 == 0)
                        result.indices.insert(result.indices.end(), {base + i, base + i + 1, base + i + 2});
                    else
                        result.indices.insert(result.indices.end(), {base + i + 1, base + i, base + i + 2});
                }
            }
        }
    }
    return result;
}

} // namespace rs::legacy_plugin
