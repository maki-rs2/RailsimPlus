//
// SceneryPlacementAction.h - 情景(Station/Struct等)の配置プレビューモードの開始・終了
//
// ImGui版とRmlUi版のブラウザが同じ操作を呼ぶための入口。
//
#pragma once

#include <string>

namespace rs::editor { class Editor; }

namespace rs::app {

struct SceneryPlacementOptions {
    bool continuous = false;
    bool stationAnchor = false;
};

class SceneryPlacementAction {
public:
    explicit SceneryPlacementAction(rs::editor::Editor& editor) : editor_(editor) {}

    // ほかの編集サブモードを落として配置モードへ入る。
    void Begin(const std::string& folderUtf8, const SceneryPlacementOptions& options);
    static bool CanAnchor(const std::string& folderUtf8);
    void End();
    bool Active() const;
    // folderUtf8のプラグインで配置プレビュー中か(駅アンカー配置を含む)。
    bool IsPlacing(const std::string& folderUtf8) const;

private:
    rs::editor::Editor& editor_;
};

} // namespace rs::app
