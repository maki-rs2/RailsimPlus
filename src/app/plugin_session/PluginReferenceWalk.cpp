#include "PluginReferenceWalk.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <utility>

#include "../../editor/tool/Editor.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/Rs2Import.h"
#include "../../world/alignment/SpeedSignalRegistry.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "../../world/scene/SceneEntity.h"
#include "../../world/scene/SceneRegistry.h"
#include "ActiveEnv.h"
#include "LayoutSession.h"
#include "PluginSelector.h"
#include "Rs2SceneApplier.h"

namespace rs::app {

// edgeの区間に含まれるfromを、toに置き換える。toがnullptrなら、置き換えずに取り除く。
// 区間の境界は変えない。同じ区間にtoがすでにあれば、fromを取り除くだけにして重複させない。
void EditFolderInEdge(rs::world::TrackEdge& edge, const std::string& from, const std::string* to) {
    using Spans = std::vector<rs::world::IntervalList<rs::world::PluginSet>::Span>;
    Spans spans = edge.pluginSections.Spans();
    const std::string toKey = to != nullptr ? PluginFolderKey(*to) : std::string();
    for (rs::world::IntervalList<rs::world::PluginSet>::Span& span : spans) {
        std::vector<std::string>& plugins = span.value.plugins;
        bool hasTarget = to != nullptr && std::any_of(plugins.begin(), plugins.end(), [&](const std::string& folder) {
            return folder != from && PluginFolderKey(folder) == toKey;
        });
        for (std::vector<std::string>::iterator it = plugins.begin(); it != plugins.end();) {
            if (*it != from) {
                ++it;
            } else if (to == nullptr || hasTarget) {
                it = plugins.erase(it);
            } else {
                *it = *to;
                hasTarget = true;
                ++it;
            }
        }
    }
    edge.pluginSections = rs::world::IntervalList<rs::world::PluginSet>::FromSpans(std::move(spans));
}

namespace {

// 環境の参照先。解決できたときはそのフォルダ。できなかったときは、ルート候補から
// `<ルート>/Env/<名前>`を組む(存在する最初のルート、無ければ先頭のルート)。ルート候補が無ければ名前。
std::string EnvironmentFolder(const LayoutSceneCache& scene, const std::vector<std::string>* roots) {
    if (!scene.envFolderUtf8.empty()) return scene.envFolderUtf8;
    if (roots == nullptr || roots->empty()) return scene.envPlugin;
    const std::string existing = ResolveEnvPluginFolder(*roots, scene.envPlugin);
    if (!existing.empty()) return existing;
    return rs::legacy_layout::ResolveTrackPluginFolder(roots->front(), "Env", scene.envPlugin);
}

// 訪問先の素性(PluginReferenceSlotのentity以降の欄に写す)。
struct SlotOrigin {
    const rs::world::SceneEntity* entity = nullptr;
    size_t listIndex = 0;
    bool inRegistry = false;
};

template <bool Const>
class Walker {
public:
    using Context = BasicPluginReferenceContext<Const>;
    template <class T>
    using Ref = typename Context::template Ref<T>;

    Walker(Context& context, const PluginReferenceVisitor& visitor) : ctx_(context), visitor_(visitor) {}

