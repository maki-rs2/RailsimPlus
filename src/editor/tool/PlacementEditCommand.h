// PlacementEditCommand.h - 実体化と配置編集を一つの Undo 操作にまとめるコマンド
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../../world/scene/MaterializeSnapshot.h"
#include "Command.h"
#include "EditorPlacementOverlayOps.h"

namespace rs::editor {

class PlacementEditCommand : public ICommand {
public:
    enum class Kind { Delete, Add, Move, Dematerialize, ConnectWire, DisconnectWire, Reanchor };

    struct Request {
        Kind kind = Kind::Delete;
        rs::world::Guid edge = rs::world::NilGuid();
        double s = 0.0;
        std::string folderUtf8;
        rs::world::Guid newEdge = rs::world::NilGuid();
        double newS = 0.0;
        bool keepUpright = false;
        float verticalOffsetM = 0.0f;
        float oldLateralOffsetM = 0.0f;
        float newLateralOffsetM = 0.0f;
        std::optional<int> newDir;
        std::string lineFolderUtf8;
        // ConnectWire / DisconnectWire: 結ぶ(切る)2つの接続点。架線プラグインはlineFolderUtf8。
        WirePointRef wirePointA;
        WirePointRef wirePointB;
    };

    PlacementEditCommand(rs::world::ManualPlacementOverlay& overlay,
                         std::vector<rs::world::MaterializeSnapshot> snapshots, Request request);

    // Applyしても何も変わらない要求か。呼び出し側はfalseならExecuteせずに捨てる。
    // no-opをコマンド化すると、動かさずにクリックしただけでUndo項目が積まれ、
    // revision更新でメッシュ全再構築まで走ってしまう。
    bool WouldChange(const rs::world::TrackGraph& graph) const;

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

private:
    rs::world::ManualPlacementOverlay& overlay_;
    std::vector<rs::world::MaterializeSnapshot> snapshots_;
    Request request_;
    EditorPlacementOverlayOps overlayOps_;
    rs::world::ManualPlacementOverlay savedOverlay_;
    std::optional<rs::world::IntervalList<rs::world::PluginSet>> savedEdgePluginSections_;
    std::vector<rs::world::IntervalList<rs::world::PluginSet>> savedSections_;
    std::vector<bool> appliedSnapshots_;
};

} // namespace rs::editor
