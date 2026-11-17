//
// track_graph.h - 軌道グラフ 世界モデル
//
// TrackNode(=RailConnector) / TrackEdge(=RailWay) / AlignmentSegment(線形の1区間)
// / TrackNodeSample(サンプリング結果) / TrackGraph(全体コンテナ) を定義する。
// 内部表現のみ（永続 JSON とは別レイヤ）。
//
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../core/Math.h"
#include "Guid.h"
#include "IntervalList.h"
#include "PlatformRoutine.h"
#include "PointRoutine.h"
#include "RigidPose.h"

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

// = RailConnector
struct TrackNode {
    Guid guid = NilGuid();
    rs::core::Vec3 pos;
    rs::core::Vec3 tangent{0, 0, 1};  // 接線（正規化）。node は「向き」を持つ
    rs::core::Vec3 up{0, 1, 0};   // アップベクトル（カント面法線の基準）
    float cant = 0.0f;            // カント(tan比、RailSim II の CRailConnector::m_Cant と同値。無次元)

    // RailSim II の RailConnector と同じ「2面 × 各面最大2スロット」モデル。
    // 走行は必ず一方の面から他方の面へ抜ける。
    struct Link {
        Guid edge = NilGuid();
        int  end = 0;  // 0:nodeA / 1:nodeB
        int  side = 0; // 0/1: node のどちらの面に接続するか（RailSim II の S0面/S1面）
    };
    std::vector<Link> links;

    // 面ごとの現在開通スロット。links を出現順に見たとき、その面で何本目（0始まり）が
    // 開通しているか。その面の本数が1本以下なら選択の余地が無いので意味を持たない。
    // 分岐（ポイント）かどうかは「いずれかの面が2本以上あるか」で決まる（IsPoint()）。
    std::array<int, 2> activeSlot{0, 0};

    // 分岐の開通先ルーチン表。回転は実行時カーソルで表現し、この定義は変更しない。
    std::vector<PointRoutineElement> pointRoutine;
    // 編成別・タグ別・共通の優先順位で解決する分岐ルーチン表。
    std::vector<PointTrainRoutine> pointTrainRoutines;
    // 配置前の編成定義(Formation)に紐付くルーチン。編成が配置された時点で
    // pointTrainRoutinesへ複写される(走行系はこちらを直接読まない)。
    std::vector<PointFormationRoutine> pointFormationRoutines;
    std::vector<PointTagRoutine> pointTagRoutines;
    std::vector<PointCondition> pointConditions;
    std::vector<PointTrainCondition> pointTrainConditions;
    std::vector<PointTagCondition> pointTagConditions;
    // 編成定義(Formation)別の条件の層。pointFormationRoutinesと同じ位置づけで、
    // 列車ごとの層の次、タグの層の前に評価する。
    std::vector<PointFormationCondition> pointFormationConditions;

    // 分岐の名前(UTF-8)。空なら未命名。番線名と同じく、3D画面のラベルなどに出す。
    std::string pointName;
    // 所属駅。NilGuidは未所属。
    Guid pointStationId = NilGuid();

    // --- links の面/スロットを引くための問い合わせ（走行中に毎フレーム呼ばれるため
    //     いずれも links を1回走査するだけで、確保を伴わない） ---

    int LinkCountOnSide(int side) const {
        int count = 0;
        for (const Link& link : links)
            if (link.side == side) ++count;
        return count;
    }

    // その面の slot 番目（0始まり）の links index。無ければ -1。
    int LinkIndexOnSide(int side, int slot) const {
        int seen = 0;
        for (int i = 0; i < static_cast<int>(links.size()); ++i) {
            if (links[static_cast<size_t>(i)].side != side) continue;
            if (seen == slot) return i;
            ++seen;
        }
        return -1;
    }

    // その link が自分の面で何番目か（0始まり）。範囲外なら -1。
    int SlotOfLink(int linkIndex) const {
        if (linkIndex < 0 || linkIndex >= static_cast<int>(links.size())) return -1;
        const int side = links[static_cast<size_t>(linkIndex)].side;
        int seen = 0;
        for (int i = 0; i < linkIndex; ++i)
            if (links[static_cast<size_t>(i)].side == side) ++seen;
        return seen;
    }

    bool IsBranching(int side) const { return LinkCountOnSide(side) >= 2; }
    bool IsPoint() const { return IsBranching(0) || IsBranching(1); }

