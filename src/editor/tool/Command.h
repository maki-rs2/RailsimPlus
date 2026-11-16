//
// command.h - Undo/Redo コマンド基盤
//
// ICommand { Apply(TrackGraph&); Revert(TrackGraph&); } + CommandStack。
// 最小セット: AddEdge / RemoveEdge / MoveNode / SetEdgeProperty。
//
#pragma once
#include "../../world/alignment/TrackGraphReferences.h"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "../../world/alignment/Alignment.h"
#include "../../world/alignment/EdgeSplitMerge.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/WarpLinkTable.h"

namespace rs::world { class SceneRegistry; }

namespace rs::editor {

// std::vector の == は要素が比べられなくても宣言だけはあるので、要素まで見て判定する。
template <class T>
struct FieldComparable : std::bool_constant<std::equality_comparable<T>> {};
template <class T, class A>
struct FieldComparable<std::vector<T, A>> : FieldComparable<T> {};

// incoming があれば target へ書き、前の値を savedOld へ控える。値が変わったら true
// (比べられない型は、書いたら変わったとみなす)。
template <class T>
bool ApplyOptionalField(const std::optional<T>& incoming, T& target, std::optional<T>& savedOld) {
    if (!incoming.has_value()) return false;
    bool changed = true;
    if constexpr (FieldComparable<T>::value) changed = !(target == *incoming);
    savedOld = target;
    target = *incoming;
    return changed;
}

template <class T>
void RestoreOptionalField(const std::optional<T>& savedOld, T& target) {
    if (savedOld.has_value()) target = *savedOld;
}

// 編集コマンドの基底インタフェース。
struct ICommand {
    virtual ~ICommand() = default;
    virtual void Apply(rs::world::TrackGraph& graph) = 0;
    virtual void Revert(rs::world::TrackGraph& graph) = 0;
    virtual void Apply(rs::world::SceneRegistry&) {}
    virtual void Revert(rs::world::SceneRegistry&) {}
    // 直前の Apply が graph を変えたか。false なら CommandStack は Undo に積まない。
    virtual bool Applied() const { return true; }
    // 直前の Apply で値が実際に変わったか。false なら CommandStack は取り消して積まない
    // (同じ値の再設定で Undo 履歴と未保存の印を増やさないため)。
    virtual bool Changed() const { return true; }
};

// ---- AddEdge ---------------------------------------------------------------
// edge を追加する。nodeA/nodeB が graph に未登録の場合はそれも合わせて追加し、
// Revert で edge と共に（このコマンドが新規作成した node のみ）削除する。
// 既存 node への接続時は、その node の links に本 edge への Link を追加/削除する。
class AddEdgeCommand : public ICommand {
public:
    // edge: guid/nodeA/nodeB/curve等を設定済みの完全な TrackEdge を渡す。
    // nodeAIsNew / nodeBIsNew: そのノードがこのコマンドで新規作成されるものか。
    // true の場合、渡された nodeATemplate/nodeBTemplate がそのまま node として
    // 追加される（既存 node の場合はテンプレートは無視、graph 側の既存値を使う）。
    // edge の curve/spline は渡さなくてよい(Apply が両端ノードの姿勢から作る)。
    // laidEndTangent: 敷設時の終端でのedge進行方向(水平でよい)。非nullなら nodeBTangentSign を
    // node.tangent との内積から決める。null なら渡された spline の末尾接線から決める。
    AddEdgeCommand(rs::world::TrackEdge edge, bool nodeAIsNew,
                   rs::world::TrackNode nodeATemplate, bool nodeBIsNew,
                   rs::world::TrackNode nodeBTemplate,
                   const rs::core::Vec3* laidEndTangent = nullptr);

    // Apply で形を作れるかの事前確認(グラフは変えない)。確定前に呼んで拒否に使う。
    // graph に無い既存ノードがある(別コマンドが後から作る)場合は確認できず true を返す。
    bool CanApply(const rs::world::TrackGraph& graph) const;

