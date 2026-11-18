//
// SplineSampleBracket.h - サンプル列上のブラケット探索共通ヘルパー
//
// TrackNodeSample の配列(.s 昇順)から、クエリ位置 s をブラケットする隣接2サンプル
// (lowerIndex, lowerIndex+1) とそのブレンド係数 t を求める、線形補間の共通コア。
// 戻り値型や呼び出し元固有の後処理(tangent反転・frame構築等)は各呼び出し元に残し、
// ここでは探索とブレンド係数計算のみを担う。
//
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "../../core/Epsilon.h"
#include "AlignmentSegment.h"

namespace rs::world {

// ブラケット探索結果。lowerIndex/lowerIndex+1 が samples の有効範囲内であることを
// (samples.size() >= 2 の前提のもとで)常に満たす。
struct SplineBracket {
    std::size_t lowerIndex = 0;
    float t = 0.0f;
};

// 線形走査版(前方から)。呼び出し頻度が低く、samples 規模が小さい場合向け。
inline SplineBracket LocateSplineBracketLinear(const std::vector<TrackNodeSample>& samples, float s) {
    SplineBracket bracket;
    if (s <= samples.front().s) {
        bracket.lowerIndex = 0;
        bracket.t = 0.0f;
        return bracket;
    }
    if (s >= samples.back().s) {
        bracket.lowerIndex = samples.size() - 2;
        bracket.t = 1.0f;
        return bracket;
    }
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].s < s) continue;
        const float span = samples[i].s - samples[i - 1].s;
        bracket.lowerIndex = i - 1;
        bracket.t = (span > rs::core::kTrackPositionEpsM) ? (s - samples[i - 1].s) / span : 0.0f;
        return bracket;
    }
    // ここには到達しない想定(s < samples.back().s のため上のループ内で必ず return する)。
    bracket.lowerIndex = samples.size() - 2;
    bracket.t = 1.0f;
    return bracket;
}

// 二分探索版(std::upper_bound)。高頻度呼び出し・samples 規模が大きい場合向け。
// 引数は double(呼び出し元の既存の型昇格を維持し、数値結果を変えないため)。
inline SplineBracket LocateSplineBracketBinary(const std::vector<TrackNodeSample>& samples, double s) {
    SplineBracket bracket;
    const double sc = std::clamp(s, static_cast<double>(samples.front().s), static_cast<double>(samples.back().s));

    std::vector<TrackNodeSample>::const_iterator it = std::upper_bound(
        samples.begin(), samples.end(), sc,
        [](double val, const TrackNodeSample& sample) { return val < sample.s; });
    if (it == samples.begin()) ++it;
    if (it == samples.end()) --it;
    const TrackNodeSample& b = *it;
    const TrackNodeSample& a = *(it - 1);
    bracket.lowerIndex = static_cast<std::size_t>(&a - &samples[0]);

    const double span = static_cast<double>(b.s) - static_cast<double>(a.s);
    bracket.t = span > rs::core::kZeroLengthEps ? static_cast<float>((sc - static_cast<double>(a.s)) / span) : 0.0f;
    return bracket;
}

}  // namespace rs::world
