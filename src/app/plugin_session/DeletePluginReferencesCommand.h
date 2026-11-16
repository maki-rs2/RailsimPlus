//
// DeletePluginReferencesCommand.h - プラグイン参照の一括削除を1回のUndoで戻すコマンド
//
// Applyは削除を(やり直しも含めて)実行し、Revertは控えた削除前の要素を元の位置へ戻す。
// 対象の持ち方は ReplacePluginReferencesCommand と同じ。
//
#pragma once

#include <string>
#include <vector>

#include "PluginReferenceDelete.h"
#include "../../editor/tool/Command.h"

namespace rs::app {

class DeletePluginReferencesCommand final : public rs::editor::ICommand {
public:
    // pluginRootsは環境プラグインの参照先を組むルート候補。コマンドが複製を持ち、contextのpluginRootsは無視する。
    DeletePluginReferencesCommand(PluginReferenceContext context, PluginReferenceSite site, std::string folderUtf8,
                                  std::vector<std::string> pluginRoots);

    void Apply(rs::world::TrackGraph& graph) override;
    void Revert(rs::world::TrackGraph& graph) override;

    // 直近のApplyで削除した参照の数。
    size_t DeletedCount() const { return deletedCount_; }

private:
    std::vector<std::string> pluginRoots_;
    PluginReferenceContext context_;
    PluginReferenceSite site_;
    std::string folderUtf8_;
    PluginDeleteLog log_;
    size_t deletedCount_ = 0;
};

}  // namespace rs::app
