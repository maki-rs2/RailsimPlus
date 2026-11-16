//
// NodePropertyAction.h - 選択nodeプロパティのアプリサービス
//
#pragma once

#include <cstddef>
#include <vector>

#include "../../editor/tool/Editor.h"
#include "../../editor/tool/WarpLinkAuthoring.h"
#include "../../world/alignment/EdgeSplitMerge.h"

namespace rs::app {

struct LayoutSession;

struct NodePropertyViewModel {
    bool visible = false;
    float elevation = 0.0f;
    std::size_t linkCount = 0;
    bool hasWarpPending = false;
    // 選択nodeがワープ端点として確定済みか。trueなら相手のScene番号を持つ。
    bool hasWarpLink = false;
    std::size_t warpPartnerScene = 0;
    rs::world::EdgeMergeRefusal mergeRefusal = rs::world::EdgeMergeRefusal::None;
    // mergeRefusal==LayerMismatchかつ、それ以外の拒否理由が無い場合にtrue。
    // trueの場合、UI側は結合ボタンを有効のままにし、確認ダイアログを1つ挟んで
    // allowLayerMismatch=trueでのMergeEdgesを許可する。
    bool mergeBlockedOnlyByLayerMismatch = false;
    // mergeBlockedOnlyByLayerMismatch==trueのときのみ意味を持つ、結合対象2edgeのlayer
    // (ダイアログ表示用。それ以外は既定値のまま)。
    rs::world::Guid mergeEdgeFirstLayer = rs::world::NilGuid();
    rs::world::Guid mergeEdgeSecondLayer = rs::world::NilGuid();
};

class NodePropertyAction {
public:
    NodePropertyAction(rs::editor::Editor& editor, rs::editor::WarpLinkAuthoring& warpAuthoring,
                       LayoutSession& layoutSession);

    void BeginWarpPending();
    // 保留中のワープ端点を、選択nodeとの間で確定する(唯一の確定入口)。
    bool ConfirmWarpPending();
    // 保留中に2点目のnodeが選ばれていれば、その瞬間に確定する(毎フレーム呼ぶ)。
    // 保留が無い、選択が無い、選択が保留中の端点そのものなら何もしない。
    void AutoConfirmWarpPending();
    void CancelWarpPending();
    // 選択nodeのワープリンクを削除する(Undo可)。リンクが無ければ何もしない。
    void RemoveWarp();
    // allowLayerMismatch=trueの場合、layer不一致のみが拒否理由のケースをUI確認
    // ダイアログ経由で許可する。
    void MergeEdges(bool allowLayerMismatch = false);
    NodePropertyViewModel ViewModel();

private:
    rs::editor::Editor& editor_;
    rs::editor::WarpLinkAuthoring& warpAuthoring_;
    LayoutSession& layoutSession_;
};

} // namespace rs::app
