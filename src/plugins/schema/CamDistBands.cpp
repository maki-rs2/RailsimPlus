//
// CamDistBands.cpp - _CAMDIST の比較条件から量子化境界を取り出す
//
#include "CamDistBands.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <iterator>
#include <numbers>

#include "SwitchExpr.h"
#include "SystemSwitchAxes.h"

namespace rs::legacy_pi {
namespace {

void AddNonNegativeBandStart(std::vector<int32_t>& bandStarts, int32_t value) {
    if (value >= 0) bandStarts.push_back(value);
}

} // namespace

CamDistConditionAnalysis AnalyzeCamDistCondition(std::string_view condition) {
    const SwitchComparisonBands scan =
        AnalyzeSwitchComparisons(std::string(condition),
                                 SystemSwitchName(SystemSwitchId::CamDist));
    if (!scan.referenced) return {};
    if (scan.hasUnsupportedReference)
        return CamDistConditionAnalysis{CamDistConditionKind::Unsupported, {}};
    CamDistConditionAnalysis result;
    result.kind = CamDistConditionKind::Supported;
    for (const int32_t threshold : scan.thresholds) {
        AddNonNegativeBandStart(result.bandStarts, threshold);
        AddNonNegativeBandStart(result.bandStarts, threshold == INT_MAX ? INT_MAX : threshold + 1);
    }
    return result;
}

int32_t ComputeScaledCamDistance(const float eye[3], const float position[3],
                                 float viewerFovYRadians) {
    const float dx = eye[0] - position[0];
    const float dy = eye[1] - position[1];
    const float dz = eye[2] - position[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    // RailSim II 仕様(Help/pi_sym_model_switch.html): 視野角45°の場合は距離そのもの、
    // 狭くなるほど tan(x/2) に比例して値は小さくなる(画面上の見かけの大きさに
    // ほぼ比例する値)。
    constexpr float kReferenceFovYRadians = std::numbers::pi_v<float> / 4.0f; // 45°
    const float scale = std::tan(viewerFovYRadians * 0.5f) / std::tan(kReferenceFovYRadians * 0.5f);
    return static_cast<int32_t>(std::lround(double(distance) * double(scale)));
}

int32_t QuantizeCamDistToBand(int32_t rawCamDist, const std::vector<int32_t>& bandStarts,
                              bool quantizationSafe) {
    if (!quantizationSafe || bandStarts.empty()) return rawCamDist;
    const std::vector<int32_t>::const_iterator nextBand =
        std::upper_bound(bandStarts.begin(), bandStarts.end(), rawCamDist);
    return nextBand == bandStarts.begin() ? 0 : *std::prev(nextBand);
}

} // namespace rs::legacy_pi