    // その link が現在開通しているか（分岐していない面の link は常に false:
    // 選択の余地が無く「開通先」という概念を持たないため）。
    bool IsLinkActive(int linkIndex) const {
        const int slot = SlotOfLink(linkIndex);
        if (slot < 0) return false;
        const int side = links[static_cast<size_t>(linkIndex)].side;
        if (!IsBranching(side)) return false;
        return activeSlot[static_cast<size_t>(side)] == slot;
    }
};

// edge内の1区間に割り当てるレール系プラグイン一式
// (edge内プラグイン区間)。TrackEdge::pluginSections(IntervalList<PluginSet>)の
// 各Spanの値として使う。folders内の同一プラグインが弧長方向に離れた複数区間で
// 有効になる「飛び地」を表現できる。
struct PluginSet {
    std::vector<std::string> plugins;  // PluginSelector::TrackPluginEntries() の folderUtf8

    bool operator==(const PluginSet& other) const { return plugins == other.plugins; }
};

// .rs2取り込みPier/Poleの厳密配置(RailWay埋め込みPierList/PoleListの
// PierLink/PoleLink由来)。.rs2取り込みedgeのみ持つ。
// PierPos/PolePos(スカラー、生成器の持ち越しカウンタで無意味)とは別物。
// s/lateralOffsetM/verticalOffsetMは取り込み時点のedge.splineに対して1度だけ
// 算出するが、s自体は不変のedge固有量なので、以後はcurve編集で更新された
// 現在のspline上でこのsを再評価(EvaluateAt)すれば常に正しい位置が求まる
// (=stateless再導出に自然に乗る。Rs2PlacementNormalize.cppがこのリストを読んで
// ManualPlacementOverlayへ変換し、変換後にこの2つのベクタをclearする)。
struct ImportedPluginLink {
    std::string folderUtf8;             // PierPlugin/PolePlugin(edge単位で共通)。
                                         // 未解決(プラグインが無い)なら空文字
    double      s = 0.0;                // edge上の弧長位置
    float       lateralOffsetM = 0.0f;  // frame.up×frame.tangent(right)方向オフセット
    float       verticalOffsetM = 0.0f; // frame.up方向オフセット
    std::string address;                // Scene直下Pier/Poleの RailSim II の生 Address(16進文字列)。
                                         // 架線スパン(ImportedWireSpan)の端点解決に使う
    float       yawOffsetDeg = 0.0f;    // 水平の向きが軌道の接線と逆なら180(個体の向きを軌道へ合わせ直さない)
};

// カントの制御点(弧長s[m]での値cant、tan比)。TrackEdge::cantPoints の要素。
struct CantControlPoint {
    float s = 0.0f;
    float cant = 0.0f;
    bool operator==(const CantControlPoint&) const = default;
};

// = RailWay
struct TrackEdge {
    // 最大カントは大きさ、逓減率は距離の比なので負の値を入力しない。
    static constexpr float kMinCantAutoMax = 0.0f;
    static constexpr float kMinCantTransitionRatio = 0.0f;

    Guid guid = NilGuid();
    Guid nodeA = NilGuid(), nodeB = NilGuid();
    // 端ごとの接続側ノード(添字は 0=nodeA側 / 1=nodeB側)。Nil なら nodeA/nodeB が形の基準と
    // 接続を兼ねる(通常のレール)。Nil でなければ、その端は形をnodeA/nodeB(姿勢の基準)で決めたまま、
    // 通行・分岐はこのノードの links でつながる。論理継ぎ目(ConnectRail など)は、
    // 基準の位置とずれたノードへ端をつなぐのに使う。引くときは EdgeLinkNode を使う。
    std::array<Guid, 2> linkNodes{};

    // curve と spline は、両端ノードの3D姿勢から RebuildEdgeSpline が作る派生データ(.rsl には
    // 保存せず、読込でも作り直す)。refit.available は緩和曲線の後編集ができるedgeを表す
    // (取り込みでは IsThreePointFilletRepresentable な単純な純円弧・片側のみ直線のedgeで立てる)。
    std::vector<AlignmentSegment> curve;   // 生成関数が作った水平curve(派生)
    std::vector<TrackNodeSample>  spline;  // 両端姿勢から作った Knot 点列(派生)

