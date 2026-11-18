//
// LineHangerPlacement.cpp - ComputeLineHangerPlacements実装。
//
#include "LineHangerPlacement.h"

#include "../../core/Epsilon.h"

#include <cmath>
#include "../schema/LineSchema.h"

namespace rs::legacy_plugin {

namespace {
using rs::core::Vec3;
} // namespace

std::vector<LineHangerPlacement> ComputeLineHangerPlacements(
    const rs::legacy_pi::LineDef& lineDef, const std::vector<rs::legacy_pi::RailInterval>& intervals,
    const Vec3& baseA, const Vec3& upA, const Vec3& baseB, const Vec3& upB) {
    std::vector<LineHangerPlacement> result;

    bool hasHangerIntervals = false;
    for (const auto& iv : intervals)
        if (!iv.wireTracking.empty() && !iv.modelFile.empty()) { hasHangerIntervals = true; break; }
    if (!hasHangerIntervals || lineDef.wireframes.empty()) return result;

    const Vec3 chord = baseB - baseA;
    const float spanLength = chord.Length();
    if (spanLength < rs::core::kWireSpanLengthEpsM) return result;

    // ハンガー取付点トラッキングのWireframeプロファイルを単一選択する
    // (BuildLineWireMeshの旧形式Wireframe選択と同じ規約: [minInterval,maxInterval)一致、
    // 無ければmaxIntervalが最大のブロックへフォールバック)。
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
    if (!wf) return result;

    const Vec3 dirSpan = chord * (1.0f / spanLength);
    Vec3 up = upA + upB;
    up = up.Length() > rs::core::kDegenerateVectorEps ? up.Normalized() : Vec3(0, 1, 0);
    if (std::abs(Vec3::Dot(up, dirSpan)) > 0.999f) up = Vec3(0, 1, 0);
    Vec3 right = Vec3::Cross(up, dirSpan).Normalized();
    up = Vec3::Cross(dirSpan, right).Normalized();

    for (const rs::legacy_pi::LineWirePolyline& poly : wf->lines) {
        for (const rs::legacy_pi::LineWireVertex& v : poly.verts) {
            if (v.label.empty()) continue;
            const rs::legacy_pi::RailInterval* iv = nullptr;
            for (const auto& cand : intervals) {
                if (cand.wireTracking == v.label && !cand.modelFile.empty()) {
                    iv = &cand;
                    break;
                }
            }
            if (!iv) continue;
            LineHangerPlacement placement;
            placement.interval = iv;
            placement.pos = baseA + dirSpan * (v.z * spanLength) + up * v.y;
            placement.fwd = dirSpan;
            placement.up = up;
            result.push_back(placement);
        }
    }
    return result;
}

} // namespace rs::legacy_plugin