    void Run() {
        VisitRegistry();
        // アクティブSceneはライブ状態、ほかのSceneはキャッシュを使う(保存と同じ見方)。
        const size_t active = ctx_.session.activeIndex;
        VisitGraph(ctx_.editor.graph, active);
        VisitOverlay(ctx_.editor.manualOverlay, active);
        for (size_t index = 0; index < ctx_.session.scenes.size(); ++index) {
            Ref<LayoutSceneCache>& scene = ctx_.session.scenes[index];
            const bool isActive = index == active;
            if (!isActive) {
                VisitGraph(scene.graph, index);
                VisitOverlay(scene.overlay, index);
                EraseAt(scene.manualScenery,
                        VisitList(scene.manualScenery, PluginReferenceSite::SceneryEntity, index, kEntityFolder,
                                  kEntitySwitches));
            }
            if (!isActive || ctx_.importedWireSpans == nullptr) VisitWireSpans(scene.wireSpans, index);
            VisitEnvironment(scene, index);
        }
        if (ctx_.importedWireSpans != nullptr) VisitWireSpans(*ctx_.importedWireSpans, active);
        VisitFormations();
    }

private:
    // 場所の文字列(とあればスイッチ値)を作業コピーに写して訪問し、書き換えられたら戻す。
    // 訪問者が削除を求めたらtrueを返す(書き戻しはせず、削除は呼び出し側が並びごとに行う)。
    template <class Folder, class Switches>
    bool VisitStringAt(Folder& folder, Switches* switches, PluginReferenceSite site, size_t sceneIndex,
                       const SlotOrigin& origin = {}) {
        PluginReferenceSlot slot;
        slot.site = site;
        slot.sceneIndex = sceneIndex;
        slot.folder = folder;
        slot.entity = origin.entity;
        slot.listIndex = origin.listIndex;
        slot.entityInRegistry = origin.inRegistry;
        std::vector<int32_t> switchCopy;
        if constexpr (!Const) {
            if (switches != nullptr) {
                switchCopy = *switches;
                slot.switchOptions = &switchCopy;
            }
        }
        const bool changed = visitor_(slot);
        if constexpr (!Const) {
            if (changed) {
                if (slot.remove) return true;
                folder = std::move(slot.folder);
                if (switches != nullptr) *switches = std::move(switchCopy);
            }
        } else {
            (void)changed;
        }
        return false;
    }

    using SwitchVec = Ref<std::vector<int32_t>>;

    // 要素の並び1本を訪ね、削除を求められた添字(昇順)を返す。一時的な配置物の個体は訪ねない。
    // folderOfとswitchesOfは、要素からフォルダ表記とスイッチ値(無ければnullptr)を取り出す。
    template <class List, class FolderOf, class SwitchesOf>
    std::vector<size_t> VisitList(List& list, PluginReferenceSite site, size_t sceneIndex, FolderOf folderOf,
                                  SwitchesOf switchesOf) {
        std::vector<size_t> removed;
        for (size_t index = 0; index < list.size(); ++index) {
            using Value = typename List::value_type;
            std::conditional_t<std::is_const_v<List>, const Value&, Value&> element = list[index];
            SlotOrigin origin;
            origin.listIndex = index;
            if constexpr (std::is_same_v<Value, rs::world::SceneEntity>) {
                if (!rs::world::IsUserPlacement(element)) continue;
                origin.entity = &element;
            }
            if (VisitStringAt(folderOf(element), switchesOf(element), site, sceneIndex, origin)) {
                removed.push_back(index);
            }
        }
        return removed;
    }

    // VisitListが返した添字の要素を取り除く(読み取り専用の訪問では何もしない)。
    template <class List>
    static void EraseAt(List& list, const std::vector<size_t>& indices) {
        if constexpr (!Const) {
            for (std::vector<size_t>::const_reverse_iterator it = indices.rbegin(); it != indices.rend(); ++it) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(*it));
            }
        } else {
            (void)list;
            (void)indices;
        }
    }

    static constexpr auto kNoSwitches = [](auto&) -> SwitchVec* { return nullptr; };
    static constexpr auto kEntityFolder = [](auto& entity) -> auto& { return entity.folderUtf8; };
    static constexpr auto kEntitySwitches = [](auto& entity) -> SwitchVec* { return &entity.switchOptions; };

