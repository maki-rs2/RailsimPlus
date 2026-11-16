#include "PluginReferenceDelete.h"

#include <algorithm>
#include <utility>

#include "../../editor/tool/Editor.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/scene/SceneRegistry.h"
#include "LayoutSession.h"

namespace rs::app {
namespace {

using Backups = std::vector<PluginDeleteLog::SceneBackup>;

bool IsSceneContainerSite(PluginReferenceSite site) {
    return site == PluginReferenceSite::Signal || site == PluginReferenceSite::PlacedIndividual ||
           site == PluginReferenceSite::WireLine;
}

// 削除の前に、各Sceneの信号・オーバーレイ・架線スパンの並びを丸ごと写す。
Backups SnapshotScenes(PluginReferenceContext& context) {
    Backups backups;
    const size_t count = SceneCountOf(context);
    for (size_t sceneIndex = 0; sceneIndex < count; ++sceneIndex) {
        PluginDeleteLog::SceneBackup backup;
        backup.sceneIndex = sceneIndex;
        if (const rs::world::TrackGraph* graph = SceneGraphFor(context, sceneIndex)) {
            backup.signals = graph->speedSignals;
        }
        if (const rs::world::ManualPlacementOverlay* overlay = SceneOverlayFor(context, sceneIndex)) {
            backup.poles = overlay->poles;
            backup.piers = overlay->piers;
            backup.lines = overlay->lines;
        }
        if (const std::vector<ImportedWireSpan>* spans = SceneWireSpansFor(context, sceneIndex)) {
            backup.wireSpans = *spans;
        }
        backups.push_back(std::move(backup));
    }
    return backups;
}

template <class T>
void DropIfUnchanged(std::optional<std::vector<T>>& saved, const std::vector<T>* current) {
    if (saved.has_value() && (current == nullptr || current->size() == saved->size())) saved.reset();
}

// 削除は要素を減らすだけなので、数が変わらなかった並びは控えから外す。
void KeepChangedOnly(PluginReferenceContext& context, Backups& backups) {
    for (PluginDeleteLog::SceneBackup& backup : backups) {
        const rs::world::TrackGraph* graph = SceneGraphFor(context, backup.sceneIndex);
        const rs::world::ManualPlacementOverlay* overlay = SceneOverlayFor(context, backup.sceneIndex);
        DropIfUnchanged(backup.signals, graph != nullptr ? &graph->speedSignals : nullptr);
        DropIfUnchanged(backup.poles, overlay != nullptr ? &overlay->poles : nullptr);
        DropIfUnchanged(backup.piers, overlay != nullptr ? &overlay->piers : nullptr);
        DropIfUnchanged(backup.lines, overlay != nullptr ? &overlay->lines : nullptr);
        DropIfUnchanged(backup.wireSpans, SceneWireSpansFor(context, backup.sceneIndex));
    }
    backups.erase(std::remove_if(backups.begin(), backups.end(),
                                 [](const PluginDeleteLog::SceneBackup& backup) {
                                     return !backup.signals && !backup.poles && !backup.piers && !backup.lines &&
                                            !backup.wireSpans;
                                 }),
                  backups.end());
}

// 個体の削除前の状態を控える。駅舎の個体は、持っていた番線も(番線の対応ごと)外して控える。
void RecordEntity(PluginReferenceContext& context, const PluginReferenceSlot& slot, PluginDeleteLog& log) {
    PluginDeleteLog::EntityBackup backup;
    backup.inRegistry = slot.entityInRegistry;
    backup.sceneIndex = slot.sceneIndex;
    backup.index = slot.listIndex;
    backup.entity = *slot.entity;
    log.entities.push_back(std::move(backup));

    if (slot.site != PluginReferenceSite::SceneryEntity || !slot.entityInRegistry) return;
    if (std::unique_ptr<rs::editor::ICommand> removal =
            context.editor.RemoveStationAnchorPlatform(slot.entity->guid)) {
        removal->Apply(context.editor.graph);
        log.anchors.push_back(std::move(removal));
    }
}

void Record(PluginReferenceContext& context, const PluginReferenceSlot& slot, PluginDeleteLog& log) {
    switch (slot.site) {
    case PluginReferenceSite::SceneryEntity:
    case PluginReferenceSite::CarEntity:
        RecordEntity(context, slot, log);
        break;
    case PluginReferenceSite::TrackSection:
        RecordEdgeBackup(log.edges, slot);
        break;
    case PluginReferenceSite::FormationCar:
        log.cars.push_back({slot.formationIndex, slot.listIndex, *slot.car});
        break;
    default:
        break;  // 信号・架線柱・橋脚・架線は、並びごと控える(SnapshotScenes)
    }
}

template <class T>
void RestoreList(std::vector<T>* target, const std::optional<std::vector<T>>& saved) {
    if (target != nullptr && saved.has_value()) *target = *saved;
}

}  // namespace

size_t DeletePluginReferences(PluginReferenceContext& context, PluginReferenceSite site,
                              const std::string& folderUtf8, PluginDeleteLog& log) {
    log = PluginDeleteLog{};
    if (site == PluginReferenceSite::Environment) return 0;
    const std::string key = PluginFolderKey(folderUtf8);
    Backups backups;
    if (IsSceneContainerSite(site)) backups = SnapshotScenes(context);

    size_t deleted = 0;
    VisitPluginReferences(context, [&](PluginReferenceSlot& slot) {
        if (slot.site != site || PluginFolderKey(slot.folder) != key) return false;
        Record(context, slot, log);
        slot.remove = true;
        ++deleted;
        return true;
    });

    KeepChangedOnly(context, backups);
    log.scenes = std::move(backups);
    return deleted;
}

void RestoreDeletedPluginReferences(PluginReferenceContext& context, const PluginDeleteLog& log) {
    // 番線は、消したときと逆の順に戻す(番線の並びの位置が元と同じになる)。
    for (auto it = log.anchors.rbegin(); it != log.anchors.rend(); ++it)
        (*it)->Revert(context.editor.graph);

    // 並びは前から順に、元の位置へ挿入し直せば、削除前と同じ並びになる。
    for (const PluginDeleteLog::EntityBackup& backup : log.entities) {
        if (backup.inRegistry) {
            context.sceneRegistry.RestoreEntityAt(backup.index, backup.entity);
        } else if (backup.sceneIndex < context.session.scenes.size()) {
            std::vector<rs::world::SceneEntity>& list = context.session.scenes[backup.sceneIndex].manualScenery;
            list.insert(list.begin() + static_cast<std::ptrdiff_t>(std::min(backup.index, list.size())),
                        backup.entity);
        }
    }
    for (const PluginDeleteLog::CarBackup& backup : log.cars) {
        context.selector.RestoreFormationCar(backup.formationIndex, backup.carIndex, backup.car);
    }

    RestoreEdgeBackups(context, log.edges);

    for (const PluginDeleteLog::SceneBackup& backup : log.scenes) {
        rs::world::TrackGraph* graph = SceneGraphFor(context, backup.sceneIndex);
        if (graph != nullptr && backup.signals.has_value()) {
            graph->speedSignals = *backup.signals;
            rs::world::RebuildSpeedSignalIndices(*graph);
        }
        rs::world::ManualPlacementOverlay* overlay = SceneOverlayFor(context, backup.sceneIndex);
        RestoreList(overlay != nullptr ? &overlay->poles : nullptr, backup.poles);
        RestoreList(overlay != nullptr ? &overlay->piers : nullptr, backup.piers);
        RestoreList(overlay != nullptr ? &overlay->lines : nullptr, backup.lines);
        RestoreList(SceneWireSpansFor(context, backup.sceneIndex), backup.wireSpans);
    }
}

}  // namespace rs::app
