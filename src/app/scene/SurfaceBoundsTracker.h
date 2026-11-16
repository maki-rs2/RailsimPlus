//
// SurfaceBoundsTracker.h - 現在のSceneに置かれたSurfaceの範囲を求める
//
// SceneRegistry内でSurface種別のプラグインを指すエンティティを探し、定義の
// SurfaceInfo(SizeX/SizeZ)を範囲として返す。結果は構成の世代(Revision)が変わるまで使い回す。
//
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "../../world/scene/SceneRegistry.h"
#include "../../world/scene/SurfaceBounds.h"

namespace rs::app {

// フォルダがSurface種別のプラグインか(親フォルダ名が"Surface"。種別=親フォルダ名の規約)。
bool IsSurfacePluginFolder(const std::string& folderUtf8);

class SurfaceBoundsTracker {
public:
    // 範囲を返す。Surfaceが無い、またはSurfaceInfoが読めないときはnullopt(押し戻さない)。
    const std::optional<rs::world::SurfaceBounds>& Update(const rs::world::SceneRegistry& registry);

private:
    uint64_t lastRevision_ = ~uint64_t{0};
    std::optional<rs::world::SurfaceBounds> bounds_;
};

}  // namespace rs::app
