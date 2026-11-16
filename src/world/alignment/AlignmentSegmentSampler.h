//
// AlignmentSegmentSampler.h - 線形1区間(直線/円弧/緩和曲線)のサンプリング。
// SampleAlignment が区間種別ごとに呼ぶ内部部品。
//
#pragma once

#include <vector>

#include "Alignment.h"
#include "CantProfile.h"

namespace rs::world {

// 区間間で引き継ぐ状態。位置・接線・幾何アップは実際の3D姿勢。
struct SampleCursor {
    rs::core::Vec3 pos;
    rs::core::Vec3 dir;
    rs::core::Vec3 up;
    float          sCum = 0.0f; // 起点からの累積弧長[m]
    bool           first = true;
};

// 各関数は out へサンプルを追記し、cur を区間終端の状態へ進める。
void SampleLineSegment(const AlignmentSegment& seg, const std::vector<float>& localOffs,
                       const CantProfile& cantProfile, SampleCursor& cur,
                       std::vector<TrackNodeSample>& out);
void SampleArcSegment(const AlignmentSegment& seg, const std::vector<float>& localOffs,
                      const CantProfile& cantProfile, SampleCursor& cur,
                      std::vector<TrackNodeSample>& out);
void SampleTransitionSegment(const AlignmentSegment& seg, const std::vector<float>& localOffs,
                             const CantProfile& cantProfile, SampleCursor& cur,
                             std::vector<TrackNodeSample>& out);

} // namespace rs::world
