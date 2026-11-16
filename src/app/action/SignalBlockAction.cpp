#include "SignalBlockAction.h"

#include "../../world/alignment/SpeedSignalRegistry.h"

#include <algorithm>
#include <optional>
#include <string>
#include <memory>
#include <utility>

#include <spdlog/spdlog.h>

#include "../../editor/tool/BlockCommand.h"
#include "../workspace/RailLabelPresentation.h"

namespace rs::app {
namespace {

rs::world::Guid OwnerBlockToPlace(std::span<const rs::editor::SpeedSignalOwnerCandidate> candidates) {
    return candidates.size() == 1 ? candidates.front().block : rs::world::NilGuid();
}

std::string NextBlockName(const rs::world::TrackGraph& graph) {
    size_t candidate = 1;
    for (;;) {
        const std::string name = "閉塞 " + std::to_string(candidate);
        const bool used = std::any_of(graph.blocks.begin(), graph.blocks.end(),
                                      [&name](const rs::world::Block& existing) {
                                          return existing.name == name;
                                      });
        if (!used) return name;
        ++candidate;
    }
}

} // namespace

const char* SpeedSignalPlacementText(rs::world::SpeedSignalPlacementError error) {
    switch (error) {
    case rs::world::SpeedSignalPlacementError::None:
        return "";
    case rs::world::SpeedSignalPlacementError::NoBlock:
        return "この位置を含む閉塞がありません";
    case rs::world::SpeedSignalPlacementError::SameDirectionExists:
        return "この閉塞には同じ向きの信号が既にあります";
    case rs::world::SpeedSignalPlacementError::BlockFull:
        return "この閉塞の信号は上下で2個までです";
    }
    return "";
}

std::string SignalAspectDisplayText(const rs::sim::SignalAspectRuntime& runtime,
                                    const rs::world::SpeedSignal& signal) {
    const rs::sim::SignalDisplay display = rs::sim::ResolveSignalDisplay(&runtime, signal);
    if (!display.speedKmh.has_value()) return "現示: 制限なし";
    const std::string speedText = "(" + std::to_string(*display.speedKmh) + " km/h)";
    if (!display.stage.has_value()) return "現示: 最上段" + speedText;
    // 強制Rの切り替えは次のtickで現示へ反映されるので、速度は0と決めて出す。
    if (runtime.IsForcedRed(signal.guid)) return "現示: 強制R(0 km/h)";
    const rs::sim::SignalAspectState* state = runtime.Find(signal.guid);
    if (state != nullptr && !state->occupants.empty()) return "現示: 在線中" + speedText;
    return "現示: 段" + std::to_string(*display.stage) + speedText;
}

SignalBlockAction::SignalBlockAction(rs::editor::Editor& editor, ISignalSidecarSource& sidecarSource,
                                     ISignalAspectSource& aspectSource)
    : editor_(editor), sidecarSource_(sidecarSource), aspectSource_(aspectSource) {}

bool SignalBlockAction::CanCreateBlockFromSelection() const {
    return editor_.selectedBlock == rs::world::NilGuid() && editor_.SelectedEdgeCount() > 0;
}

bool SignalBlockAction::BlockCreateAnchor(rs::world::Guid& outEdge, double& outS) const {
    if (!CanCreateBlockFromSelection()) return false;
    for (const rs::world::Guid& edge : editor_.SelectedEdges()) {
        const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator it =
            editor_.graph.edges.find(edge);
        // 複線中心線は閉塞に入らない(CreateBlockFromSelectionと同じ)。
        if (it == editor_.graph.edges.end() || rs::world::IsCenterlineOnlyEdge(it->second)) continue;
        outEdge = edge;
        outS = 0.5 * rs::world::EdgeLength(it->second);
        return true;
    }
    return false;
}

bool SignalBlockAction::CreateBlockFromSelection() {
    if (!CanCreateBlockFromSelection()) return false;
    rs::world::Block block;
    block.guid = rs::world::GenerateGuid();
    block.name = NextBlockName(editor_.graph);
    for (const rs::world::Guid& edge : editor_.SelectedEdges()) {
        const std::unordered_map<rs::world::Guid, rs::world::TrackEdge, rs::world::GuidHash>::const_iterator it =
            editor_.graph.edges.find(edge);
        // 複線中心線は実体の走行面を持たないので閉塞に入れない。
        if (it != editor_.graph.edges.end() && !rs::world::IsCenterlineOnlyEdge(it->second)) {
            block.edges.push_back(edge);
        }
    }
    if (block.edges.empty()) return false;
    editor_.ExecutePlatformCommand(std::make_unique<rs::editor::AddBlockCommand>(block));
    // 作った閉塞は選ばない。選んだままだと次の「閉塞にする」が押せず、続けて閉塞を作れない。
    editor_.ClearSelectedEdge();
    return true;
}

void SignalBlockAction::SelectBlock(rs::world::Guid blockGuid) {
    // 選んでいる閉塞をもう一度選ぶと選択を外す(どの閉塞も選ばない状態へ戻り、レールを選んで新しく作れる)。
    editor_.selectedBlock = editor_.selectedBlock == blockGuid ? rs::world::NilGuid() : blockGuid;
}

void SignalBlockAction::RenameBlock(rs::world::Guid blockGuid, const std::string& name) {
    if (!workspace::CanCommitBlockName(rs::world::FindBlock(editor_.graph, blockGuid), name)) return;
    rs::editor::BlockPropertyDelta delta;
    delta.name = name;
    editor_.ExecutePlatformCommand(
        std::make_unique<rs::editor::SetBlockPropertyCommand>(blockGuid, std::move(delta)));
}

void SignalBlockAction::RemoveBlock(rs::world::Guid blockGuid) {
    // 閉塞の信号は担当の閉塞を失うので、閉塞と一緒に消す(1回のUndoでまとめて戻る)。
    std::unique_ptr<rs::editor::CompositeCommand> composite =
        std::make_unique<rs::editor::CompositeCommand>();
    const rs::world::Block* block = rs::world::FindBlock(editor_.graph, blockGuid);
    if (block != nullptr) {
        for (const rs::world::SpeedSignal* signal : rs::world::SpeedSignalsOfBlock(editor_.graph, *block)) {
            composite->Add(std::make_unique<rs::editor::RemoveSpeedSignalCommand>(signal->guid));
        }
    }
    composite->Add(std::make_unique<rs::editor::RemoveBlockCommand>(blockGuid));
    editor_.ExecutePlatformCommand(std::move(composite));
    editor_.ClearSelectedBlock();
}

void SignalBlockAction::Disarm() {
    editor_.CancelSpeedSignalPending();
    editor_.signalBlockArmedAction = rs::editor::SignalBlockArmedAction::None;
}

std::string SignalBlockAction::SelectSignalPlugin(const std::string& folderUtf8) {
    const std::string error = ValidateSignalSidecar(sidecarSource_.Sidecar(folderUtf8));
    editor_.signalPluginError = error;
    if (error.empty()) {
        editor_.signalPluginFolder = folderUtf8;
        spdlog::info("[signal] 信号プラグインを選びました: {}", folderUtf8);
    } else {
        spdlog::warn("[signal] 信号プラグインを選べません: {} ({})", folderUtf8, error);
    }
    return error;
}


const char* SpeedSignalKindText(rs::world::SpeedSignalKind kind) {
    switch (kind) {
    case rs::world::SpeedSignalKind::Main: return "主信号";
    case rs::world::SpeedSignalKind::Relay: return "中継信号";
    case rs::world::SpeedSignalKind::Distant: return "遠方信号";
    case rs::world::SpeedSignalKind::Shunting: return "入換信号";
    }
    return "";
}

std::vector<rs::editor::SpeedSignalOwnerCandidate> SpeedSignalOwnerCandidates(
    const rs::world::TrackGraph& graph, const rs::world::Guid& edge, double s, int direction,
    rs::world::SpeedSignalKind kind, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    std::vector<rs::editor::SpeedSignalOwnerCandidate> candidates;
    if (!rs::world::IsBlockBoundarySignal(kind)) return candidates;
    for (const rs::world::Block* block : rs::world::FindBlocksOnEdge(graph, edge)) {
        candidates.push_back({block->guid, rs::world::ValidateSpeedSignalPlacement(
            graph, edge, s, direction, block->guid, rs::world::NilGuid(), warpLinks, sceneIndex, kind)});
    }
    return candidates;
}

void SignalBlockAction::ArmPlaceSpeedSignal() {
    if (editor_.signalPluginFolder.empty()) return;
    editor_.CancelSpeedSignalPending();
    editor_.signalBlockArmedAction = rs::editor::SignalBlockArmedAction::PlaceSpeedSignal;
    editor_.speedSignalHover = {};
}

void SignalBlockAction::UpdateSpeedSignalHover(const rs::world::Guid& edge, double s, bool reverse,
    const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    rs::editor::SpeedSignalHover hover;
    if (editor_.speedSignalPending) {
        hover = *editor_.speedSignalPending;
    } else if (edge != rs::world::NilGuid() && !editor_.signalPluginFolder.empty()) {
        hover.hasTarget = true;
        hover.edge = edge;
        hover.s = s;
        hover.direction = editor_.ResolvePlaceTravelDirection(edge, reverse, warpLinks, sceneIndex);
    }
    if (hover.hasTarget) {
        hover.candidates = SpeedSignalOwnerCandidates(editor_.graph, hover.edge, hover.s, hover.direction,
            editor_.signalKindToPlace, warpLinks, sceneIndex);
        hover.block = OwnerBlockToPlace(hover.candidates);
        if (hover.candidates.empty()) {
            hover.error = rs::world::ValidateSpeedSignalPlacement(editor_.graph, hover.edge, hover.s,
                hover.direction, hover.block, rs::world::NilGuid(), warpLinks, sceneIndex, editor_.signalKindToPlace);
        } else {
            const auto placeable = std::find_if(hover.candidates.begin(), hover.candidates.end(),
                [](const rs::editor::SpeedSignalOwnerCandidate& candidate) {
                    return candidate.error == rs::world::SpeedSignalPlacementError::None;
                });
            hover.error = placeable != hover.candidates.end() ? placeable->error : hover.candidates.front().error;
        }
        if (editor_.speedSignalPending) editor_.speedSignalPending = hover;
    }
    editor_.speedSignalHover = std::move(hover);
}

SignalPlaceOutcome SignalBlockAction::BeginSpeedSignalPlacement(const rs::world::Guid& edge, double s,
    bool reverse, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    if (editor_.signalBlockArmedAction != rs::editor::SignalBlockArmedAction::PlaceSpeedSignal) return {};
    editor_.CancelSpeedSignalPending();
    UpdateSpeedSignalHover(edge, s, reverse, warpLinks, sceneIndex);
    const rs::editor::SpeedSignalHover hover = editor_.speedSignalHover;
    if (!hover.hasTarget) return {};
    if (hover.candidates.size() > 1) {
        editor_.speedSignalPending = hover;
        return {};
    }
    return PlaceSpeedSignalAt(hover.edge, hover.s, hover.block, hover.direction, warpLinks, sceneIndex);
}

SignalPlaceOutcome SignalBlockAction::ChooseSpeedSignalOwner(const rs::world::Guid& block,
    const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    if (!editor_.speedSignalPending ||
        editor_.signalBlockArmedAction != rs::editor::SignalBlockArmedAction::PlaceSpeedSignal) return {};
    const rs::editor::SpeedSignalHover pending = *editor_.speedSignalPending;
    const auto candidate = std::find_if(pending.candidates.begin(), pending.candidates.end(),
        [&block](const rs::editor::SpeedSignalOwnerCandidate& value) { return value.block == block; });
    if (candidate == pending.candidates.end()) return {};
    return PlaceSpeedSignalAt(pending.edge, pending.s, block, pending.direction, warpLinks, sceneIndex);
}

SignalPlaceOutcome SignalBlockAction::PlaceSpeedSignalAt(const rs::world::Guid& edge, double s,
    const rs::world::Guid& block, int direction, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
    SignalPlaceOutcome outcome;
    if (editor_.signalPluginFolder.empty()) {
        outcome.pluginNotSelected = true;
        return outcome;
    }
    outcome.error = rs::world::ValidateSpeedSignalPlacement(editor_.graph, edge, s, direction, block,
        rs::world::NilGuid(), warpLinks, sceneIndex, editor_.signalKindToPlace);
    if (outcome.error != rs::world::SpeedSignalPlacementError::None) return outcome;
    rs::world::SpeedSignal signal;
    signal.kind = editor_.signalKindToPlace;
    signal.block = rs::world::IsBlockBoundarySignal(signal.kind) ? block : rs::world::NilGuid();
    signal.guid = rs::world::GenerateGuid();
    signal.edge = edge;
    signal.s = s;
    signal.direction = direction < 0 ? -1 : +1;
    signal.stages = SignalStagesFromSidecar(sidecarSource_.Sidecar(editor_.signalPluginFolder));
    signal.pluginFolderUtf8 = editor_.signalPluginFolder;
    if (editor_.ExecutePlatformCommand(std::make_unique<rs::editor::AddSpeedSignalCommand>(signal)))
        editor_.NotePlaced();
    editor_.lastPlacedTravel = rs::editor::PlacedTravel{edge, signal.direction};
    // 連続配置のときは配置待ちのまま、仮止めとホバーだけ片づける。
    if (editor_.signalPlacementContinuous) {
        editor_.CancelSpeedSignalPending();
        editor_.speedSignalHover = {};
    } else {
        Disarm();
    }
    outcome.placed = true;
    return outcome;
}

SignalBlockSignalRowViewModel SignalBlockAction::SignalRow(const rs::world::SpeedSignal& signal) const {
    SignalBlockSignalRowViewModel row;
    row.guid = signal.guid;
    row.direction = signal.direction;
    row.kind = signal.kind;
    row.pluginName = SignalPluginDisplayName(signal.pluginFolderUtf8);
    for (const rs::world::SignalAspectStage& stage : signal.stages) {
        row.stages.push_back({stage.speedKmh, stage.sidecarIndex});
    }
    if (!signal.pluginFolderUtf8.empty()) {
        row.addableSpeeds =
            SignalAddableSpeeds(sidecarSource_.Sidecar(signal.pluginFolderUtf8), signal.stages);
    }
    row.forcedRed = aspectSource_.Aspects().IsForcedRed(signal.guid);
    row.entryPermitted = aspectSource_.Aspects().IsEntryPermitted(signal.guid);
    row.aspectText = SignalAspectDisplayText(aspectSource_.Aspects(), signal);
    return row;
}

SignalBlockViewModel SignalBlockAction::ViewModel() const {
    SignalBlockViewModel viewModel;
    if (editor_.speedSignalPending) {
        const rs::editor::SpeedSignalHover& pending = *editor_.speedSignalPending;
        viewModel.candidateAnchor = workspace::RailLabelAnchor{pending.edge, pending.s};
        for (const rs::editor::SpeedSignalOwnerCandidate& candidate : pending.candidates) {
            viewModel.ownerCandidates.push_back({candidate.block, SpeedSignalPlacementText(candidate.error)});
        }
    }
    viewModel.armedAction = editor_.signalBlockArmedAction;
    viewModel.selectedEdgeCount = editor_.SelectedEdgeCount();
    viewModel.canCreateBlockFromSelection = CanCreateBlockFromSelection();
    const rs::world::Block* block = rs::world::FindBlock(editor_.graph, editor_.selectedBlock);
    if (block != nullptr) {
        viewModel.selectedBlock.visible = true;
        viewModel.selectedBlock.edgeCount = block->edges.size();
        for (const rs::world::SpeedSignal* signal :
             rs::world::SpeedSignalsOfBlock(editor_.graph, *block)) {
            viewModel.selectedBlock.signals.push_back(SignalRow(*signal));
        }
    }
    for (const rs::world::SpeedSignal& signal : editor_.graph.speedSignals) {
        if (!rs::world::IsBlockBoundarySignal(signal.kind)) {
            viewModel.displayOnlySignals.push_back(SignalRow(signal));
        }
    }
    viewModel.signalPluginFolder = editor_.signalPluginFolder;
    viewModel.signalPluginName = SignalPluginDisplayName(editor_.signalPluginFolder);
    viewModel.signalPluginError = editor_.signalPluginError;
    const rs::editor::SpeedSignalHover& hover = editor_.speedSignalHover;
    if (editor_.signalBlockArmedAction == rs::editor::SignalBlockArmedAction::PlaceSpeedSignal) {
        if (editor_.signalPluginFolder.empty()) {
            viewModel.placeBlockedMessage = kSignalPluginNotSelectedText;
        } else if (hover.hasTarget) {
            viewModel.placeBlockedMessage = SpeedSignalPlacementText(hover.error);
        }
    }
    return viewModel;
}

} // namespace rs::app