    // .rs2取り込みPier/Poleの厳密配置(.rs2取り込みedgeのみ使用)。
    // TrackPluginPlacement.cppのinterval自動生成の代わりに、
    // Rs2PlacementNormalize.cppがこのリストを
    // ManualPlacementOverlayのPier/Pole個体へ正規化する(ImportRs2Scene末尾で
    // 必ずclearされる、正規化専用の入力)。
    std::vector<ImportedPluginLink> importedPiers;
    std::vector<ImportedPluginLink> importedPoles;

    // .rs2取り込みでPier/Poleプラグインとして「参照された」folder(実リンクの有無を
    // 問わない、.rs2取り込みedgeのみ使用)。Rs2Import.cpp::AppendTrackPlugins
    // が設定する。Rs2PlacementNormalize.cppがこれを読み、実リンクの有無に関わらず
    // pluginSectionsから必ず除去する(あれば個体化、無ければ空のまま=「このedgeに
    // Pier/Poleは無い」という.rs2の明示を正しく表現する)。正規化後は空文字にクリアされる
    // (importedPiers/importedPolesと同じ、正規化専用の入力)。
    std::string importedPierFolderUtf8;
    std::string importedPoleFolderUtf8;

    // 装飾プラグイン割当(軌道装飾の多重レイヤ)。edgeを弧長方向に
    // 区間分割し、区間ごとにPluginSet(folderUtf8のフラットな1本のリスト、
    // 種別(Rail/Tie/Girder/Pier/Line/Pole)は各エントリのdef側が持つためここでは
    // 分けない)を割り当てる。既定構築
    // (単一区間・空folders)=このedgeには何も配置しない。
    IntervalList<PluginSet> pluginSections;

    // このedgeのRail/Tie系プラグイン積み上げによる沈め込み量(spline基準からの
    // レール頭頂面までの持ち上げ量[m]、up方向)。spline(pos/up)自体は常に
    // 地面(ノード標高)を表す不変の基準であり続け、このoffsetはメッシュ再構築時
    // (EditorViewport.cpp)に書き込まれ、列車(TrainVehicle.cpp AxleFrame)・
    // 敷設エディタの軌道基準線描画が「レール頭頂面 = spline.pos + spline.up *
    // railTopLift」として参照する。Pier等の離散配置は地面(spline)基準のまま
    // 変更しない。
    float railTopLift = 0.0f;

    // 速度制限[km/h]。レール1本に1値。負=無指定(制限なし)。
    // 既定値は必ず -1.0f(0にすると「速度0=停止」として読めてしまう)。
    float speedLimitKmh = -1.0f;

    float cantA = 0.0f, cantB = 0.0f;  // 端点カント(tan比、RailSim II の CRailConnector::m_Cant と同値。無次元、node と整合)
    // 曲線部（円弧区間）のプラトーカント(tan比、RailSim II の CRailConnector::m_Cant と同値。無次元)。
    // 有効になるのは次のいずれか: (a) curve に Transition を含む edge
    // （cantA →第1緩和で逓減→ cantMid →最終緩和で逓減→ cantB）、または
    // (b) cantTransitionRatio > 0（下記、curve の Transition 有無に関係なく
    // cantA →両端付近の距離ベース逓減→ cantMid（プラトー）→逓減→ cantB）。
    // どちらも無効な edge では無視され、cantA→cantB の全長線形補間。
    float cantMid = 0.0f;  // tan比、無次元

