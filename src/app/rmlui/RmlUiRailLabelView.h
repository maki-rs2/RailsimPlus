#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../core/Math.h"
#include "RmlUiInlineRename.h"
#include "../workspace/RailLabelHover.h"
#include "../workspace/RailLabelPresentation.h"

namespace Rml {
class ElementDocument;
class Event;
}

namespace rs::app {
struct SignalBlockViewportLink;
struct SignalBlockViewModel;
}

namespace rs::app::rmlui {

struct RailLabelsModel;

class RmlUiRailLabelView {
public:
    RmlUiRailLabelView();
    ~RmlUiRailLabelView();
    RmlUiRailLabelView(const RmlUiRailLabelView&) = delete;
    RmlUiRailLabelView& operator=(const RmlUiRailLabelView&) = delete;

    // ensureSelectedがtrueなら選択を保ち、falseならトグルする。書き込みは呼び出し側のActionへ渡す。
    void Attach(std::function<void(rs::world::Guid, bool)> selectBlock,
                std::function<void(rs::world::Guid, const std::string&)> renameBlock,
                std::function<void(rs::world::Guid)> chooseOwner,
                std::function<bool()> cancelPending);
    bool CancelPendingPlacement() { return cancelPending_ && cancelPending_(); }
    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 3Dホバーは前フレームの結果。ラベル上のポインタはContextの現在の要素から毎フレーム引く。
    void Refresh(bool active, const rs::world::TrackGraph& graph, const SignalBlockViewportLink& link,
                 const SignalBlockViewModel& placement,
                 const rs::world::Guid& selectedBlock, std::size_t sceneIndex, double deltaSeconds,
                 Rml::ElementDocument* document);
    void UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight);

private:
    void OnSelectLabel(const std::string& id);
    void OnBeginRename(Rml::Event& event, const std::string& id);
    void OnCommitName(Rml::Event& event);

    std::function<void(rs::world::Guid, bool)> selectBlock_;
    std::function<void(rs::world::Guid, const std::string&)> renameBlock_;
    std::function<void(rs::world::Guid)> chooseOwner_;
    std::function<bool()> cancelPending_;
    RmlUiInlineRename rename_;
    std::optional<std::size_t> sceneIndex_;
    std::vector<workspace::RailLabelItem> items_;
    const rs::world::TrackGraph* graph_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<RailLabelsModel> model_;
};

} // namespace rs::app::rmlui
