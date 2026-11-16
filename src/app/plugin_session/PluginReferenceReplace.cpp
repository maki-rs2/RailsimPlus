#include "PluginReferenceReplace.h"

#include "../../plugins/schema/PluginTypeTraits.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "../../editor/tool/Editor.h"
#include "LayoutSession.h"

namespace rs::app {
namespace {

using rs::legacy_pi::PluginType;

// 場所ごとに置ける種類。信号は駅舎と同じ種類のプラグインを使い、架線柱と橋脚は同じ場所にまとまっている。
struct SiteTypes {
    PluginReferenceSite site;
    std::array<PluginType, 6> types;  // 余りはUnknownで埋める
};

constexpr PluginType kNone = PluginType::Unknown;
constexpr std::array<SiteTypes, 6> kSiteTypes = {{
    {PluginReferenceSite::CarEntity, {PluginType::Train, kNone, kNone, kNone, kNone, kNone}},
    {PluginReferenceSite::Signal, {PluginType::Station, kNone, kNone, kNone, kNone, kNone}},
    {PluginReferenceSite::PlacedIndividual, {PluginType::Pole, PluginType::Pier, kNone, kNone, kNone, kNone}},
    {PluginReferenceSite::WireLine, {PluginType::Line, kNone, kNone, kNone, kNone, kNone}},
    {PluginReferenceSite::FormationCar, {PluginType::Train, kNone, kNone, kNone, kNone, kNone}},
    {PluginReferenceSite::Environment, {PluginType::Env, kNone, kNone, kNone, kNone, kNone}},
}};

void RecordBefore(PluginReplaceLog& log, const PluginReferenceSlot& slot, size_t ordinal) {
    if (slot.site == PluginReferenceSite::TrackSection) {
        RecordEdgeBackup(log.edges, slot);
        return;
    }
    if (slot.site == PluginReferenceSite::Environment) {
        log.environments.push_back({slot.sceneIndex, slot.scene->envPlugin, slot.scene->envFolderUtf8});
        return;
    }
    PluginReplaceLog::Item item;
    item.ordinal = ordinal;
    item.folderUtf8 = slot.folder;
    if (slot.switchOptions != nullptr) item.switchOptions = *slot.switchOptions;
    if (slot.car != nullptr) item.car = *slot.car;
    log.items.push_back(std::move(item));
}

void WriteReplacement(PluginReferenceSlot& slot, const PluginReplacePlan& plan) {
    slot.folder = plan.toFolderUtf8;
    if (slot.switchOptions != nullptr) slot.switchOptions->clear();
    if (slot.car != nullptr && plan.car.has_value()) {
        // 向きは編成の並びの一部なので引き継ぐ。寸法とスイッチ数は差し替え先のもの。
        slot.car->displayName = plan.car->displayName;
        slot.car->frontLimit = plan.car->frontLimit;
        slot.car->tailLimit = plan.car->tailLimit;
        slot.car->doorClosingTime = plan.car->doorClosingTime;
        slot.car->pluginSwitchCount = plan.car->pluginSwitchCount;
        slot.car->unresolved = false;
    }
}

}  // namespace

bool SiteAcceptsPluginType(PluginReferenceSite site, PluginType type) {
    if (type == PluginType::Unknown) return false;
    const rs::legacy_pi::PluginTypeTraits& traits = rs::legacy_pi::GetPluginTypeTraits(type);
    if (site == PluginReferenceSite::SceneryEntity) return traits.scenery;
    if (site == PluginReferenceSite::TrackSection) return traits.trackPlugin;
    for (const SiteTypes& entry : kSiteTypes) {
        if (entry.site != site) continue;
        return std::find(entry.types.begin(), entry.types.end(), type) != entry.types.end();
    }
    return false;
}

void RecordEdgeBackup(std::vector<PluginReplaceLog::EdgeBackup>& edges, const PluginReferenceSlot& slot) {
    // 同じedgeの別表記は続けて訪ねるので、直前の控えと比べるだけで重複を避けられる。
    if (!edges.empty() && edges.back().sceneIndex == slot.sceneIndex && edges.back().edge == slot.edge->guid) {
        return;
    }
    edges.push_back({slot.sceneIndex, slot.edge->guid, slot.edge->pluginSections});
}

void RestoreEdgeBackups(PluginReferenceContext& context, const std::vector<PluginReplaceLog::EdgeBackup>& edges) {
    for (const PluginReplaceLog::EdgeBackup& backup : edges) {
        rs::world::TrackGraph* graph = SceneGraphFor(context, backup.sceneIndex);
        if (graph == nullptr) continue;
        const auto found = graph->edges.find(backup.edge);
        if (found != graph->edges.end()) found->second.pluginSections = backup.sections;
    }
}

size_t ReplacePluginReferences(PluginReferenceContext& context, const PluginReplacePlan& plan,
                               PluginReplaceLog* log) {
    const std::string fromKey = PluginFolderKey(plan.fromFolderUtf8);
    if (log != nullptr) {
        *log = PluginReplaceLog{};
        log->appliedKey = PluginFolderKey(plan.toFolderUtf8);
    }
    size_t ordinal = 0;
    size_t replaced = 0;
    VisitPluginReferences(context, [&](PluginReferenceSlot& slot) {
        const size_t current = ordinal++;
        if (slot.site != plan.site || PluginFolderKey(slot.folder) != fromKey) return false;
        if (log != nullptr) RecordBefore(*log, slot, current);
        WriteReplacement(slot, plan);
        ++replaced;
        return true;
    });
    return replaced;
}

void RestorePluginReferences(PluginReferenceContext& context, const PluginReplaceLog& log) {
    for (const PluginReplaceLog::EnvBackup& backup : log.environments) {
        if (backup.sceneIndex >= context.session.scenes.size()) continue;
        LayoutSceneCache& scene = context.session.scenes[backup.sceneIndex];
        scene.envPlugin = backup.envPlugin;
        scene.envFolderUtf8 = backup.envFolderUtf8;
    }
    RestoreEdgeBackups(context, log.edges);

    // ここまでで構造は差し替え前と同じになっているので、通し番号が差し替え前の訪問と一致する。
    size_t ordinal = 0;
    size_t cursor = 0;
    VisitPluginReferences(context, [&](PluginReferenceSlot& slot) {
        const size_t current = ordinal++;
        if (cursor >= log.items.size() || log.items[cursor].ordinal != current) return false;
        const PluginReplaceLog::Item& item = log.items[cursor++];
        if (PluginFolderKey(slot.folder) != log.appliedKey) {
            spdlog::warn("[plugin_replace] reference {} no longer holds the replaced plugin; left as is", current);
            return false;
        }
        slot.folder = item.folderUtf8;
        if (item.switchOptions.has_value() && slot.switchOptions != nullptr) {
            *slot.switchOptions = *item.switchOptions;
        }
        if (item.car.has_value() && slot.car != nullptr) *slot.car = *item.car;
        return true;
    });
}

}  // namespace rs::app
