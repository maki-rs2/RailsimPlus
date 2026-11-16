#include "MissingPluginRefreshGate.h"

#include <functional>

#include "../../editor/tool/Editor.h"
#include "../../world/scene/SceneRegistry.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginSelector.h"

namespace rs::app {

MissingPluginSignature MakeMissingPluginSignature(const rs::editor::Editor& editor,
                                                  const rs::world::SceneRegistry& sceneRegistry,
                                                  const LayoutSession& session, const PluginSelector& selector,
                                                  const std::vector<std::string>& pluginRoots) {
    MissingPluginSignature signature;
    signature.editorRevision = editor.revision;
    signature.registryRevision = sceneRegistry.PersistentRevision();
    signature.activeScene = session.activeIndex;
    signature.sceneCount = session.scenes.size();
    const std::vector<Formation>& formations = selector.Formations();
    signature.formationCount = formations.size();
    for (const Formation& formation : formations) {
        signature.carCount += formation.cars.size();
        for (const FormationCar& car : formation.cars) {
            if (car.unresolved) ++signature.unresolvedCarCount;
        }
    }
    std::hash<std::string> hasher;
    for (const std::string& root : pluginRoots) {
        signature.rootsHash = signature.rootsHash * 1099511628211ULL + hasher(root);
    }
    return signature;
}

bool MissingPluginRefreshGate::Update(const MissingPluginSignature& signature, double deltaSeconds) {
    if (!initialized_ || forced_) {
        initialized_ = true;
        forced_ = false;
        pending_ = false;
        last_ = signature;
        return true;
    }
    if (!(signature == last_)) {
        last_ = signature;
        pending_ = true;
        sinceChange_ = 0.0;
        return false;
    }
    if (!pending_) return false;
    sinceChange_ += deltaSeconds;
    if (sinceChange_ < kSettleSeconds) return false;
    pending_ = false;
    return true;
}

}  // namespace rs::app
