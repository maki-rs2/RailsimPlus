//
// CantProfile.h - edge一本のカントプロファイル（弧長→カント値）
//
// SampleAlignment が使うカント計算部。curve中のTransitionセグメント構成と
// TrackEdge::cantTransitionRatio から、弧長 s におけるカント値(tan比)を返す。
//
#pragma once

#include <vector>

#include "TrackGraph.h"

namespace rs::world {

// edge一本のカントプロファイル。構築時にcurveをスキャンしてTransition区間を
// 検出し、以降はCantAt(s)で任意の弧長のカント値を返す（副作用なし、GUI非依存）。
class CantProfile {
public:
    // curve: edge.curve（Transitionセグメントの有無・位置を判定する）
    // totalLen: curve全長[m]（AlignmentTotalLengthの結果を渡す）
    // cantStart/cantEnd/cantMid: edge.cantA/cantB/cantMid
    // cantTransitionRatio: edge.cantTransitionRatio（0.0f=無効）
    // cantTransitionShape: edge.cantTransitionShape
    // cantPoints: edge.cantPoints（非空なら他のカント指定より優先し、折れ線で返す）
    CantProfile(const std::vector<AlignmentSegment>& curve, float totalLen,
                float cantStart, float cantEnd, float cantMid,
                float cantTransitionRatio,
                AlignmentSegment::TransitionShape cantTransitionShape,
                const std::vector<CantControlPoint>& cantPoints = {});

    // 起点からの弧長sにおけるカント値(tan比)。
    float CantAt(float s) const;

private:
    // curve中の1つのTransition区間（弧長範囲・形状・曲率連動の逓減パラメータ）。
    struct TransRange {
        float s0 = 0.0f, s1 = 0.0f;
        AlignmentSegment::TransitionShape shape = AlignmentSegment::TransitionShape::Clothoid;
        float tau = 0.0f;
        bool mirrored = false;  // 出口緩和(|κ0|>|κ1|): 曲率が入口の鏡像プロファイル

        float RampAt(float u) const;
    };

    float totalLen_ = 0.0f;
    float cantStart_ = 0.0f;
    float cantEnd_ = 0.0f;
    float cantMid_ = 0.0f;
    std::vector<CantControlPoint> cantPoints_;
    float cantTransitionRatio_ = 0.0f;
    AlignmentSegment::TransitionShape cantTransitionShape_ =
        AlignmentSegment::TransitionShape::Clothoid;

    TransRange firstTr_, lastTr_;
    int transCount_ = 0;

    // transCount_==0 && cantTransitionRatio_>0 のときの両端遷移長[m]（クランプ済み）。
    float ratioTransLenA_ = 0.0f;  // cantStart_ -> cantMid_（edge始点側）
    float ratioTransLenB_ = 0.0f;  // cantMid_ -> cantEnd_（edge終点側）

    float CantAtNoTransition(float s) const;      // transCount_==0
    float CantAtSingleTransition(float s) const;  // transCount_==1
    float CantAtDoubleTransition(float s) const;  // transCount_>=2
};

}  // namespace rs::world