    // 形を作れなければ何も追加せず applied_=false のまま戻る(Revert も何もしない)。
    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return applied_; }

private:
    void ResolveNodeBSign(rs::world::TrackEdge& edge, const rs::world::TrackNode& nodeB) const;

    bool applied_ = false;
    bool hasLaidEndTangent_ = false;
    rs::core::Vec3 laidEndTangent_;
    rs::world::TrackEdge edge_;
    bool nodeAIsNew_;
    rs::world::TrackNode nodeATemplate_;
    bool nodeBIsNew_;
    rs::world::TrackNode nodeBTemplate_;
};

// ---- RemoveEdge -------------------------------------------------------------
// 既存 edge を削除する。Revert で完全復元できるよう Apply 前に必要な情報を
// コンストラクタで受け取っておく（呼び出し側が graph から現在値を渡す）。
class RemoveEdgeCommand : public ICommand {
public:
    explicit RemoveEdgeCommand(rs::world::Guid edgeGuid);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return hadSavedEdge_; }

private:
    rs::world::Guid edgeGuid_;
    // Apply() 実行時に graph から保存したスナップショット（Revert 用）。
    rs::world::TrackEdge savedEdge_;
    bool hadSavedEdge_ = false;

    // Apply() 実行時(RemoveLinkFromNode呼び出し前)の両端点nodeの完全な状態。
    // 孤立化して丸ごと削除された場合・リンクが残った場合のどちらであっても、
    // Revertではこのスナップショットをそのままgraph.nodesへ書き戻す
    // (AddLinkToNodeの面判定による再構築はしない。分岐点でUndo後に
    // links順序・activeSlotが変わり経路が変わる不整合を避けるため)。
    bool hadNodeA_ = false;
    rs::world::TrackNode savedNodeA_;
    bool hadNodeB_ = false;
    rs::world::TrackNode savedNodeB_;
    // 論理継ぎ目の接続側ノード(linkNodes)。両端ノードとは別に links から外すので保存する。
    std::vector<rs::world::TrackNode> savedLinkNodes_;
    // このedgeを railEdges に持つ番線があったときの、削除前の番線一覧。
    rs::world::TrackGraphReferenceSnapshot savedReferences_;
};

// ---- EraseOrphanNode ---------------------------------------------------------
// 接続edgeを持たない孤立nodeを1個削除する(RemoveSelectedNodeが、削除対象node
// 自体が最初から孤立していた場合に使う。RemoveEdgeCommandが引き起こす孤立化の
// 場合はRemoveEdgeCommand自身が処理するため、こちらは経由しない)。
class EraseOrphanNodeCommand : public ICommand {
public:
    explicit EraseOrphanNodeCommand(rs::world::Guid nodeGuid);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return hadNode_; }

private:
    rs::world::Guid nodeGuid_;
    rs::world::TrackNode savedNode_;
    bool hadNode_ = false;
};

// ---- MoveNode ---------------------------------------------------------------
// node の pos/dir/up を変更する（既存値は Apply 時に自動保存）。
class MoveNodeCommand : public ICommand {
public:
    MoveNodeCommand(rs::world::Guid nodeGuid, rs::core::Vec3 newPos,
                     rs::core::Vec3 newDir, rs::core::Vec3 newUp, bool keepEdgeSigns = false);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return hadOld_; }
    bool Changed() const override { return changed_; }

private:
    bool changed_ = false;
    rs::world::Guid nodeGuid_;
    rs::core::Vec3 newPos_, newDir_, newUp_;
    bool keepEdgeSigns_;  // true なら接線の向きが変わっても edge 端の符号を反転しない
    rs::core::Vec3 oldPos_, oldDir_, oldUp_;
    bool hadOld_ = false;
    // 再サンプルで書き換わる区間境界の保存。
    std::vector<rs::world::EdgeSectionsSnapshot> savedSections_;
    // 接線の向きが変わったときに反転する edge 端の符号の保存(Undo用)。
    std::vector<rs::world::EdgeTangentSignsSnapshot> savedSigns_;
};

