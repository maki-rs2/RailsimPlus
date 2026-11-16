//
// DiaPanelActionResolve.h - ダイヤの左タブの操作が、表示値の添字から実際の番線・層・要素を引き直す共通処理
//
#pragma once

#include <optional>
#include <utility>

#include "DiaPanelAction.h"
#include "PlatformRoutineAction.h"
#include "../workspace/DiaPanelPresentation.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/PlatformRegistry.h"

namespace rs::app::dia_panel_action {

struct ResolvedTarget {
    const workspace::DiaPanelTarget* target = nullptr;
    rs::world::PlatformStopInfo* platform = nullptr;
};

// request.targetIndex_ が指す番線を、操作できる形で返す。アクティブでないシーンの番線、
// すでに消えた番線は返さない(操作しない)。
std::optional<ResolvedTarget> ResolveActiveTarget(const Refs& refs, const Request& request);

// 表示値の層を、番線の実際の層(共通・タグ・編成)へ、層のキーで引き直す。
std::optional<RoutineLayerRef> ResolveLayerRef(const rs::world::PlatformStopInfo& platform,
                                               const workspace::DiaPanelLayer& layer);

// 引き直した層の実際の要素列。画面の要素数と合わないとき(画面が古いとき)は nullptr。
const std::vector<rs::world::PlatformRoutineElement>* LiveRoutine(const rs::world::PlatformStopInfo& platform,
                                                                  const RoutineLayerRef& ref,
                                                                  const workspace::DiaPanelLayer& layer);

// layerIndex の層について、PlatformRoutineAction・層の参照・実際の要素列を引き直して f に渡す。
// 引き直せなければ何もしない。
template <typename Function>
void WithLayer(const Refs& refs, const Request& request, int layerIndex, Function&& function) {
    const std::optional<ResolvedTarget> resolved = ResolveActiveTarget(refs, request);
    if (!resolved || layerIndex < 0 || static_cast<std::size_t>(layerIndex) >= resolved->target->layers_.size()) return;
    const workspace::DiaPanelLayer& layer = resolved->target->layers_[static_cast<std::size_t>(layerIndex)];
    const std::optional<RoutineLayerRef> ref = ResolveLayerRef(*resolved->platform, layer);
    if (!ref) return;
    const std::vector<rs::world::PlatformRoutineElement>* routine = LiveRoutine(*resolved->platform, *ref, layer);
    if (routine == nullptr) return;
    PlatformRoutineAction action(refs.editor, resolved->platform->id, {});
    std::forward<Function>(function)(action, *ref, *routine, layer);
}

} // namespace rs::app::dia_panel_action