    // カント差に比例した遷移長(逓減率)[m]（カント差の絶対値1あたりの遷移長）。
    // 0.0f(既定)=無効、cantA→cantB の全長線形補間(curve に Transition が
    // 無い場合)。>0 のとき、curve に Transition が無くても edge 両端付近で
    // 遷移長分だけ cantMid へ/から逓減し、それ以外は cantMid で一定のプロファイルへ
    // 切り替わる。
    // 遷移長が edge 全長を超える場合はクランプする(拒否はしない)。curve に
    // Transition を含む edge では既存の曲率連動の逓減長がそのまま使われ、この
    // フィールドは無視される(緩和曲線の実長と競合させないため)。
    float cantTransitionRatio = 0.0f;
    // カントの制御点列(弧長s→カント値、sの昇順)。空=無効(上の cantA/cantB/cantMid 等の
    // 台形/線形)。非空なら両端(0,cantA)・(全長,cantB)を暗黙の端点として区間ごとに
    // 線形補間し、cantMid/cantTransitionRatio/curve のTransitionによる逓減は使わない。
    // .rs2取り込み(Splitter.Up の焼き込み値)の途中増減を持つための保持先で、取り込み産と
    // エディタ産で同じ。カント自動モード(cantAuto)の間は無視される(自動値が形を決めるため)。
    std::vector<CantControlPoint> cantPoints;
    // 上記カント逓減の形状。既定 Clothoid(線形)。curve の Transition と同じ
    // enum(AlignmentSegment::TransitionShape)を流用する。
    AlignmentSegment::TransitionShape cantTransitionShape =
        AlignmentSegment::TransitionShape::Clothoid;
    // カント自動モード。true の間、上の cantA/cantB/cantMid/cantTransitionRatio は使わず
    // (値は残す)、AutoCant.h が curve と隣の edge から決めた値で形を作る。
    // cantAutoMax は曲線部の最大カント(tan比、符号なしの大きさ)。
    bool  cantAuto = false;
    float cantAutoMax = 0.0f;

    // 敷設プラン（緩和曲線の後編集用メタデータ）。curve はソルバの出力なので、
    // 種別・長さを後から変えるにはノード接線からフィレットを再ソルブする。その
    // 入力になるのが本プラン。3点フィレット由来の edge、および
    // IsThreePointFilletRepresentable な単純.rs2取り込みedgeでvalid=true
    // （biarc/非対称・複合曲線の.rs2取り込みは false: 緩和曲線の後編集不可）。
    struct Refit {
        AlignmentSegment::TransitionShape shape = AlignmentSegment::TransitionShape::Clothoid;
        float transitionLength = 0.0f;  // 緩和曲線長[m]（片側, 対称）。0=緩和曲線なし
        bool  available = false;
        bool operator==(const Refit&) const = default;
    } refit;
    // 敷設時の始点接線と nodeA.tangent の符号関係。nodeA の逆側(-tangent)へ延伸した
    // エッジは -1。spline 再構築(RebuildEdgeSpline)で nodeA.tangent に掛けて使う。
    float nodeATangentSign = 1.0f;
    // 終点側の対応値。edge終点でのedge進行方向 = nodeB.tangent * nodeBTangentSign
    // (nodeAと同じ流儀)。spline 再構築の入力であり、edgeを新しく作る側が
    // node.tangent と終端の進行方向の内積の符号(EdgeTangentSignAgainst)で決める。
    float nodeBTangentSign = 1.0f;
    Guid  layer = NilGuid();           // 編集レイヤ（無指定=既定）

    // --- edge種別（RailSim II の CRailWay の m_CenterlineOnly 準拠）---
    // centerlineOnly: 複線グループの仮想中心線。実レール群(trackEdges)の平均から
    //   派生する spline を持ち、複線一括仕様(プラグイン trackNum>1)の橋脚/架線柱/
    //   架線の配置基準になる。実レールの押し出しメッシュ・選択・走行・縦断からは
    //   IsCenterlineOnlyEdge() で除外される。
    // ワープ接続はwarpDummy edgeを生成しない方式（node間テレポート）とする。
    bool  centerlineOnly = false;
    int   trackCount = 0;          // centerlineOnly が代表する実レール本数 N
    float trackSpacing = 0.0f;  // 実レール間隔[m]
    std::vector<Guid> trackEdges;    // 平均元の実レールedge群(N本)。spline はこの平均から派生

    // このedgeが複線中心線のmemberEdgeであり、かつ個別ノードドラッグ(MoveNodeTo)で
    // 独立編集された(=中心線由来のOffsetCurve形状から外れた)ことを示す
    // materialize-on-touchフラグ。true の間、MoveNodeToはこのedgeの
    // curve/両端nodeを一切変更しない(固定/切り離し=freeze)。一度trueになったら自動では戻らない(削除+再敷設でのみ解除)。
    bool detachedFromCenterline = false;

    // 駅の Platform の ParentObject に追従するedge。spline と両端ノードは親の休止姿勢のまま持ち、
    // 今の姿勢は parentDelta として EvaluateAt が1か所で掛ける。parentEntity は駅の配置
    // (SceneEntity::guid)、parentPart はその駅の PluginDefinition::parts の添字。保存する。
    Guid parentEntity = NilGuid();
    int  parentPart = -1;
    // 休止姿勢から今の姿勢への剛体差分。実行時だけの値で、保存・Undo・ダイジェストに入れない
    // (TrackNode::activeSlot と同じ扱い)。毎tick、駅のアニメ評価の後に更新する。
    RigidPose parentDelta;
};

