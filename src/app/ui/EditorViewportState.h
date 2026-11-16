#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "../../editor/tool/Editor.h"
#include "../../render/mesh/Mesh.h"
#include "ViewMode.h"

namespace rs::app {

// 解決済みパスごとにロード結果を共有する。失敗も空メッシュとして保持する。
using HangerModelCache = std::unordered_map<std::string, rs::render::Mesh>;

struct EditorModeSignature {
    ViewMode viewMode;
    rs::editor::ToolMode toolMode;
    bool placementAddMode;
    bool stationAnchorMode;
    bool sectionSplitMode;
    bool wireEditMode;
    bool sceneryPlacementMode;

    bool operator==(const EditorModeSignature&) const = default;
};

// 画面ごとの保留操作と描画用キャッシュは、シーンの差し替えをまたいで持ち越さない。
struct EditorViewportState {
public:
    void Synchronize(uint64_t sceneGeneration, uint64_t pluginGeneration);
    bool UpdateMode(const EditorModeSignature& signature);

    rs::world::Guid pendingEdge = rs::world::NilGuid();
    std::string pendingDematerialize;
    HangerModelCache hangerModels;

private:
    std::optional<uint64_t> sceneGeneration_;
    std::optional<uint64_t> pluginGeneration_;
    std::optional<EditorModeSignature> lastModeSignature_;
};

} // namespace rs::app
