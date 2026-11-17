#include "LineWireGeometry.h"

namespace rs::legacy_plugin {

std::vector<WireProfileRef> SelectLineWireProfiles(const rs::legacy_pi::LineDef& lineDef,
                                                 double spanLength) {
    std::vector<WireProfileRef> selected;
    if (!lineDef.wiredmeshes.empty()) {
        auto toRef = [](const rs::legacy_pi::LineWiredmesh& w) {
            WireProfileRef ref;
            ref.lines = &w.lines;
            ref.r = w.diffuseR; ref.g = w.diffuseG; ref.b = w.diffuseB; ref.a = w.diffuseA;
            if (!w.faces.empty()) ref.faces = &w.faces;
            ref.texFileName = w.texFileName;
            ref.texVPerMeter = w.texVPerMeter;
            ref.useTexture = w.useTexture;
            ref.materialExtra = w.materialExtra;
            return ref;
        };
        for (const auto& w : lineDef.wiredmeshes)
            if (spanLength >= w.minInterval && spanLength < w.maxInterval)
                selected.push_back(toRef(w));
        if (selected.empty()) {
            const rs::legacy_pi::LineWiredmesh* best = nullptr;
            for (const auto& w : lineDef.wiredmeshes)
                if (!best || w.maxInterval > best->maxInterval) best = &w;
            if (best) selected.push_back(toRef(*best));
        }
    } else {
        const rs::legacy_pi::LineWireframe* wf = nullptr;
        for (const auto& w : lineDef.wireframes) {
            if (spanLength >= w.minInterval && spanLength < w.maxInterval) {
                wf = &w;
                break;
            }
        }
        if (!wf) {
            for (const auto& w : lineDef.wireframes)
                if (!wf || w.maxInterval > wf->maxInterval) wf = &w;
        }
        if (wf) selected.push_back({&wf->lines, wf->diffuseR, wf->diffuseG, wf->diffuseB, wf->diffuseA});
    }
    return selected;
}

std::vector<rs::core::DVec3> CalculateLineWirePoints(const rs::legacy_pi::LineWirePolyline& poly,
    const rs::core::Vec3& baseA, const rs::core::Vec3& dirSpan, const rs::core::Vec3& up,
    double spanLength) {
    std::vector<rs::core::DVec3> points;
    points.reserve(poly.verts.size());
    for (const rs::legacy_pi::LineWireVertex& vertex : poly.verts) {
        const double distance = vertex.z * spanLength;
        points.push_back({baseA.x + dirSpan.x * distance + static_cast<double>(up.x) * vertex.y,
                          baseA.y + dirSpan.y * distance + static_cast<double>(up.y) * vertex.y,
                          baseA.z + dirSpan.z * distance + static_cast<double>(up.z) * vertex.y});
    }
    return points;
}

} // namespace rs::legacy_plugin