    void VisitRegistry() {
        // 配置した情景・車両。情景はアクティブSceneのぶんだけ、車両は全Sceneのぶんがここにある。
        // TrackPluginカテゴリはedgeの区間から導かれる実行時の個体なので、TrackSectionで数える。
        const std::vector<rs::world::SceneEntity>& entities = ctx_.sceneRegistry.Entities();
        std::vector<rs::world::Guid> removed;
        for (size_t index = 0; index < entities.size(); ++index) {
            const rs::world::SceneEntity& entity = entities[index];
            if (!rs::world::IsUserPlacement(entity)) continue;
            PluginReferenceSite site;
            if (entity.category == rs::world::SceneEntityCategory::Scenery) {
                site = PluginReferenceSite::SceneryEntity;
            } else if (entity.category == rs::world::SceneEntityCategory::Car) {
                site = PluginReferenceSite::CarEntity;
            } else {
                continue;
            }
            PluginReferenceSlot slot;
            slot.site = site;
            slot.folder = entity.folderUtf8;
            slot.entity = &entity;
            slot.listIndex = index;
            slot.entityInRegistry = true;
            std::vector<int32_t> switchCopy;
            if constexpr (!Const) {
                switchCopy = entity.switchOptions;
                slot.switchOptions = &switchCopy;
            }
            const bool changed = visitor_(slot);
            if constexpr (!Const) {
                if (changed && slot.remove) {
                    removed.push_back(entity.guid);
                } else if (changed) {
                    ctx_.sceneRegistry.ReplaceEntityPlugin(entity.guid, slot.folder, std::move(switchCopy));
                }
            } else {
                (void)changed;
            }
        }
        // 走査中は並びを変えないよう、削除はまとめてあとで行う。
        if constexpr (!Const) {
            for (const rs::world::Guid& guid : removed) ctx_.sceneRegistry.RemoveEntity(guid);
        }
    }