// ---- SetEdgeProperty ---------------------------------------------------------
// edge の speedLimitKmh / cantA・cantB・cantMid / pluginSections /
// refit を変更する。未指定フィールドは std::nullopt で「変更しない」を表す。
// refit は緩和曲線の指定(Editor::SetSelectedEdgeTransition)で、変えたら両端ノードの姿勢から
// curve/spline を作り直す。curve は直接指定できない(形はノード姿勢から BuildEdgeShape が作る)。
struct EdgePropertyDelta {
    std::optional<float> speedLimitKmh;
    std::optional<float> cantA;
    std::optional<float> cantB;
    std::optional<float> cantMid;
    std::optional<float> cantTransitionRatio;
    // カント制御点列(TrackEdge::cantPoints)。空の列を指定すると制御点を消して台形/線形へ戻す。
    std::optional<std::vector<rs::world::CantControlPoint>> cantPoints;
    //カント自動モード(TrackEdge::cantAuto / cantAutoMax)。変えると隣の自動edgeも作り直す。
    std::optional<bool> cantAuto;
    std::optional<float> cantAutoMax;
    std::optional<rs::world::IntervalList<rs::world::PluginSet>> pluginSections;
    std::optional<rs::world::TrackEdge::Refit> refit;
    // 複線中心線(centerlineOnly)のtrackEdges更新用(実レール削除時の退化メンバ処理)。
    // 通常のedgeプロパティ編集UIからは使わない。
    std::optional<std::vector<rs::world::Guid>> trackEdges;
    // 複線memberEdgeの独立編集フラグ(materialize-on-touch)。
    // ノードドラッグ(EditorNodeDrag)が中心線memberを個別に動かした際にtrueへ設定する。
    std::optional<bool> detachedFromCenterline;
};

class SetEdgePropertyCommand : public ICommand {
public:
    SetEdgePropertyCommand(rs::world::Guid edgeGuid, EdgePropertyDelta delta);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return hadOld_; }
    bool Changed() const override { return changed_; }

private:
    bool changed_ = false;
    rs::world::Guid edgeGuid_;
    EdgePropertyDelta delta_;
    EdgePropertyDelta oldValues_; // Apply 時に保存した変更前の値
    bool hadOld_ = false;
    // cant/refit の変更は edge 長を変えることがあり、再サンプルで区間境界が書き換わる。
    // oldValues_ は delta_ に含まれるフィールドしか持たないので別途保存する。
    std::vector<rs::world::EdgeSectionsSnapshot> savedSections_;
};

// ---- SetPointLinks ------------------------------------------------------------
// node の pointThroatLink/pointActiveLink を変更する(分岐(ポイント)の初期化・
// 開通方向切替の両方で使う共通コマンド)。未指定フィールド(nullopt)は「変更しない」。
// MakePoint相当の初期化: 端点スナップでlinks==3になったnodeへ、既存2本(throat/active)
// を温存し新規分岐(links[2])は非開通のままにする(方式A)。
// 分岐(ポイント)の開通状態。TrackNode::activeSlot(面ごとの現在スロット)を
// そのまま差し替える。X字分岐(両面が分岐)では両面が独立に意味を持つため、
// Undo 用にも必ず両面まとめて保持する。
struct PointLinksDelta {
    std::array<int, 2> activeSlot{0, 0};
};

class SetPointLinksCommand : public ICommand {
public:
    SetPointLinksCommand(rs::world::Guid nodeGuid, PointLinksDelta delta);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;
    bool Applied() const override { return hadOld_; }
    bool Changed() const override { return changed_; }

private:
    bool changed_ = false;
    rs::world::Guid nodeGuid_;
    PointLinksDelta delta_;
    PointLinksDelta oldValues_;
    bool hadOld_ = false;
};

