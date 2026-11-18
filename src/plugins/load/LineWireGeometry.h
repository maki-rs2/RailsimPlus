#pragma once
#include <string>
#include <vector>
#include "../../core/Math.h"
#include "../schema/LineSchema.h"
#include "../schema/RailProfileSchema.h"


namespace rs::legacy_plugin {

// 描画元の形式の違いを吸収する。断面が無い場合はリボンで描く。
struct WireProfileRef {
    const std::vector<rs::legacy_pi::LineWirePolyline>* lines;
    float r, g, b, a;
    const std::vector<rs::legacy_pi::RailProfileFace>* faces = nullptr;
    std::string texFileName;
    float texVPerMeter = 1.0f;
    bool useTexture = false;
    rs::legacy_pi::ProfileMaterialExtra materialExtra;
};

std::vector<WireProfileRef> SelectLineWireProfiles(const rs::legacy_pi::LineDef& lineDef,
                                                 double spanLength);

std::vector<rs::core::DVec3> CalculateLineWirePoints(const rs::legacy_pi::LineWirePolyline& poly,
    const rs::core::Vec3& baseA, const rs::core::Vec3& dirSpan, const rs::core::Vec3& up,
    double spanLength);

} // namespace rs::legacy_plugin
