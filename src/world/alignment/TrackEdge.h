//
// TrackEdge.h - 軌道のedge(RailSim II の RailWay)と、edgeに割り当てるプラグイン区間
//
// edgeだけを使う側が、ノード・信号・駅の定義まで読まずに済むよう TrackGraph.h から分けてある。
//
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "AlignmentSegment.h"
#include "Guid.h"
#include "IntervalList.h"
#include "RigidPose.h"

namespace rs::world {

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

} // namespace rs::world