// ---- AddWarpLink -------------------------------------------------------------
// Scene間ワープリンクを1本追加する。
// warpリンクはTrackGraphではなくWarpLinkTable(Scene横断の独立テーブル)を
// 変更するため、Apply/Revertに渡されるTrackGraph&は使わない(ICommandのAPI形状を
// 満たすためだけの引数。呼び出し側はeditor.graphをそのまま渡せばよい)。
class AddWarpLinkCommand : public ICommand {
public:
    AddWarpLinkCommand(rs::world::WarpLinkTable& table, rs::world::WarpEndpoint a,
                       rs::world::WarpEndpoint b);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

private:
    rs::world::WarpLinkTable& table_;
    rs::world::WarpEndpoint a_;
    rs::world::WarpEndpoint b_;
};

// Scene間ワープリンクを1本削除する。AddWarpLinkCommandの逆操作であり、
// TrackGraphには依存しない。
class RemoveWarpLinkCommand : public ICommand {
public:
    RemoveWarpLinkCommand(rs::world::WarpLinkTable& table, rs::world::WarpEndpoint a,
                          rs::world::WarpEndpoint b);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

private:
    rs::world::WarpLinkTable& table_;
    rs::world::WarpEndpoint a_;
    rs::world::WarpEndpoint b_;
};

// ---- Composite --------------------------------------------------------------
// 複数コマンドを1つのUndo単位にまとめる。Applyは登録順、Revertは逆順。
class CompositeCommand : public ICommand {
public:
    void Add(std::unique_ptr<ICommand> cmd);
    bool Empty() const;
    void Apply(rs::world::TrackGraph& graph) override;   // 登録順に Apply
    void Revert(rs::world::TrackGraph& graph) override;  // 逆順に Revert
    void Apply(rs::world::SceneRegistry& registry) override;
    void Revert(rs::world::SceneRegistry& registry) override;
    // 子の適用に失敗したら、それまでに実行した子だけを逆順で取り消してfalseにする。
    bool Applied() const override;
    // 1つでも値を変えた子があれば true。
    bool Changed() const override;
private:
    std::vector<std::unique_ptr<ICommand>> cmds_;
    size_t appliedCount_ = 0;
    bool applied_ = false;
};

// ---- CommandStack ------------------------------------------------------------
// undo/redo 2本のスタック。新規コマンド実行(Execute)で redo スタックはクリアする。
class CommandStack {
public:
    // コマンドを実行し（Apply）、undo スタックへ積む。redo スタックはクリアする。
    // Applied() が false のコマンドは取り消して捨て、スタックは変えずに false を返す。
    bool Execute(rs::world::TrackGraph& graph, std::unique_ptr<ICommand> cmd);
    bool Execute(rs::world::TrackGraph& graph, rs::world::SceneRegistry& sceneRegistry,
                 std::unique_ptr<ICommand> cmd);

    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    void Undo(rs::world::TrackGraph& graph);
    void Undo(rs::world::TrackGraph& graph, rs::world::SceneRegistry& sceneRegistry);
    bool Redo(rs::world::TrackGraph& graph);
    bool Redo(rs::world::TrackGraph& graph, rs::world::SceneRegistry& sceneRegistry);

    size_t UndoCount() const { return undoStack_.size(); }
    size_t RedoCount() const { return redoStack_.size(); }

    // undo/redo双方を空にする。graphを丸ごと差し替える操作(.rs2読込・シーン切替等)の
    // 直後に呼ぶこと(コマンドは差し替え前のgraphを前提に構築されているため、
    // 差し替え後にUndo/Redoすると無関係なgraphへ適用されてしまう)。
    void Clear() { undoStack_.clear(); redoStack_.clear(); }

private:
    struct Entry {
        std::unique_ptr<ICommand> command;
        bool sceneCommand = false;
    };
    std::vector<Entry> undoStack_;
    std::vector<Entry> redoStack_;
};

} // namespace rs::editor
