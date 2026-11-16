#pragma once

#include <string>

namespace rs::editor {
class Editor;
}

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {
class CameraController;
class PluginSelector;
struct LayoutSession;

struct LayoutDigestResult {
    bool ok = false;
    std::string detail;
};

LayoutDigestResult WriteLayoutDigest(const std::string& pathUtf8, const LayoutSession& session,
                                     const rs::editor::Editor& editor,
                                     const rs::world::SceneRegistry& sceneRegistry,
                                     const CameraController& camera,
                                     const PluginSelector& selector);
}  // namespace rs::app