// 親オブジェクトに追従するedgeか。
inline bool IsParentDrivenEdge(const TrackEdge& edge) {
    return edge.parentPart >= 0 && !GuidIsNil(edge.parentEntity);
}

// edgeの端(0=nodeA側/1=nodeB側)が接続(通行・分岐の links)に使うノード。論理継ぎ目が無ければ
// 形の基準と同じ nodeA/nodeB。形(spline・カント)を引くときは nodeA/nodeB を直接使う。
inline const Guid& EdgeLinkNode(const TrackEdge& edge, int end) {
    const Guid& linkNode = edge.linkNodes[end == 0 ? 0 : 1];
    if (!GuidIsNil(linkNode)) return linkNode;
    return end == 0 ? edge.nodeA : edge.nodeB;
}

// 進行方向(+1=nodeA→nodeB / -1=逆)に対し、進んだ先の端の接続側ノード。
inline const Guid& EdgeLinkNodeAhead(const TrackEdge& edge, int direction) {
    return EdgeLinkNode(edge, direction >= 0 ? 1 : 0);
}

// 複線中心線ダミーedge判定。実レール専用処理(押し出し
// メッシュ・ピッキング・走行・縦断・チェーン化)はこれで除外する。
// TrackPluginPlacement/WalkPoleRuns は
// 逆に「含める」(複線一括構造物の配置先)。
inline bool IsCenterlineOnlyEdge(const TrackEdge& edge) {
    return edge.centerlineOnly;
}

// TrackNumを持つ構造物(Girder・Pier・Pole)をこのedgeに置くか。複線一括(trackNum>1)は
// 中心線edgeにだけ、単線用は実レールedgeにだけ置く。中心線が束ねる本数(trackCount)と
// trackNumの一致は見ない(RailSim II も「中心線か」と「複線用か」の一致だけで判定する)。
inline bool EdgeAcceptsTrackNum(const TrackEdge& edge, int trackNum) {
    return edge.centerlineOnly == (trackNum > 1);
}

// edge内の全区間に現れるフォルダを重複排除して返す。順序は「区間順→区間内の
// folders順」での初出順(描画順・UI表示順を決めるためソートしてはならない)。
inline std::vector<std::string> ActiveFolders(const TrackEdge& edge) {
    std::vector<std::string> out;
    for (const IntervalList<PluginSet>::Span& span : edge.pluginSections.Spans()) {
        for (const std::string& folder : span.value.plugins) {
            if (std::find(out.begin(), out.end(), folder) == out.end()) out.push_back(folder);
        }
    }
    return out;
}

// edge のいずれかの区間に folderUtf8 が含まれるか。
inline bool EdgeHasFolder(const TrackEdge& edge, const std::string& folderUtf8) {
    for (const IntervalList<PluginSet>::Span& span : edge.pluginSections.Spans()) {
        if (std::find(span.value.plugins.begin(), span.value.plugins.end(), folderUtf8) !=
            span.value.plugins.end())
            return true;
    }
    return false;
}

// folderUtf8 が有効な弧長範囲を列挙する。連続する区間はマージして1本にまとめる
// (IntervalListのSpanは常に隙間なく連続するため、隣接するfolder含有Spanは自然に
// 1本の範囲になる)。totalLenはedge全長[m](最後の区間の終端)。飛び地があれば
// 複数返る。
inline std::vector<std::pair<double, double>> FolderRanges(const TrackEdge& edge,
                                                            const std::string& folderUtf8,
                                                            double totalLen) {
    std::vector<std::pair<double, double>> out;
    const std::vector<IntervalList<PluginSet>::Span>& spans = edge.pluginSections.Spans();
    bool inRange = false;
    double rangeStart = 0.0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const bool has = std::find(spans[i].value.plugins.begin(), spans[i].value.plugins.end(),
                                   folderUtf8) != spans[i].value.plugins.end();
        if (has && !inRange) {
            inRange = true;
            rangeStart = spans[i].sStart;
        } else if (!has && inRange) {
            out.emplace_back(rangeStart, spans[i].sStart);
            inRange = false;
        }
    }
    if (inRange) out.emplace_back(rangeStart, totalLen);
    return out;
}

