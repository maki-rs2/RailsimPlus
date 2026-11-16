//
// ReplacePluginReferencesCommand.h - プラグイン参照の一括差し替えを1回のUndoで戻すコマンド
//
// Applyは差し替えを(やり直しも含めて)実行し、Revertは控えた変更前の値を同じ場所へ書き戻す。
// 対象はEditor・SceneRegistry・PluginSelector・LayoutSessionなど、アプリが持つ実体への参照で、
// Scene単位のキャッシュはScene番号で引く(シーン切替でCommandStackがクリアされるので、番号は有効)。
//
#pragma once

#include <string>
#include <vector>

#include "PluginReferenceReplace.h"
#include "../../editor/tool/Command.h"

namespace rs::app {

class ReplacePluginReferencesCommand final : public rs::editor::ICommand {
public:
    // pluginRootsは環境プラグインの参照先を組むルート候補。コマンドが複製を持ち、contextのpluginRootsは無視する。
    ReplacePluginReferencesCommand(PluginReferenceContext context, PluginReplacePlan plan,
                                   std::vector<std::string> pluginRoots);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

    // 直近のApplyで書き換えた参照の数。
    size_t ReplacedCount() const { return replacedCount_; }

private:
    std::vector<std::string> pluginRoots_;
    PluginReferenceContext context_;
    PluginReplacePlan plan_;
    PluginReplaceLog log_;
    size_t replacedCount_ = 0;
};

}  // namespace rs::app
