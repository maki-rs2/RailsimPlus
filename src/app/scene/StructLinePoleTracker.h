//
// StructLinePoleTracker.h - 現在のSceneに置かれたStructの架線接続点(LinePoint)の柱を保持する
//
// SceneRegistryの構成(StructureRevision)が変わったときだけ柱を導出し直す。結果が前回と
// 同じなら自分のRevisionは進めず、架線メッシュの再構築を引き起こさない。
// プラグイン定義の読み出し(LinePointの引き当て)もここに集める。
//
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../plugins/schema/PluginSchema.h"
#include "../../world/scene/SceneRegistry.h"
#include "StructLinePoles.h"

namespace rs::app {

// プラグインフォルダのLinePointを定義から引く。種別がStructのときだけ返す。フォルダごとに結果を覚える。
class PluginLinePointLookup {
public:
    const std::vector<rs::legacy_pi::LinePointDef>* operator()(const std::string& folderUtf8);

private:
    std::unordered_map<std::string, std::vector<rs::legacy_pi::LinePointDef>> cache_;
};

class StructLinePoleTracker {
public:
    // 構成が変わっていれば導出し直す。graphは軌道上に置かれたエンティティの座標解決に使う。
    void Update(const rs::world::SceneRegistry& registry, const rs::world::TrackGraph& graph);

    const std::vector<rs::world::PlacedIndividual>& Poles() const { return poles_; }

    // 導出結果が変わるたびに進む。
    uint64_t Revision() const { return revision_; }

    // 前回の呼び出し以降に導出結果が変わっていればtrue(見たRevisionを覚える。使う側は1か所)。
    bool TakeChanged() {
        const bool changed = revision_ != consumedRevision_;
        consumedRevision_ = revision_;
        return changed;
    }

private:
    uint64_t lastStructureRevision_ = ~uint64_t{0};
    uint64_t revision_ = 0;
    uint64_t consumedRevision_ = 0;
    std::vector<rs::world::PlacedIndividual> poles_;
    PluginLinePointLookup lookup_;
};

// Sceneキャッシュの仕上げ。entitiesのStructから柱を導出し、overlay内の同じ位置のプラグインなしの柱を
// 導出した柱へ置き換える(.rsl読込と.rs2取り込みで共通)。導出した柱を返す。
std::vector<rs::world::PlacedIndividual> BindSceneLinePoles(
    const std::vector<rs::world::SceneEntity>& entities, const rs::world::TrackGraph& graph,
    rs::world::ManualPlacementOverlay& overlay);

}  // namespace rs::app
