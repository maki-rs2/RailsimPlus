//
// GeometrySolvers.h - 線路敷設の純粋幾何ソルバ群（状態を持たないフリー関数）
//
// 3点円弧(接線-接線フィレット)・双円弧(biarc/S字)・複線オフセット・角度スナップ・
// 2接線交点を、Editor クラスや描画から独立した純関数として提供する。すべて
// rs::core::Vec3 / rs::world::AlignmentSegment のみに依存し、ヘッドレステスト
// (tests/EditorPickTests.cpp)から直接呼べる。
//
#pragma once

#include <vector>

#include "../../core/Math.h"
#include "../../world/alignment/Alignment.h"

namespace rs::world {

// 緩和曲線の指定（3点フィレットソルバへの入力）。length<=0 で緩和曲線なし。
// 円弧の前後へ同じ長さの緩和曲線を対称に挿入する（曲率 0→1/R, 1/R→0）。
struct TransitionSpec {
    rs::world::AlignmentSegment::TransitionShape shape =
        rs::world::AlignmentSegment::TransitionShape::Clothoid;
    float length = 0.0f;  // 片側の緩和曲線長[m]
};

// RailSim2流3点円弧（接線-接線フィレット）ソルバの結果。
// P0(始点)/C(中間点=頂点)/P1(終点)から、二等辺三角形フィレットに収まる
// 円弧(+前後の直線)を解く純関数の戻り値。
struct ThreePointArcSolveResult {
    bool valid = false;          // 幾何が解けたか（a<0.05 or b<0.05 で false）
    bool isStraightFallback = false; // |theta|が極小 -> 全長直線1本
    float theta = 0.0f;          // 符号付き旋回角[rad]（t0からt1への角）
    float radius = 0.0f;         // 符号付き半径[m]（直線フォールバック時は0）
    float d = 0.0f;              // 二等辺三角形の等辺長 = min(a,b)[m]
    float aLen = 0.0f;           // |C-P0|
    float bLen = 0.0f;           // |P1-C|
    float preLineLen = 0.0f;     // 始点側直線長 = max(a-d,0)[m]
    float postLineLen = 0.0f;    // 終点側直線長 = max(b-d,0)[m]
    rs::core::Vec3 endTangent;   // 終端接線（水平, 正規化。t1に一致するはず）
    std::vector<rs::world::AlignmentSegment> curve; // 確定用curve(1～5セグメント)
    float transLen = 0.0f;       // 実際に挿入した緩和曲線長[m]（片側, 0=なし）
    bool acceptable = false;     // 確定可否
    const char* rejectReason = nullptr; // acceptable==false のときの理由（表示用）
};

// P0(始点)・C(中間点=頂点)・P1(終点)から、RailSim2流の3点円弧（接線-接線
// フィレット）を解く純関数。ヘッドレステストからも直接呼べるようpublic。
// trans.length>0 なら円弧の前後へ緩和曲線を対称挿入する（移程量を考慮して
// 半径を反復で解く。旋回角が緩和曲線でほぼ使い切られる場合は確定拒否）。
ThreePointArcSolveResult SolveThreePointArc(const rs::core::Vec3& p0, const rs::core::Vec3& c,
                                            const rs::core::Vec3& p1,
                                            const TransitionSpec& trans = {});

// 敷設済みノード同士のS字接続(双円弧biarc)ソルバの結果。任意の水平接線ペアに対する
// 等パラメータd方式の一般biarc閉形式解。平行同方向オフセット(渡り線)では
// 対称等半径S字と同一解になる。
struct BiarcSolveResult {
    bool valid = false;          // 幾何が解けたか
    bool acceptable = false;     // 確定可否
    float radius = 0.0f;         // 第1弧の|R|(直線退化時は0)
    float theta = 0.0f;          // 第1弧の符号付き旋回角[rad]
    float radius2 = 0.0f;        // 第2弧の|R|(直線退化時は0)
    float theta2 = 0.0f;         // 第2弧の符号付き旋回角[rad]
    rs::core::Vec3 joint;        // 接続点J(2弧の境界)
    rs::core::Vec3 endTangent;   // 終端接線(=t1)
    std::vector<rs::world::AlignmentSegment> curve; // 2セグメント(Arc/Line、直線退化含む)
    const char* rejectReason = nullptr; // acceptable==false のときの理由(表示用)
};

// P0から接線t0で発し、P1で接線t1に到達する双円弧(biarc)を解く純関数。
// t0/t1は単位・水平。任意の接線ペアで解けるが、平行同方向で後方(P1がt0の
// 反対側)の場合など幾何的に解なしのときは valid=false。半径過小・旋回角
// 175度以上は valid=true, acceptable=false。ヘッドレステストからも直接呼べるようpublic。
BiarcSolveResult SolveBiarc(const rs::core::Vec3& p0, const rs::core::Vec3& t0,
                            const rs::core::Vec3& p1, const rs::core::Vec3& t1);

// 旋回角(theta[rad])を刻み角(stepDeg, 1〜90にclamp)へスナップした結果[rad]を返す。
// ヘッドレステスト用に公開するフリー関数。
float SnapThetaToStep(float thetaRad, float stepDeg);

// OffsetCurve の結果。
struct OffsetCurveResult {
    std::vector<rs::world::AlignmentSegment> curve;
    bool valid = false;
    const char* rejectReason = nullptr;
};

// curve を進行方向右手方向へ offset[m](正=右)平行移動する。ヘッドレステスト
// からも直接呼べるようpublicなフリー関数として公開する。
OffsetCurveResult OffsetCurve(const std::vector<rs::world::AlignmentSegment>& curve, float offset);

// P0+s*t0 = target-u*t1 (s,u共に正) の交点を求める。t0/t1は単位・水平。
// 解なし(平行/交点が後方 s<eps or u<eps)は false。
bool IntersectTangentRays(const rs::core::Vec3& p0, const rs::core::Vec3& t0,
                          const rs::core::Vec3& target, const rs::core::Vec3& t1,
                          rs::core::Vec3& outIntersection);

// 接線ハンドルドラッグ用: マウスの水平面上の向き(xzUnit、水平単位ベクトル)から
// ドラッグ中の接線候補を組み立てる。垂直(Y)成分はside(掴んだハンドルの正負)に関わらず常にoriginalTangentYを
// そのまま使う(勾配を維持する)。side<0(次数2ノードの逆方向ハンドル)ならXZ成分の
// 符号を反転する(マウスが指す方向と逆向きが実際の接線候補になる)。戻り値は
// 正規化されない場合がある(呼び出し側で正規化すること)。
rs::core::Vec3 ComposeTangentDragCandidate(const rs::core::Vec3& xzUnit, float originalTangentY, int side);

// 頂点C復元→3点フィレット再ソルブの共通処理。BuildEdgeShape(緩和曲線つきedge)と
// ノードドラッグの終端の水平弧長が使う。dirA/dirBは水平成分のみ使用(node.dirをそのまま
// 渡してよい)。nodeATangentSignは始点接線の符号(edge.nodeATangentSign)。終点接線の向きは
// dirBの±両候補を試す(スナップ接続で反転している場合があるため)。成功時、実効始点接線
// (t0、水平・単位ベクトル)をoutStartTangentへ返す。
bool TryRefitThreePointArc(const rs::core::Vec3& p0, const rs::core::Vec3& dirA, float nodeATangentSign,
                           const rs::core::Vec3& p1, const rs::core::Vec3& dirB,
                           const TransitionSpec& spec, ThreePointArcSolveResult& outSol,
                           rs::core::Vec3& outStartTangent);

} // namespace rs::world
