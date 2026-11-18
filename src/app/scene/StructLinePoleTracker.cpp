#include "StructLinePoleTracker.h"

#include "../../plugins/load/PluginDefinitionLookup.h"
#include <utility>
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

namespace {

bool SamePole(const rs::world::PlacedIndividual& a, const rs::world::PlacedIndividual& b) {
    return a.id == b.id && a.worldPos.x == b.worldPos.x && a.worldPos.y == b.worldPos.y &&
           a.worldPos.z == b.worldPos.z && a.worldYawDeg == b.worldYawDeg;
}

bool SamePoles(const std::vector<rs::world::PlacedIndividual>& a,
               const std::vector<rs::world::PlacedIndividual>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (!SamePole(a[index], b[index])) return false;
    }
    return true;
}

}  // namespace

const std::vector<rs::legacy_pi::LinePointDef>* PluginLinePointLookup::operator()(
    const std::string& folderUtf8) {
    std::unordered_map<std::string, std::vector<rs::legacy_pi::LinePointDef>>::iterator it =
        cache_.find(folderUtf8);
    if (it == cache_.end()) {
        std::vector<rs::legacy_pi::LinePointDef> points;
        if (!folderUtf8.empty()) {
            const rs::legacy_pi::ParsedDefinitionResult definition =
                rs::legacy_plugin::LoadPluginDefinition(folderUtf8);
            if (definition.ok && definition.def.header.type == rs::legacy_pi::PluginType::Struct) {
                points = definition.def.linePoints;
            }
        }
        it = cache_.emplace(folderUtf8, std::move(points)).first;
    }
    return it->second.empty() ? nullptr : &it->second;
}

void StructLinePoleTracker::Update(const rs::world::SceneRegistry& registry,
                                   const rs::world::TrackGraph& graph) {
    if (registry.StructureRevision() == lastStructureRevision_) return;
    lastStructureRevision_ = registry.StructureRevision();
    std::vector<rs::world::PlacedIndividual> derived = DeriveStructLinePoles(
        registry.Entities(), graph,
        [this](const std::string& folderUtf8) { return lookup_(folderUtf8); });
    if (SamePoles(derived, poles_)) return;
    poles_ = std::move(derived);
    ++revision_;
}

std::vector<rs::world::PlacedIndividual> BindSceneLinePoles(
    const std::vector<rs::world::SceneEntity>& entities, const rs::world::TrackGraph& graph,
    rs::world::ManualPlacementOverlay& overlay) {
    PluginLinePointLookup lookup;
    std::vector<rs::world::PlacedIndividual> derived = DeriveStructLinePoles(
        entities, graph,
        [&lookup](const std::string& folderUtf8) { return lookup(folderUtf8); });
    BindPlainPolesToStructLinePoles(overlay, derived);
    return derived;
}

}  // namespace rs::app
