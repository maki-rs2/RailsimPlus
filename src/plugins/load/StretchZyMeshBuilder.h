#pragma once

#include <cstdint>
#include <vector>

#include "../../core/Math.h"
#include "../../render/mesh/Mesh.h"
#include "../schema/LineSchema.h"

namespace rs::legacy_pi {
struct RailProfile;
} // namespace rs::legacy_pi

namespace rs::legacy_plugin {

// RailSim II の命令列では横ずらしを2回加算する。実機は未確認のため1回へ切り替え可能にする。
inline constexpr int kStretchZyOffsetApplications = 2;

struct StretchZyJointFrame {
    rs::core::DVec3 position; // ワールド座標。拡大率による除算は適用済み。
    rs::core::DVec3 right;    // 物体行列の1行目。正規化前。
    rs::core::DVec3 up;       // 物体行列の2行目。正規化前。
};

struct StretchZyMeshInput {
    const rs::legacy_pi::RailProfile* profile = nullptr;
    const std::vector<rs::legacy_pi::LineWirePolyline>* lines = nullptr;
    StretchZyJointFrame joints[2];
    uint32_t partIndex = 0;
};

struct StretchZyMeshResult {
    std::vector<rs::render::MeshVertex> vertices;
    std::vector<uint32_t> indices;
};

// 解決済みの取り付け先からワールド座標の頂点を作る。入力は変更せず、巻き順も反転しない。
StretchZyMeshResult BuildStretchZyMesh(const StretchZyMeshInput& in);

} // namespace rs::legacy_plugin
