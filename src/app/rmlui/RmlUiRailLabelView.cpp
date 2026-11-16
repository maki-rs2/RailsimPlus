#include "RmlUiRailLabelView.h"

#include <utility>
#include <algorithm>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "RmlUiAnchoredArea.h"
#include "RmlUiAnchoredElementPlacement.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/BlockRegistry.h"
#include "../ui/SignalBlockViewportLink.h"
#include "../action/SignalBlockAction.h"

namespace rs::app::rmlui {

struct RailLabelModel {
    Rml::String id;
    Rml::String name;
    bool selected = false;
    bool warning = false;
    Rml::String colorClass;
    int verticalIndex = 0;
    bool shown = false;
    Rml::String left;
    Rml::String top;
    bool disabled = false;
    Rml::String title;
    bool operator==(const RailLabelModel&) const = default;
};

struct RailLabelsModel {
    Rml::Vector<RailLabelModel> labels;
};

namespace {

constexpr const char* kLabelsKey = "railLabels";
constexpr const char* kEditingIdKey = "railLabelEditingId";
constexpr const char* kEditingInputSelector = ".rail-label.is-editing .rail-label-name-input";
constexpr const char* kLabelSelector = ".rail-label";
constexpr const char* kBlockIdAttribute = "data-block-id";

// 寸法はworkspace.rcssのトークンに合わせる。未表示の1フレームだけ幅と1行の高さを目安にする。
constexpr double kDefaultLabelWidthDp = 160.0; // = --rail-label-width
constexpr double kLabelLineHeightDp = 20.0;   // = --rail-label-line-height = ceil(--font-size(16) × 1.2)
constexpr double kLabelPaddingYDp = 4.0;      // = --pad-widget-y
constexpr double kLabelBorderDp = 1.0;        // = --border-width
constexpr double kDefaultLabelHeightDp = kLabelLineHeightDp + 2.0 * (kLabelPaddingYDp + kLabelBorderDp);
constexpr double kLabelGapDp = 6.0;           // = --gap-y
constexpr double kAnchorGapDp = 8.0;          // = --gap-x。レールのクリックを奪わないすき間
constexpr double kEdgeMarginDp = 8.0;         // = --gap-x

} // namespace

RmlUiRailLabelView::RmlUiRailLabelView()
    : rename_(kEditingIdKey, kEditingInputSelector), model_(std::make_unique<RailLabelsModel>()) {}
RmlUiRailLabelView::~RmlUiRailLabelView() = default;

void RmlUiRailLabelView::Attach(std::function<void(rs::world::Guid, bool)> selectBlock,
                               std::function<void(rs::world::Guid, const std::string&)> renameBlock,
                               std::function<void(rs::world::Guid)> chooseOwner,
                               std::function<bool()> cancelPending) {
    selectBlock_ = std::move(selectBlock);
    renameBlock_ = std::move(renameBlock);
    chooseOwner_ = std::move(chooseOwner);
    cancelPending_ = std::move(cancelPending);
}

void RmlUiRailLabelView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<RailLabelModel> handle = constructor.RegisterStruct<RailLabelModel>()) {
        handle.RegisterMember("id", &RailLabelModel::id);
        handle.RegisterMember("name", &RailLabelModel::name);
        handle.RegisterMember("selected", &RailLabelModel::selected);
        handle.RegisterMember("warning", &RailLabelModel::warning);
        handle.RegisterMember("colorClass", &RailLabelModel::colorClass);
        handle.RegisterMember("verticalIndex", &RailLabelModel::verticalIndex);
        handle.RegisterMember("shown", &RailLabelModel::shown);
        handle.RegisterMember("left", &RailLabelModel::left);
        handle.RegisterMember("top", &RailLabelModel::top);
        handle.RegisterMember("disabled", &RailLabelModel::disabled);
        handle.RegisterMember("title", &RailLabelModel::title);
    }
    constructor.RegisterArray<Rml::Vector<RailLabelModel>>();
    constructor.Bind(kLabelsKey, &model_->labels);
    rename_.RegisterModel(constructor);
    constructor.BindEventCallback("select_rail_label",
        [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            OnSelectLabel(FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback("begin_rename_rail_label",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            OnBeginRename(event, FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback("commit_rail_label_name",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { OnCommitName(event); });
    constructor.BindEventCallback("key_rail_label_name",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { rename_.OnKey(event); });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiRailLabelView::OnSelectLabel(const std::string& id) {
    // dblclickの直後のclickと入力中のクリックでは、選択を戻さない。
    if (!selectBlock_ || rename_.IsEditing(id)) return;
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(id);
    if (!guid) return;
    const auto item = std::find_if(items_.begin(), items_.end(),
        [&guid](const workspace::RailLabelItem& value) { return value.guid == *guid; });
    if (item == items_.end() || item->disabled) return;
    if (item->candidate) {
        if (chooseOwner_) chooseOwner_(*guid);
        return;
    }
    if (guid.has_value() && graph_ != nullptr && rs::world::FindBlock(*graph_, *guid) != nullptr) {
        selectBlock_(*guid, false);
    }
}

void RmlUiRailLabelView::OnBeginRename(Rml::Event& event, const std::string& id) {
    if (!selectBlock_ || !renameBlock_ || graph_ == nullptr) return;
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(id);
    if (!guid.has_value()) return;
    const auto item = std::find_if(items_.begin(), items_.end(),
        [&guid](const workspace::RailLabelItem& value) { return value.guid == *guid; });
    if (item == items_.end() || item->candidate) return;
    const rs::world::Block* const block = rs::world::FindBlock(*graph_, *guid);
    Rml::Element* const element = event.GetCurrentElement();
    if (block == nullptr || element == nullptr) return;
    selectBlock_(*guid, true);
    rename_.Begin(id, *guid, block->name, element->GetOwnerDocument());
}

void RmlUiRailLabelView::OnCommitName(Rml::Event& event) {
    if (!renameBlock_) return;
    // blurの後に選択が移っても、編集を始めたguidの名前だけを変える(確定先は部品が持つ)。
    const std::optional<RmlUiInlineRename::Commit> commit = rename_.OnCommit(event);
    if (commit.has_value()) renameBlock_(commit->target, commit->name);
}

void RmlUiRailLabelView::Refresh(bool active, const rs::world::TrackGraph& graph,
                                const SignalBlockViewportLink& link, const SignalBlockViewModel& placement,
                                const rs::world::Guid& selectedBlock,
                                std::size_t sceneIndex, double /*deltaSeconds*/, Rml::ElementDocument* document) {
    if (!modelHandle_) return;
    graph_ = &graph;
    document_ = document;
    if (!active || sceneIndex_ != sceneIndex) {
        rename_.End();
    }
    if (rename_.Active() && rs::world::FindBlock(graph, rename_.Target()) == nullptr) rename_.End();
    rename_.FocusPending(document);
    sceneIndex_ = active ? std::optional<std::size_t>(sceneIndex) : std::nullopt;
    items_.clear();
    if (active && placement.candidateAnchor) {
        rename_.End();
        items_ = workspace::BuildRailLabelCandidateItems(graph, *placement.candidateAnchor, placement.ownerCandidates);
    } else if (active) {
        // 閉塞のラベルは、信号モードの間は常に、閉塞の中点の画面上へ出す。
        std::vector<workspace::RailLabel> labels;
        for (const rs::world::Block& block : graph.blocks) {
            const std::optional<workspace::RailLabelAnchor> anchor = workspace::BlockMidpointAnchor(graph, block);
            if (anchor.has_value()) labels.push_back({block.guid, *anchor});
        }
        items_ = workspace::BuildRailLabelItems(graph, labels, selectedBlock, link.connectivity);
    }
    Rml::Vector<RailLabelModel> labels;
    for (const workspace::RailLabelItem& item : items_) {
        RailLabelModel label;
        label.id = rs::world::GuidToHex(item.guid);
        label.name = item.name;
        label.selected = item.selected;
        label.warning = item.warning;
        label.colorClass = item.colorClass;
        label.disabled = item.disabled;
        label.title = item.title;
        label.verticalIndex = static_cast<int>(item.verticalIndex);
        for (const RailLabelModel& previous : model_->labels) {
            if (previous.id != label.id) continue;
            label.shown = previous.shown;
            label.left = previous.left;
            label.top = previous.top;
            break;
        }
        labels.push_back(std::move(label));
    }
    UpdateVariable(modelHandle_, kLabelsKey, model_->labels, labels);
}

void RmlUiRailLabelView::UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight) {
    if (!modelHandle_ || graph_ == nullptr || items_.empty()) return;
    const double dpRatio = document_ != nullptr && document_->GetContext() != nullptr
                               ? document_->GetContext()->GetDensityIndependentPixelRatio() : 1.0;
    const AnchoredElementSizes measured = MeasureAnchoredElementSizes(document_, kLabelSelector, kBlockIdAttribute);
    const AnchoredArea area = MeasureAnchoredArea(document_, screenWidth, screenHeight);
    bool changed = false;
    for (const workspace::RailLabelItem& first : items_) {
        if (first.verticalIndex != 0) continue;
        std::vector<std::size_t> indices;
        std::vector<workspace::RailLabelSize> sizes;
        for (std::size_t index = 0; index < items_.size(); ++index) {
            if (items_[index].anchor != first.anchor) continue;
            indices.push_back(index);
            const auto found = measured.find(model_->labels[index].id);
            sizes.push_back(found != measured.end()
                                ? workspace::RailLabelSize{found->second.width, found->second.height}
                                : workspace::RailLabelSize{kDefaultLabelWidthDp * dpRatio,
                                                           kDefaultLabelHeightDp * dpRatio});
        }
        const workspace::RailLabelStack stack = workspace::BuildRailLabelStack(sizes, kLabelGapDp * dpRatio);
        const auto edge = graph_->edges.find(first.anchor.edge);
        const bool shown = edge != graph_->edges.end();
        workspace::AnchoredElementPlacement placement;
        if (shown) {
            const rs::world::TrackFrame frame = rs::world::EvaluateAt(edge->second, first.anchor.s, +1);
            placement = workspace::PlaceAnchoredElement({ProjectAnchoredPoint(frame.pos, viewProj), screenWidth, screenHeight,
                         area.left, area.top, area.right, area.bottom, stack.size.width, stack.size.height,
                         kEdgeMarginDp * dpRatio, kAnchorGapDp * dpRatio});
        }
        for (std::size_t row = 0; row < indices.size(); ++row) {
            RailLabelModel& label = model_->labels[indices[row]];
            Rml::String left = label.left;
            Rml::String top = label.top;
            if (shown) PlaceAnchoredElementPosition(placement, left, top, stack.tops[row]);
            if (label.shown == shown && label.left == left && label.top == top) continue;
            label.shown = shown;
            label.left = std::move(left);
            label.top = std::move(top);
            changed = true;
        }
    }
    if (changed) modelHandle_.DirtyVariable(kLabelsKey);
}

} // namespace rs::app::rmlui
