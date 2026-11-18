//
// EdgeShapeBuild.h - 両端ノードの3D姿勢から edge の curve と spline を作る単一の入口
//
// 取り込み(.rs2)edge・敷設エディタ産edgeを区別せず、全edgeの形をここで作る。
// 入力は両端の3D姿勢(位置+ピッチ込みの接線)・カント・緩和曲線の指定だけで、
// グラフには触らない純関数。edge の spline と curve を書くのは RebuildEdgeSpline が
// この関数を呼ぶ1か所だけ。
//
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Alignment.h"
#include "EdgeShapeGenerator.h"
#include "../../core/Math.h"
#include "AlignmentSegment.h"
#include "Guid.h"
#include "TrackNode.h"

namespace rs::world {

struct TrackEdge;
struct TrackGraph;

struct EdgeShapeInput {
    EdgeEndPose start;  // dir はピッチ込みの3D単位ベクトル(ノード接線 × 符号)
    EdgeEndPose end;
    float cantA = 0.0f;
    float cantB = 0.0f;
    float cantMid = 0.0f;
    float cantTransitionRatio = 0.0f;
    std::vector<CantControlPoint> cantPoints;  // TrackEdge::cantPoints（非空なら台形より優先）
    AlignmentSegment::TransitionShape cantTransitionShape =
        AlignmentSegment::TransitionShape::Clothoid;
    //カント自動モード(TrackEdge::cantAuto)。true なら上の cantA/cantB/cantMid/
    //cantTransitionRatio は使わず、BuildEdgeShape が作った curve から AutoCant.h で決める。
    //autoNeighborPlateauA/B は両端の相手の自動プラトー(この edge の向きの符号、
    //グラフを渡さない呼び出しでは 0)。
    bool cantAuto = false;
    float cantAutoMax = 0.0f;
    float autoNeighborPlateauA = 0.0f;
    float autoNeighborPlateauB = 0.0f;
    // 緩和曲線の指定(TrackEdge::refit)。length<=0 なら緩和なし。
    AlignmentSegment::TransitionShape transitionShape =
        AlignmentSegment::TransitionShape::Clothoid;
    float transitionLength = 0.0f;
    AlignmentSampleOptions sample;
    EdgeShapeOptions options;  // 終点の許容など
};

struct EdgeShapeBuild {
    bool ok = false;
    std::vector<AlignmentSegment> curve;   // 失敗時は空
    std::vector<TrackNodeSample> spline;   // 失敗時は空
    std::string error;                     // 失敗理由(成功時は空)
    // 緩和ありで解いたとき、ソルバが実際に入れた緩和曲線長[m](片側)。要求長が
    // 幾何学的な最大長で頭打ちになった値で、緩和なしのときは 0。
    float effectiveTransitionLength = 0.0f;
};

// 緩和なし: GenerateEdgeCurve の curve を SampleAlignment でサンプルする。
// 緩和あり(transitionLength>0): 水平形を TryRefitThreePointArc で作り、縦は両端の
// ピッチから3次エルミートでつなぐ(緩和曲線edgeの縦は両端ピッチの3次曲線)。
// どちらも、サンプル後に終点の残差を s に比例して全サンプルへ配り、最後のサンプルを
// end の位置・接線にそろえる。残差が許容(位置2mm・向き0.01度)を超えたら ok=false。
EdgeShapeBuild BuildEdgeShape(const EdgeShapeInput& input);

// ノードと edge の符号・カント・緩和指定から EdgeShapeInput を作る(姿勢の組み立てを
// 1か所にする)。graph を渡すと、カント自動モードの edge は隣の edge の自動値を読んで
// つなぎ目に使う。渡さない呼び出し(形の事前確認など)ではつなぎ目を 0 として作る。
EdgeShapeInput MakeEdgeShapeInput(const TrackEdge& edge, const TrackNode& nodeA,
                                  const TrackNode& nodeB, const AlignmentSampleOptions& opt = {},
                                  const TrackGraph* graph = nullptr);

// ノードの姿勢を変えた後の edge の形の事前確認(グラフは変えない)。
struct EdgeShapesUnderNodePoses {
    bool ok = false;
    std::vector<std::pair<Guid, EdgeShapeBuild>> edges;  // 作った順(失敗時は空)
    std::string error;                                   // 失敗理由(成功時は空)
};

// nodePoses(guid→変更後のノード)の各ノードに直接つながる全 edge の形を、変更後の姿勢で作る。
// ノード接線が反対向きになる edge 端の符号は MoveNodeCommand と同じ規則で反転して作るので、
// 確定後の RebuildEdgeSpline と同じ入力になり、同じ spline が得られる。1本でも作れなければ
// ok=false。edge に使うカント・緩和曲線の指定は edge の現在値。
// keepEdgeSignsNodes に入れたノードは、接線が反対向きになっても符号を反転しない(MoveNodeCommand の
// keepEdgeSigns と同じ)。
EdgeShapesUnderNodePoses BuildEdgeShapesUnderNodePoses(
    const TrackGraph& graph, const std::unordered_map<Guid, TrackNode, GuidHash>& nodePoses,
    const std::unordered_set<Guid, GuidHash>& keepEdgeSignsNodes = {});

// 3次エルミート基底(t∈[0,1])とその t による微分。
template <typename T>
inline void HermiteBasis(T t, T& h00, T& h10, T& h01, T& h11) {
    const T t2 = t * t, t3 = t2 * t;
    h00 = 2 * t3 - 3 * t2 + 1;
    h10 = t3 - 2 * t2 + t;
    h01 = -2 * t3 + 3 * t2;
    h11 = t3 - t2;
}
template <typename T>
inline void HermiteBasisDeriv(T t, T& d00, T& d10, T& d01, T& d11) {
    const T t2 = t * t;
    d00 = 6 * t2 - 6 * t;
    d10 = 3 * t2 - 4 * t + 1;
    d01 = -6 * t2 + 6 * t;
    d11 = 3 * t2 - 2 * t;
}

// 新規ノードの接線: 水平接線にピッチ(弦勾配 atan2(Δy, 水平弧長))を付ける。
// horizontalTangent は水平単位ベクトル。敷設確定(単線・複線)が共通で使う。
rs::core::Vec3 PitchedTangent(const rs::core::Vec3& horizontalTangent, float pitchRad);

// 接線の勾配[rad] = asin(tangent.y / |tangent|)。ゼロベクトルは 0。PitchedTangent の逆。
float TangentPitchRad(const rs::core::Vec3& tangent);

// 弦勾配のピッチ[rad] = atan2(deltaY, horizontalLength)。horizontalLength<=0 なら 0。
float ChordPitchRad(float deltaY, float horizontalLength);

}  // namespace rs::world
