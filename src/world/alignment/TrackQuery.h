//
// TrackQuery.h - ワールド座標から最も近い軌道上の位置を求める問い合わせ
//
// .rs2取り込み等、Pos/Dirのみでedge/sが分からない列車の初期配置を軌道へ
// スナップするために使う。
//
#pragma once

#include <optional>

#include "../../core/Math.h"
#include "TrackPosition.h"

namespace rs::world {

struct TrackEdge;
struct TrackGraph;

// FindNearestTrackPosition の結果。found==false のときは position は未定義。
struct NearestTrackHit {
    TrackPosition position;
    double        distanceM = 0.0;
    bool          found = false;
};

struct NearestSplineHit {
    bool found = false;
    double s = 0.0;
    double distanceM = 0.0;
    rs::core::DVec3 tangent;
    double distanceSquaredM2 = 0.0;
};

// 高さの基準を変えた配置物も、同じ点列上の最近点探索を使う。
NearestSplineHit FindNearestSplinePosition(const TrackEdge& edge, const rs::core::Vec3& worldPos,
                                           float verticalOffsetM = 0.0f,
                                           double lengthSquaredEpsM2 = rs::core::kTrackSegmentLengthSquaredEpsM2);

// graph の全edge(複線中心線ダミーedgeは除く)の spline を線分列として走査し、
// worldPos に最も近い点を持つ edge とその弧長 s を返す。
// dir は、最近傍点での接線と forwardDir の内積の符号で +1/-1 を決める
// (符号が判定できない場合は +1)。距離が maxDistanceM を超える場合は found=false。
// 計算はdoubleで行い、edge.splineのfloat座標のみ境界で変換する。
NearestTrackHit FindNearestTrackPosition(const TrackGraph& graph, const rs::core::Vec3& worldPos,
                                          const rs::core::Vec3& forwardDir, double maxDistanceM);

// worldPos を edge へ射影した弧長を返す。initialS から始めて、接線方向の残差が無くなるまで
// s を詰める(高さ・横方向のずれは問わない)。edge の範囲に収まらず、残差が
// maxAlongResidualM を超えて残るなら nullopt。
std::optional<double> ProjectOntoEdgeFrom(const TrackEdge& edge, double initialS,
                                          const rs::core::Vec3& worldPos, double maxAlongResidualM);

} // namespace rs::world
