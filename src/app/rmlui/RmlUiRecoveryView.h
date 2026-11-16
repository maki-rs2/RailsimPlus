//
// RmlUiRecoveryView.h - 異常終了のあとの復元の画面をデータモデル"workspace"へ結ぶ。
//
// RmlUiLayoutFilesViewと同じ形で、値を見せて操作を受けるだけ。押された結果はTakeCommandで
// 呼び出し側(LayoutDocumentController)へ渡す。
//
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

namespace rs::app::rmlui {

struct RecoveryModel;

struct RecoveryCardContent {
    std::string name;
    std::string date;
};

struct RecoveryCommand {
    enum class Kind { Restore, Discard, NormalStart, SafeMode };
    Kind kind = Kind::NormalStart;
    std::size_t index = 0; // Restore・Discardのカードの添字
};

class RmlUiRecoveryView {
public:
    RmlUiRecoveryView();
    ~RmlUiRecoveryView();
    RmlUiRecoveryView(const RmlUiRecoveryView&) = delete;
    RmlUiRecoveryView& operator=(const RmlUiRecoveryView&) = delete;

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    void SetCards(const std::vector<RecoveryCardContent>& cards);
    std::optional<RecoveryCommand> TakeCommand();

private:
    void SetCommand(RecoveryCommand::Kind kind, int index);

    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<RecoveryModel> model_;
    std::optional<RecoveryCommand> command_;
};

} // namespace rs::app::rmlui
