//
// WarpLinkAuthoring.h - シーン間ワープリンクのオーサリングUI状態
//
// GUI(ImGui等)非依存の純粋なUI状態クラス。Scene A側nodeを「保留」してScene切替を
// 跨いで維持し、Scene B側nodeで「確定」した瞬間に初めて1個のAddWarpLinkCommandを
// CommandStackへExecuteする。保留状態自体はCommandStackには積まない(Scene切替時の
// CommandStack::Clear()を跨いで保留を維持するための設計)。
//
#pragma once

#include <optional>
#include <vector>

#include "../../world/alignment/WarpLinkTable.h"

namespace rs::world { struct TrackGraph; }

namespace rs::editor {

class Editor;

// warp端点オーサリングの一時UI状態。
// CommandStackには積まない(Scene切替のCommandStack::Clear()を跨いで保留を
// 維持するための設計)。
class WarpLinkAuthoring {
public:
    // Scene Aでnodeを保留状態にする。既に保留中なら選び直し(単純に上書き)。
    void BeginPending(size_t sceneIndex, const rs::world::Guid& node);

    // 保留を解除する。
    void CancelPending();

    // Sceneの並べ替え・削除(oldToNew[旧]=新)に合わせて、保留中の端点のSceneを付け替える。
    // 端点のSceneが消える(kRemovedScene)なら、保留を解除する。
    void RemapScenes(const std::vector<size_t>& oldToNew);

    bool HasPending() const;
    std::optional<rs::world::WarpEndpoint> Pending() const;

    // 保留中の端点と(sceneIndex, node)を結び、1個のAddWarpLinkCommandとして
    // Editorの入口で実行する。保留が無い場合、または(sceneIndex, node)が保留中の
    // 端点と完全に一致する場合(自己リンクになるため無意味)は何もせずfalseを返す。
    // 成功時は保留を解除してtrueを返す。
    bool ConfirmPending(size_t sceneIndex, const rs::world::Guid& node, Editor& editor,
                        rs::world::WarpLinkTable& warpTable);

private:
    std::optional<rs::world::WarpEndpoint> pending_;
};

} // namespace rs::editor