    template <class Graph>
    void VisitGraph(Graph& graph, size_t sceneIndex) {
        // 順序を決めるため、edgeはguidの昇順に訪ねる。
        using EdgePtr = std::conditional_t<std::is_const_v<Graph>, const rs::world::TrackEdge*,
                                           rs::world::TrackEdge*>;
        std::vector<std::pair<rs::world::Guid, EdgePtr>> edges;
        edges.reserve(graph.edges.size());
        for (auto& entry : graph.edges) edges.emplace_back(entry.first, &entry.second);
        std::sort(edges.begin(), edges.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (auto& entry : edges) {
            for (const std::string& folder : rs::world::ActiveFolders(*entry.second)) {
                PluginReferenceSlot slot;
                slot.site = PluginReferenceSite::TrackSection;
                slot.sceneIndex = sceneIndex;
                slot.folder = folder;
                slot.edge = entry.second;
                const bool changed = visitor_(slot);
                if constexpr (!Const) {
                    if (changed && slot.remove) {
                        EditFolderInEdge(*entry.second, folder, nullptr);
                    } else if (changed && slot.folder != folder) {
                        EditFolderInEdge(*entry.second, folder, &slot.folder);
                    }
                } else {
                    (void)changed;
                }
            }
        }
        const std::vector<size_t> removedSignals = VisitList(
            graph.speedSignals, PluginReferenceSite::Signal, sceneIndex,
            [](auto& signal) -> auto& { return signal.pluginFolderUtf8; }, kEntitySwitches);
        if constexpr (!std::is_const_v<Graph>) {
            if (!removedSignals.empty()) {
                EraseAt(graph.speedSignals, removedSignals);
                rs::world::RebuildSpeedSignalIndices(graph);
            }
        } else {
            (void)removedSignals;
        }
    }

    template <class Overlay>
    void VisitOverlay(Overlay& overlay, size_t sceneIndex) {
        const auto folderOfLine = [](auto& line) -> auto& { return line.lineFolderUtf8; };
        const std::vector<size_t> removedPoles =
            VisitList(overlay.poles, PluginReferenceSite::PlacedIndividual, sceneIndex, kEntityFolder, kNoSwitches);
        const std::vector<size_t> removedPiers =
            VisitList(overlay.piers, PluginReferenceSite::PlacedIndividual, sceneIndex, kEntityFolder, kNoSwitches);
        const std::vector<size_t> removedLines =
            VisitList(overlay.lines, PluginReferenceSite::WireLine, sceneIndex, folderOfLine, kNoSwitches);
        if constexpr (!std::is_const_v<Overlay>) {
            // 個体を消すときは、その個体につながる架線も消す(個別削除と同じ)。
            std::unordered_set<rs::world::Guid, rs::world::GuidHash> goneIds;
            for (const size_t index : removedPoles) goneIds.insert(overlay.poles[index].id);
            for (const size_t index : removedPiers) goneIds.insert(overlay.piers[index].id);
            EraseAt(overlay.poles, removedPoles);
            EraseAt(overlay.piers, removedPiers);
            EraseAt(overlay.lines, removedLines);
            if (!goneIds.empty()) {
                overlay.lines.erase(std::remove_if(overlay.lines.begin(), overlay.lines.end(),
                                                   [&goneIds](const rs::world::WireSpan& line) {
                                                       return goneIds.count(line.poleA) > 0 ||
                                                              goneIds.count(line.poleB) > 0;
                                                   }),
                                    overlay.lines.end());
            }
        }
    }

    template <class Spans>
    void VisitWireSpans(Spans& spans, size_t sceneIndex) {
        EraseAt(spans, VisitList(spans, PluginReferenceSite::WireLine, sceneIndex,
                                 [](auto& span) -> auto& { return span.lineFolderUtf8; }, kNoSwitches));
    }

    void VisitEnvironment(Ref<LayoutSceneCache>& scene, size_t sceneIndex) {
        if (scene.envPlugin.empty()) return;
        PluginReferenceSlot slot;
        slot.site = PluginReferenceSite::Environment;
        slot.sceneIndex = sceneIndex;
        slot.folder = EnvironmentFolder(scene, ctx_.pluginRoots);
        slot.scene = &scene;
        const std::string original = slot.folder;
        const bool changed = visitor_(slot);
        if constexpr (!Const) {
            if (changed && slot.folder != original) {
                scene.envFolderUtf8 = slot.folder;
                scene.envPlugin = PluginFolderLeafName(slot.folder);
            }
        } else {
            (void)changed;
        }
    }

    void VisitFormations() {
        const std::vector<Formation>& formations = ctx_.selector.Formations();
        for (size_t defIndex = 0; defIndex < formations.size(); ++defIndex) {
            std::vector<size_t> removed;
            for (size_t carIndex = 0; carIndex < formations[defIndex].cars.size(); ++carIndex) {
                FormationCar car = formations[defIndex].cars[carIndex];
                PluginReferenceSlot slot;
                slot.site = PluginReferenceSite::FormationCar;
                slot.folder = car.folderUtf8;
                slot.car = &car;
                slot.formationIndex = defIndex;
                slot.listIndex = carIndex;
                std::vector<int32_t> switchCopy;
                if constexpr (!Const) {
                    switchCopy = car.switchOptions;
                    slot.switchOptions = &switchCopy;
                }
                const bool changed = visitor_(slot);
                if constexpr (!Const) {
                    if (changed && slot.remove) {
                        removed.push_back(carIndex);
                    } else if (changed) {
                        car.folderUtf8 = std::move(slot.folder);
                        car.switchOptions = std::move(switchCopy);
                        ctx_.selector.ReplaceFormationCar(defIndex, carIndex, std::move(car));
                    }
                } else {
                    (void)changed;
                }
            }
            // 走査中は並びを変えないよう、削除は編成ごとにあとで(後ろから)行う。
            if constexpr (!Const) {
                for (std::vector<size_t>::const_reverse_iterator it = removed.rbegin(); it != removed.rend(); ++it) {
                    ctx_.selector.RemoveFormationCar(defIndex, *it);
                }
            }
        }
    }

    Context& ctx_;
    const PluginReferenceVisitor& visitor_;
};

}  // namespace

std::string PluginFolderKey(const std::string& folderUtf8) {
    std::string key = folderUtf8;
    std::replace(key.begin(), key.end(), '\\', '/');
    while (!key.empty() && key.back() == '/') key.pop_back();
    return rs::platform_text::ToAsciiLower(std::move(key));
}

std::string PluginFolderLeafName(const std::string& folderUtf8) {
    std::string trimmed = folderUtf8;
    while (!trimmed.empty() && (trimmed.back() == '/' || trimmed.back() == '\\')) trimmed.pop_back();
    const std::filesystem::path path(rs::platform_text::Utf8ToWide(trimmed));
    return rs::platform_text::WideToUtf8(path.filename().wstring());
}

void VisitPluginReferences(PluginReferenceContext& context, const PluginReferenceVisitor& visitor) {
    Walker<false>(context, visitor).Run();
}

void VisitPluginReferences(const ConstPluginReferenceContext& context, const PluginReferenceVisitor& visitor) {
    ConstPluginReferenceContext copy = context;
    Walker<true>(copy, visitor).Run();
}

}  // namespace rs::app