// 全区間を単一区間の folders に置き換える(敷設確定・取り込み時の既定経路)。
inline void SetEdgeFolders(TrackEdge& edge, std::vector<std::string> folders) {
    PluginSet value;
    value.plugins = std::move(folders);
    edge.pluginSections.SetAll(value);
}

// 駅。Undo/Redoのコマンドの約束事(ICommand::apply/revertがTrackGraphしか受け取らない)で
// 復元できるよう、番線と同じTrackGraphに置く。番線はstationIdで駅を指す一方向参照で、
// 駅側にplatforms[]の逆一覧は持たない(両方向を保存するとUndoが整合させる対象が増える)。
struct StationInfo {
    Guid id = NilGuid();
    std::string name;           // 駅名(UTF-8)
    std::string sourceAddress;  // 配置との照合に使うAddress。手動作成駅では空。
};

// 停止位置の基準。向きは進行方向で決まる(番線チェーンが進行方向で解決されるため)。
// Front=編成先頭を進行方向側の番線端へ、Center=編成中央を番線中央へ、
// Rear=編成後端を進行方向手前側の番線端へ揃える。正規化Offsetへの変換は
// PlatformChain.h の ResolveStopOffset が行う。
enum class StopReference { Front, Center, Rear };

// 駅の番線(プラットフォーム)。railEdgesが解決できたものを保持する。接近判定では
// stoppable=false の番線も必要になる。id はRailSim II 側の Platform Addressとは無関係の
// 内部識別子(インポート時に新規発行)。
struct PlatformStopInfo {
    Guid id = NilGuid();
    Guid stationId = NilGuid();  // 所属駅。解決できない番線はNilGuidのまま保持する。
    std::string name;            // 番線名(UTF-8)
    bool stoppable = true;
    // 全列車共通の停止位置設定。個別・タグの設定層が追加されても、この既定層は残る。
    StopReference stopReference = StopReference::Center;
    double stopOffsetM = 0.0;
    std::vector<PlatformRoutineElement> routine;
    std::vector<PlatformTrainRoutine> trainRoutines;
    // 配置前の編成定義(Formation)に紐付くルーチン。編成が配置された時点で
    // trainRoutinesへ複写される(走行系はこちらを直接読まない)。
    std::vector<PlatformFormationRoutine> formationRoutines;
    // List order is tag-layer priority. UI must not sort this collection.
    std::vector<PlatformTagRoutine> tagRoutines;
    std::string stationAddress;  // この番線が属する駅舎配置の RailSim II の Address
    // この番線が属する駅の配置(SceneEntity::guid)。エディタが施設の Platform から作った番線だけ持つ
    // (.rs2取り込みは stationAddress で結ぶ)。Nil=不明。
    Guid stationEntity = NilGuid();
    int indexInStation = 0;      // 駅舎内のPlatform出現順(接近マスクのビット位置)
    std::vector<Guid> railEdges;  // このプラットフォームに属するedge(RailList解決済み)
    // RailSim II の`OpenDoor = yes/no, yes/no;`(openDoor[0]=左、openDoor[1]=右)。
    // _DOOR1/_DOOR2への割り当ては読み出し側(SceneRenderer::TickTrainGroups)で
    // openDoor[1]→_DOOR1、openDoor[0]→_DOOR2と反転させている(ユーザー指定)。
    bool openDoor[2] = {false, false};
    // どちらかのドアを開けるか(ドアを閉じ始める先行時間の有無を決める)。
    bool OpensAnyDoor() const { return openDoor[0] || openDoor[1]; }
};

// 閉塞。範囲はレール1本単位(edge全長)で、edgeの一覧が正典。信号位置からの自動生成はしない。
// 1本のedgeは複数の閉塞のedgesに入れる(重なる)。nameが空なら名前なし。
// 在線状態は実行時派生データであり、ここ(Undo対象の静的データ)には
// 置かない(rs::sim::ComputeBlockOccupancy参照)。
struct Block {
    Guid guid = NilGuid();
    std::string name;
    std::vector<Guid> edges;
};

