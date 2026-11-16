//
// SceneEntitySwitchAction.h - 配置スイッチのアプリケーション操作
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "SwitchList.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::editor {
class Editor;
}

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class SceneRenderer;

struct SceneEntitySwitchViewModel {
    bool visible = false;
    rs::world::Guid entityId = rs::world::NilGuid();
    std::string folderUtf8;
    std::vector<SwitchRowViewModel> rows;
};

class SceneEntitySwitchAction : public SwitchValueWriter {
public:
    // targetEntityIdが無効値、または見つからない個体なら、何も書かずViewModelも非表示になる。
    // idが信号モデルのものであるときは、行の作成と書き込みを信号側(SignalBlockAction)へ
    // 任せる(書き込みはUndo対象)。editorがnullptrなら信号は扱わない。
    SceneEntitySwitchAction(rs::world::Guid targetEntityId,
                            rs::world::SceneRegistry& sceneRegistry,
                            SceneRenderer& sceneRenderer,
                            rs::editor::Editor* editor = nullptr);
    void SetSwitch(std::size_t index, std::int32_t value) override;
    SceneEntitySwitchViewModel ViewModel() const;

    // idがスイッチ編集の対象として今も残っているか(SceneRegistryの個体、または信号モデル)。
    // パネルを開いたままにするかの判定で、ImGui版とRmlUi版が共用する。
    static bool TargetExists(const rs::world::Guid& entityId, const rs::world::SceneRegistry& sceneRegistry,
                             const SceneRenderer& sceneRenderer);

private:
    // 対象が信号モデルならその信号のguid。editorがnullptr・信号でないときはnullopt。
    std::optional<rs::world::Guid> TargetSignalGuid() const;

    // ViewModelは値のコピーなので、描画後に保留して一括適用する必要はない。
    rs::world::Guid targetEntityId_;
    rs::world::SceneRegistry& sceneRegistry_;
    SceneRenderer& sceneRenderer_;
    rs::editor::Editor* editor_;
};

} // namespace rs::app
