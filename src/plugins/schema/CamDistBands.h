//
// CamDistBands.h - _CAMDIST の単純な比較条件から量子化境界を取り出す
//
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace rs::legacy_pi {

enum class CamDistConditionKind { NotReferenced, Supported, Unsupported };

struct CamDistConditionAnalysis {
    CamDistConditionKind kind = CamDistConditionKind::NotReferenced;
    std::vector<int32_t> bandStarts;
};

CamDistConditionAnalysis AnalyzeCamDistCondition(std::string_view condition);

// 視点と配置位置から _CAMDIST の生の値を求める。距離を視野角45°基準へ
// 正規化(距離 x tan(fovY/2) / tan(22.5°))して四捨五入する。
int32_t ComputeScaledCamDistance(const float eye[3], const float position[3],
                                 float viewerFovYRadians);

// 生の値をバンド開始値へ量子化する。!quantizationSafe または bandStarts が空なら
// 生の値のまま返す。先頭のバンドより前なら0。bandStarts は昇順で重複なし。
int32_t QuantizeCamDistToBand(int32_t rawCamDist, const std::vector<int32_t>& bandStarts,
                              bool quantizationSafe);

} // namespace rs::legacy_pi