// 信号の段1つ。sidecarIndexは信号プラグインのstation.jsonの`signal`配列(正規化前)の要素番号で、
// 段のスイッチ割当を引くのに使う。
struct SignalAspectStage {
    int speedKmh = 0;
    int sidecarIndex = 0;
};

// 信号の種別。Main以外は表示専用で、閉塞を持たず列車の速度にも前方探索にも載らない。
// Relay(中継)は前方の主信号の現示を写し、Distant(遠方)は前方の主信号の1段手前の現示を出し、
// Shunting(入換)は最上段を出して強制Rで段0になる。
enum class SpeedSignalKind { Main, Relay, Distant, Shunting };

// 速度制限の信号。edge上の位置(edge, s)に自由に置き、担当閉塞
// (block)に制限速度をかける。directionはedge自身のs軸に対する±1で、同じ向きに走る列車にだけかかる。
// 担当閉塞はblockに保存する(Main以外はNil)。担当閉塞の引き方はOwnerBlock(SpeedSignalRegistry.h)。
// stagesは現示の段(速度の昇順・先頭0・狭義単調増加。SignalAspectRuntimeのIsValidAspectTable)。
// 列車にかかる制限速度は、その列車に見える現示の段の速度(SignalAspectRuntime)で、edgeの
// speedLimitKmhと重なるときは小さい方を使う(EffectiveSpeedLimitKmh参照)。
// 最上段の速度(SignalTopSpeedKmh)は表示と配置の検証用。stagesが空の信号は制限をかけない。
// pluginFolderUtf8は信号プラグイン(station.jsonに`signal`の速度表を持つStation)のフォルダ。
// 置く操作では必ず入る。読込時に保存データへ無かった信号だけ空になり、速度制限とマーカーは働く。
struct SpeedSignal {
    Guid guid = NilGuid();
    Guid edge = NilGuid();
    double s = 0.0;
    int direction = +1;
    std::vector<SignalAspectStage> stages;
    std::string pluginFolderUtf8;
    // 信号プラグインのDefineSwitch宣言順のスイッチ値。空なら全スイッチ既定0。
    // 現示で決まるスイッチ(station.jsonのsignal表が書くもの)は、描画時にこの値を現示の値で上書きする。
    std::vector<int32_t> switchOptions;
    SpeedSignalKind kind = SpeedSignalKind::Main;
    // 担当閉塞のguid。主信号は、この閉塞がedgeを含むときだけ働く。表示専用の信号はNil。
    Guid block = NilGuid();
};

} // namespace rs::world
#include "RailLinkOverlay.h"
namespace rs::world {

struct TrackGraph {
    std::unordered_map<Guid, TrackNode, GuidHash> nodes;
    std::unordered_map<Guid, TrackEdge, GuidHash> edges;
    RailLinkOverlay railLinkOverlay;

    // 駅停車統合。
    std::vector<StationInfo> stations;
    std::vector<PlatformStopInfo> platforms;
    // platform Guid → platformsのindex。FindPlatformById専用で、引いた先のIDを確かめてから使う。
    std::unordered_map<Guid, size_t, GuidHash> platformIndexById;
    std::unordered_map<Guid, Guid, GuidHash> edgeToPlatform;  // edge Guid → platform Guid
    // 接近判定用。同一edgeを複数の番線/駅舎が共有する場合も全て保持する。
    std::unordered_map<Guid, std::vector<Guid>, GuidHash> edgeToPlatforms;
    // 閉塞。edgeToBlocksはplatformsと同型の索引(edge Guid → blocksのindex配列)。
    std::vector<Block> blocks;
    std::unordered_map<Guid, std::vector<size_t>, GuidHash> edgeToBlocks;
    // block Guid → blocksのindex。FindBlock専用で、引いた先のIDを確かめてから使う。
    std::unordered_map<Guid, size_t, GuidHash> blockIndexById;
    // 速度制限の信号。edgeToSpeedSignalsはblocksと同型の索引(edge Guid → speedSignalsのindex配列)。
    std::vector<SpeedSignal> speedSignals;
    std::unordered_map<Guid, std::vector<size_t>, GuidHash> edgeToSpeedSignals;
    // 担当閉塞のguid → speedSignalsのindex配列(主信号だけ)。信号だけから決まるので、閉塞の編集では作り直さない。
    std::unordered_map<Guid, std::vector<size_t>, GuidHash> blockToSpeedSignals;
};

} // namespace rs::world
