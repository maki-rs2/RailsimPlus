//
// alignment.h - 線形カーネル
//
// curve: [AlignmentSegment] + 始点フレーム(pos/dir/up) + 端点カント
// -> spline: [TrackNodeSample] を生成する純関数群。GUI 非依存・ヘッドレス数値検証。
//
#pragma once

#include <string>
#include <vector>

#include "TrackGraph.h"

namespace rs::world {

// サンプリングオプション。
struct AlignmentSampleOptions {
    float sampleStep = 1.0f;  // 既定の弧長刻み[m]
};

rs::core::Vec3 ComposeCantUp(const rs::core::Vec3& tangent, float cant,
                             const rs::core::Vec3& fallbackRight = rs::core::Vec3(1.0f, 0.0f, 0.0f));

// dir, up に直交する右方向を求める。up と dir がほぼ平行なときはワールドYアップを
// 使ったフォールバックへ切り替える(起点右方向算出と同じ方針)。SampleAlignment内部
// (Line/Arc/Transition各分岐の区間開始時基底算出)と、取り込み経路の線形生成
// (Rs2CurveFit.cpp の BuildCurveFromSplitters3D)の双方から共有する。
rs::core::Vec3 ComputeRightVector(const rs::core::Vec3& dir, const rs::core::Vec3& up);

// ロドリゲスの回転公式: axis(正規化済み)を回転軸として v を angleRad だけ回転する。
rs::core::Vec3 RotateAroundAxis(const rs::core::Vec3& v, const rs::core::Vec3& axis, float angleRad);

// 曲率(1/R)を返す。R==0 は「直線側」(曲率0)の規約。CantProfile.cpp とも共有。
float CurvatureFromRadius(float radius);

// 緩和曲線の曲率逓減形状 f(u)（u∈[0,1], f(0)=0, f(1)=1）。tau は CubicParabola 用の
// 終端接線傾き係数（他形状は未使用）。CantProfile.cpp とも共有（カント逓減の形状に
// 同じ enum を流用するため）。定義・詳細は Alignment.cpp を参照。
float TransitionProfileF(AlignmentSegment::TransitionShape shape, float u, float tau);

// CubicParabola/SineHalfのTransitionセグメントについて、piece内ローカル長さ割合uLocal
// (0..1)における曲率を返す(domainStart/domainEnd/domainTauを考慮した一般化)。
// Clothoid(線形補間で十分)やLine/Arcには使わない。
float TransitionKappaWithDomain(const AlignmentSegment& seg, float uLocal);

// curve（線形の正典）を始点フレームから弧長方向にサンプリングし、
// TrackNodeSample の列を返す。
//
// 規約（既存描画 BuildRailMesh 円弧ブランチに合わせる, Y-up 左手系）:
//   - 直線区間の既定接線は startDir。
//   - Arc: radius>0 のとき進行に伴い +X 側へ曲がる
//     （ローカル: theta=s/radius, pos=(R(1-cosT), 0, R sinT), dir=(sinT,0,cosT)）。
//   - Transition(緩和曲線): 曲率が κ0→κ1 へ shape の逓減形状 f(u) で変化。
//     1/R(s) = (1/radius) + f(s/length) * (1/radiusEnd - 1/radius)
//     （radius/radiusEnd = 0 は曲率0＝直線側）。数値積分（弧長刻みで接線角を積算→
//     台形則で位置を積算。Clothoid/SineHalf の接線角は閉形式）。
//   - 各セグメント境界・終端は必ずサンプルに含める（境界含む等分割 + 端点を必ず含める）。
//   - cant: curve に Transition が無ければ全長で cantStart→cantEnd 線形補間。
//     Transition があれば「最初の Transition より前=cantStart / 最初の Transition 内で
//     cantStart→cantMid を f(u) 逓減 / 中間=cantMid / 最後の Transition 内で
//     cantMid→cantEnd を f(u) 逓減 / それ以降=cantEnd」の曲率連動プロファイル。
//   - up は水平（起点の up を保持しつつ dir に直交するよう再直交化）した上で、
//     cant による回転（dir 軸まわりのロール）を適用したものを格納する。
// cantTransitionRatio/cantTransitionShape: TrackEdge::cantTransitionRatio/
// cantTransitionShape（既定0.0f/Clothoidは全長線形補間）。
// curve に Transition が無い場合にのみ有効（詳細は CantProfile.h を参照）。
// cantPoints: TrackEdge::cantPoints。非空なら上のカント指定をすべて無視して折れ線で決める。
std::vector<TrackNodeSample> SampleAlignment(
    const std::vector<AlignmentSegment>& curve, const rs::core::Vec3& startPos,
    const rs::core::Vec3& startDir, const rs::core::Vec3& startUp, float cantStart,
    float cantEnd, const AlignmentSampleOptions& opt = {},
    float cantMid = 0.0f, float cantTransitionRatio = 0.0f,
    AlignmentSegment::TransitionShape cantTransitionShape =
        AlignmentSegment::TransitionShape::Clothoid,
    const std::vector<CantControlPoint>& cantPoints = {});

// 緩和曲線（曲率 0→curvature を shape で逓減, 長さ length）の終端ポーズ。
// ローカル座標: x=+right(曲がる側)へのオフセット, z=初期接線方向, theta=終端偏角[rad]。
// curvature/length は正値で渡す（符号は呼び出し側で対称に処理する）。
// 敷設ソルバの移程量(シフト)計算用。数値積分（steps 分割の台形則）。
struct TransitionEndPose {
    float x = 0.0f;      // 曲がる側への横オフセット[m]
    float z = 0.0f;      // 初期接線方向の前進量[m]
    float theta = 0.0f;  // 終端接線の偏角[rad]
};
TransitionEndPose EvalTransitionEnd(AlignmentSegment::TransitionShape shape,
                                    float curvature, float length, int steps = 64);

// curve 全体の弧長（=各セグメント length の総和）。
float AlignmentTotalLength(const std::vector<AlignmentSegment>& curve);

// 両端ノードの3D姿勢(位置 + ピッチ込みの接線 × 符号 + up)とカント・緩和曲線の指定から
// edge.curve / edge.spline を作り直す(BuildEdgeShape を呼ぶ単一の入口)。
//   - 始点 = {nodeA.pos, nodeA.tangent * nodeATangentSign, nodeA.up}
//   - 終点 = {nodeB.pos, nodeB.tangent * nodeBTangentSign, nodeB.up}
// 終点は作り方の上でノード姿勢に一致し、解けなければ false を返して edge を一切変えない。
// nodeA/nodeB が graph に無いときも false。nodeBTangentSign は入力であり、この関数は
// 書き換えない(edge を新しく作る箇所で DeriveEdgeNodeBTangentSign 等で決める)。
// 成功時は区間分割(pluginSections)を長さ比で再スケールする。
bool RebuildEdgeSpline(TrackGraph& graph, TrackEdge& edge,
                       const AlignmentSampleOptions& opt = {});

// RebuildEdgeSplineWithFallback が、どの作り方で edge を作ったか。
enum class EdgeRebuildMethod {
    Exact,     // 通常の許容で作れた
    Relaxed,   // 終点の許容をゆるめて作った(形はほぼ元どおり、端がノードの姿勢から数cm以内でずれる)
    Straight,  // 両端のノードを結ぶ直線で代用した(端の向きがノードの向きと合わない)
    Failed,    // 作れなかった(端のノードが無い、同一点など)。edge は変わらない
};

// RebuildEdgeSpline で作れないとき、許容をゆるめて作り直し、それでも作れなければ両端の
// ノードを結ぶ直線で代用する。edge をグラフから消さずにつなぎを保つための経路で、
// ノードの位置と向きは変えない。保存した.rslを読むときに使う。
EdgeRebuildMethod RebuildEdgeSplineWithFallback(TrackGraph& graph, TrackEdge& edge,
                                                const AlignmentSampleOptions& opt = {});

// 新規接続や形状復元で、node.tangent に対する幾何上の接線の符号を求める。走行方向には使わない。
float EdgeTangentSignAgainst(const rs::core::Vec3& nodeTangent, const rs::core::Vec3& edgeDir);

struct EdgeEndConnection {
    int side = 0;
    float tangentSign = 1.0f;
};

// 接続済みなら面を正とする。まだリンクの無い新規接続だけ、形を指定する接線から面を作る。
EdgeEndConnection ResolveEdgeEndConnection(const TrackNode& node, const Guid& edge, int end,
                                            const rs::core::Vec3& edgeTangent);

// spline の先頭/末尾サンプルの接線と各ノードの接線の内積の符号(>=0 なら +1、他は -1)から
// edge.nodeBTangentSign を決める。弦の向きでは決めない(90度超の曲がりで外れるため)。
// spline が空、または nodeB が graph に無ければ何もしない。nodeBTangentSign の値を入れる
// 全経路(取り込み/敷設/分割結合/読込)が共通で使う単一の規則。
void DeriveEdgeNodeBTangentSign(const TrackGraph& graph, TrackEdge& edge);

// DeriveEdgeNodeBTangentSign に加えて nodeATangentSign も同じ規則で決める。nodeA 側は
// 敷設時の意図で値を決める経路があるため、既存値と食い違わないかの確認用に使う。
void DeriveEdgeTangentSigns(const TrackGraph& graph, TrackEdge& edge);

// edge長が変わったとき、区間分割(pluginSections)の境界を
// 新旧の長さ比で比例スケールする(区間の相対的な構成が編集を通して保たれる)。RebuildEdgeSpline が
// 再サンプル直後に呼ぶため通常は直接呼ばない。edge の分割・結合など、spline 再構築を
// 経ずに長さが変わる経路のために公開している。
void RescaleEdgeSections(TrackEdge& edge, double oldLen, double newLen);

// nodeGuid に直接つながる edge の spline を再生成する(node 移動・標高変更の後に呼ぶ)。
// 全 edge が成功したら true。失敗した edge は元のまま残る。
bool RebuildEdgesForNode(TrackGraph& graph, const Guid& nodeGuid,
                         const AlignmentSampleOptions& opt = {});

// nodeGuid に直接つながる全 edge が、現在のノード状態で生成できるか(グラフは変えない)。
// nodeOverride が非 null なら、nodeGuid のノードをその状態に置き換えて確かめる。
// 複数ノードの姿勢を同時に変える確認は BuildEdgeShapesUnderNodePoses(EdgeShapeBuild.h)。
bool CanRebuildEdgesForNode(const TrackGraph& graph, const Guid& nodeGuid,
                            const TrackNode* nodeOverride = nullptr);

// RebuildEdgesForNode が再サンプルする edge 集合(node に直接つながる edge)。Undoコマンドが
// 「再サンプルで区間境界が書き換わる範囲」を再ビルド本体と取り違えないよう、列挙を一本化する。
std::vector<Guid> CollectEdgesForNode(const TrackGraph& graph, const Guid& nodeGuid);

// edge内の区間分割(pluginSections)のスナップショット。
//
// RescaleEdgeSections は長さ比の掛け算なので往復すれば概ね元へ戻るが、幅が
// kMinSpanLen 未満へ潰れた区間は IntervalList の正規化に吸収されて復活しない。
// 長さを変える編集コマンドは、区間分割を明示的に保存してUndoで戻すこと。
struct EdgeSectionsSnapshot {
    Guid edge = NilGuid();
    IntervalList<PluginSet> pluginSections;
    // 長さを縮めて端へ寄せた信号は、長さを戻すだけでは元の位置へ戻らない。
    std::vector<std::pair<Guid, double>> signalPositions;
};

// ノード接線を変える操作(ノード移動・接線編集)が、つながるedgeの進行方向を保つための符号の扱い。
// edge 端の進行方向 = node.tangent * 符号 なので、接線が反対向き(内積<0)になったら、そのノードに
// つながる edge の該当端の符号を反転する。符号は Undo のためスナップショットで保存・復元する。
struct EdgeTangentSignsSnapshot {
    Guid edge = NilGuid();
    float nodeASign = 1.0f;
    float nodeBSign = 1.0f;
};
std::vector<EdgeTangentSignsSnapshot> CaptureEdgeTangentSigns(const TrackGraph& graph,
                                                              const std::vector<Guid>& edges);
void RestoreEdgeTangentSigns(TrackGraph& graph,
                             const std::vector<EdgeTangentSignsSnapshot>& snapshots);
void KeepEdgeSignsForTangentChange(TrackGraph& graph, const Guid& nodeGuid,
                                        const rs::core::Vec3& oldTangent,
                                        const rs::core::Vec3& newTangent);
// KeepEdgeSignsForTangentChange と、変更後の姿勢で edge の形を事前確認する
// BuildEdgeShapesUnderNodePoses(EdgeShapeBuild.h)が共有する規則。
// 接線が反対向き(内積<0)になるか。
bool TangentChangeReversesEdgeDirections(const rs::core::Vec3& oldTangent,
                                         const rs::core::Vec3& newTangent);
// edge の nodeGuid 側の端(nodeA/nodeB。同じnodeなら両方)の符号を反転する。
void FlipEdgeEndSignsAtNode(TrackEdge& edge, const Guid& nodeGuid);

std::vector<EdgeSectionsSnapshot> CaptureEdgeSections(const TrackGraph& graph,
                                                      const std::vector<Guid>& edges);

// スナップショットを書き戻す。**必ず spline 再構築の後に呼ぶこと**: 先に戻すと
// 再構築が内部で走らせる RescaleEdgeSections が戻した値を再び汚す。
void RestoreEdgeSections(TrackGraph& graph, const std::vector<EdgeSectionsSnapshot>& snapshots);

} // namespace rs::world
