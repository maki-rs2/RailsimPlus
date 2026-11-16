#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "../../core/DebugDraw.h"
#include "GlyphInfo.h"

namespace rs::render {

struct OverlayVertex {
    float x = 0.0f;
    float y = 0.0f;
    float ndcZ = 0.0f;
    float acrossWidth = 0.0f;
    float alongPx = 0.0f;
    float dashPeriodPx = 0.0f;
    float dashDuty = 1.0f;
    uint32_t color = 0;
};

struct OverlayTextVertex {
    float x = 0.0f;
    float y = 0.0f;
    float ndcZ = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    uint32_t color = 0;
};

struct OverlayGeometry {
    std::vector<OverlayVertex> depthTested;
    std::vector<OverlayVertex> alwaysOnTop;
    std::vector<OverlayTextVertex> textDepthTested;
    std::vector<OverlayTextVertex> textAlwaysOnTop;
};

struct OverlayGeometryConfig {
    float attenuationReferenceDistance = 100.0f;
    float attenuationMinScale = 0.35f;
    float aaSmoothingWidthPx = 1.25f;
    float dashDutyCycle = 0.5f;
    uint32_t width = 1;
    uint32_t height = 1;
};

class OverlayGeometryBuilder {
public:
    class GlyphProvider {
    public:
        virtual ~GlyphProvider() = default;
        virtual bool GetGlyph(uint32_t codePoint, GlyphInfo& glyph) = 0;
    };
    static OverlayGeometry Build(const rs::core::DebugDrawList& list,
                                 const rs::core::Mat4& viewProj,
                                 const OverlayGeometryConfig& config,
                                 GlyphProvider* glyphProvider = nullptr);
    // output の中身を作り直す。配列の容量は残すので、毎フレーム呼ぶ側はこちらを使う。
    static void Build(const rs::core::DebugDrawList& list, const rs::core::Mat4& viewProj,
                      const OverlayGeometryConfig& config, GlyphProvider* glyphProvider,
                      OverlayGeometry& output);
    static std::vector<uint32_t> DecodeUtf8(std::string_view utf8);
    static uint32_t CircleSegmentCount(float radiusPx);
    static float DistanceAttenuation(float distance, const OverlayGeometryConfig& config);
};

} // namespace rs::render
