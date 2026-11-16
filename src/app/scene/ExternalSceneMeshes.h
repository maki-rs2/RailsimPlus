//
// ExternalSceneMeshes.h - 配置物のidごとの「外から渡された表示メッシュ」の登録口
//
// 押し出し表示(Rail/Tie/Girder/Line)や、アセンブリが無くパーツだけのプラグインは、プラグイン自身の
// 組立メッシュを持たない。そのような配置物を情景スロットで出すとき、呼び出し側が表示用のメッシュを
// ここへ登録しておく。スロットの読込は、登録のある配置物だけ、このメッシュを組立メッシュとして使う
// (登録の無い通常の配置物は、組立メッシュの無いプラグインを読込失敗として扱うまま)。
// 登録は配置物を消すときに外す。メッシュは読み取り専用で共有する。
//
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "../../render/mesh/Mesh.h"
#include "../../world/alignment/Guid.h"

namespace rs::app {

class ExternalSceneMeshes {
public:
    void Set(const rs::world::Guid& entityId, std::shared_ptr<const rs::render::Mesh> mesh) {
        meshes_[entityId] = std::move(mesh);
    }
    void Erase(const rs::world::Guid& entityId) { meshes_.erase(entityId); }
    // 登録が無ければnullptr。
    std::shared_ptr<const rs::render::Mesh> Find(const rs::world::Guid& entityId) const {
        const MeshMap::const_iterator it = meshes_.find(entityId);
        return it != meshes_.end() ? it->second : nullptr;
    }

private:
    using MeshMap = std::unordered_map<rs::world::Guid, std::shared_ptr<const rs::render::Mesh>,
                                       rs::world::GuidHash>;
    MeshMap meshes_;
};

} // namespace rs::app
