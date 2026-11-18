//
// AlignmentSegment.h - 線形の1区間・サンプリング点・カント制御点
//
// 形だけを扱う値型。軌道グラフ全体を読まずに使えるよう、TrackGraph.h から分けてある。
//
#pragma once

#include <cstdint>

#include "../../core/Math.h"

namespace rs::world {

// ---- 線形の1区間（パラメトリック） ------------------------
struct AlignmentSegment {
    // Transition = 緩和曲線（Clothoid を一般化したもの。逓減形状は shape で選ぶ）。
    enum class Kind { Line, Arc, Transition };
    // 緩和曲線の曲率逓減形状 f(u), u=l/length, κ(l)=κ0+(κ1-κ0)·f(u):
    //   Clothoid      : f(u)=u（直線逓減）
    //   CubicParabola : 3次放物線 y=x³/(6RX) の厳密曲率（小偏角でクロソイドに漸近）
    //   SineHalf      : サイン半波長逓減 f(u)=(1-cos πu)/2
    enum class TransitionShape : uint8_t { Clothoid, CubicParabola, SineHalf };
    Kind  kind = Kind::Line;
    float length = 0.0f;      // 区間長[m]
    float radius = 0.0f;      // Arc: 半径[m]（符号=曲がる向き, +X 側規約は既存に合わせる）
    float radiusEnd = 0.0f;   // Transition: 終端半径（Arc/Line は未使用, 0=直線側）
    float grade = 0.0f;       // 勾配[ratio]（縦断）
    TransitionShape shape = TransitionShape::Clothoid; // Transition のみ使用
    // Arc専用: 円弧の回転軸を水平(up0)からsegRight0側へ傾ける角度[rad]。
    // 0=水平円弧。90°で回転軸がsegRight0となり、縦断面内の円弧になる。
    // Line/Transitionでは未使用。
    float axisTiltRad = 0.0f;
    // Transition限定、CubicParabola/SineHalfの分割対応用。このセグメントが表す
    // 曲率変化が、分割前の「最初の(未分割の)緩和曲線」のu範囲のうちどの区間
    // [domainStart, domainEnd] を担当しているかを表す(0..1、未分割なら既定の0..1のまま)。
    // radius/radiusEndは常にこのセグメント自身の実際の始端/終端曲率を保持する(分割で
    // 変わらないのはdomainStart/domainEndの方で、radius/radiusEndの方は分割の都度
    // 正しい値へ再計算される。既存の全ての使う側(カント計算・UI表示・曲率連続性判定等)が
    // radius/radiusEndを「このセグメントの実際の境界曲率」として読む前提を壊さないため)。
    float domainStart = 0.0f;
    float domainEnd = 1.0f;
    // CubicParabola限定、SineHalf/Clothoidでは無視される(TransitionProfileFがtauを
    // 使わないため)。分割前の「最初の(未分割の)緩和曲線」のtau値
    // (=0.5·元の全長·|元の終端曲率-元の始端曲率|)。domainStart==0.0f&&domainEnd==1.0f
    // (未分割)の間は無視してよく、その場合はこのセグメント自身のlength/radius/radiusEnd
    // から都度計算する(Alignment.cpp側のロジック参照)。分割時にのみ子セグメントへ
    // この値を引き継ぐ。
    float domainTau = 0.0f;
};

// curve をサンプリングした Knot 点列（=Splitter 相当）
struct TrackNodeSample {
    rs::core::Vec3 pos;
    rs::core::Vec3 tangent;  // 接線（正規化）
    rs::core::Vec3 up;   // アップベクトル（カント適用済み）
    float cant = 0.0f;   // cant tan ratio (dimensionless; RailSim II CRailConnector::m_Cant)
    float s = 0.0f;      // 起点からの累積距離[m]
    // このサンプルが属する線形区間の種別（敷設プレビューの色分け等に使う）。
    AlignmentSegment::Kind kind = AlignmentSegment::Kind::Line;
};

// カントの制御点(弧長s[m]での値cant、tan比)。TrackEdge::cantPoints の要素。
struct CantControlPoint {
    float s = 0.0f;
    float cant = 0.0f;
    bool operator==(const CantControlPoint&) const = default;
};

} // namespace rs::world
