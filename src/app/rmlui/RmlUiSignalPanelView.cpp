#include "RmlUiSignalPanelView.h"

#include <cstdlib>
#include <optional>
#include <string>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>

#include "RmlUiAnchoredArea.h"
#include "RmlUiAnchoredElementPlacement.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdatePhase.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/TrackTraversal.h"
#include "../action/SignalBlockAction.h"
#include "../action/SignalPanelAction.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app::rmlui {
using namespace workspace;

namespace {

constexpr const char* kCardSelector = ".signal-card";
constexpr const char* kSignalIdAttribute = "data-signal-id";
// 閉塞を作るボタン(1つだけ)。測定の表では、このidで引く。
constexpr const char* kBlockButtonSelector = ".signal-block-button";
constexpr const char* kBlockButtonId = "block";
constexpr const char* kBlockButtonAttribute = "data-block-id";
// 未表示のカードの大きさの見込み(閉じたカード1行ぶん)と、信号・画面端とのすき間。
constexpr double kDefaultCardWidthDp = 160.0; // = --signal-card-width
constexpr double kDefaultCardHeightDp = 30.0;
constexpr double kDefaultBlockButtonWidthDp = 120.0; // = 行の高さ20dp + 上下の余白4dp×2 + 枠1dp×2
constexpr double kAnchorGapDp = 8.0;          // = --gap-x。信号のクリックを奪わないすき間
constexpr double kEdgeMarginDp = 8.0;         // = --gap-x

} // namespace

RmlUiSignalPanelView::RmlUiSignalPanelView(ToastQueue& queue) : queue_(queue) {}

void RmlUiSignalPanelView::RegisterModel(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<SignalPanelStage> stage = constructor.RegisterStruct<SignalPanelStage>();
    stage.RegisterMember("speed", &SignalPanelStage::speed_);
    stage.RegisterMember("removable", &SignalPanelStage::removable_);
    constructor.RegisterArray<std::vector<SignalPanelStage>>();
    Rml::StructHandle<SignalPanelSignal> signal = constructor.RegisterStruct<SignalPanelSignal>();
    signal.RegisterMember("id", &SignalPanelSignal::id_);
    signal.RegisterMember("name", &SignalPanelSignal::name_);
    signal.RegisterMember("direction", &SignalPanelSignal::direction_);
    signal.RegisterMember("aspectText", &SignalPanelSignal::aspectText_);
    signal.RegisterMember("forcedRed", &SignalPanelSignal::forcedRed_);
    signal.RegisterMember("selected", &SignalPanelSignal::selected_);
    signal.RegisterMember("stages", &SignalPanelSignal::stages_);
    signal.RegisterMember("addableSpeeds", &SignalPanelSignal::addableSpeeds_);
    signal.RegisterMember("shown", &SignalPanelSignal::shown_);
    signal.RegisterMember("left", &SignalPanelSignal::left_);
    signal.RegisterMember("top", &SignalPanelSignal::top_);
    constructor.RegisterArray<std::vector<SignalPanelSignal>>();
    Rml::StructHandle<SignalPanelPresentation> panel = constructor.RegisterStruct<SignalPanelPresentation>();
    panel.RegisterMember("visible", &SignalPanelPresentation::visible_);
    panel.RegisterMember("canCreateBlock", &SignalPanelPresentation::canCreateBlock_);
    panel.RegisterMember("blockButtonShown", &SignalPanelPresentation::blockButtonShown_);
    panel.RegisterMember("blockButtonLeft", &SignalPanelPresentation::blockButtonLeft_);
    panel.RegisterMember("blockButtonTop", &SignalPanelPresentation::blockButtonTop_);
    panel.RegisterMember("signals", &SignalPanelPresentation::signals_);
    constructor.Bind("signalPanel", &presentation_);
    constructor.BindEventCallback("signal_panel_ui", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnUi(event, arguments); });
    constructor.BindEventCallback("signal_panel_action", [this](Rml::DataModelHandle, Rml::Event& event,
        const Rml::VariantList& arguments) { OnAction(event, arguments); });
    constructor.BindEventCallback("signal_create_block", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (createBlock_ && presentation_.canCreateBlock_) createBlock_();
    });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiSignalPanelView::Refresh(bool active, const rs::world::TrackGraph& graph,
                                   const std::optional<BlockAnchor>& blockAnchor, Rml::ElementDocument* document,
                                   std::vector<SignalPanelSignal> cards) {
    graph_ = &graph;
    document_ = document;
    const bool canCreateBlock = blockAnchor.has_value();
    const rs::world::Guid blockEdge = canCreateBlock ? blockAnchor->edge : rs::world::NilGuid();
    const double blockS = canCreateBlock ? blockAnchor->s : 0.0;
    bool changed = presentation_.visible_ != active || presentation_.canCreateBlock_ != canCreateBlock ||
                   presentation_.blockEdge_ != blockEdge || presentation_.blockS_ != blockS;
    presentation_.visible_ = active;
    presentation_.canCreateBlock_ = canCreateBlock;
    presentation_.blockEdge_ = blockEdge;
    presentation_.blockS_ = blockS;
    if (active) changed = presentation_.Sync(std::move(cards)) || changed;
    if (changed) modelHandle_.DirtyVariable("signalPanel");
}

void RmlUiSignalPanelView::UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight) {
    if (!modelHandle_ || graph_ == nullptr || !presentation_.visible_) return;
    const double dpRatio = document_ != nullptr && document_->GetContext() != nullptr
                               ? document_->GetContext()->GetDensityIndependentPixelRatio() : 1.0;
    const AnchoredArea area = MeasureAnchoredArea(document_, screenWidth, screenHeight);
    bool changed = UpdateBlockButton(viewProj, screenWidth, screenHeight, area, dpRatio);
    const AnchoredElementSizes measured = MeasureAnchoredElementSizes(document_, kCardSelector, kSignalIdAttribute);
    for (SignalPanelSignal& signal : presentation_.signals_) {
        const auto edge = graph_->edges.find(signal.edge_);
        const bool onTrack = edge != graph_->edges.end();
        Rml::String left = signal.left_;
        Rml::String top = signal.top_;
        if (onTrack) {
            const rs::world::TrackFrame frame = rs::world::EvaluateAt(edge->second, signal.s_, +1);
            PlaceMeasuredAnchoredElement(measured, signal.id_,
                {ProjectAnchoredPoint(frame.pos, viewProj), screenWidth, screenHeight, area.left, area.top, area.right,
                 area.bottom, kDefaultCardWidthDp * dpRatio, kDefaultCardHeightDp * dpRatio, kEdgeMarginDp * dpRatio,
                 kAnchorGapDp * dpRatio},
                left, top);
        }
        if (signal.shown_ == onTrack && signal.left_ == left && signal.top_ == top) continue;
        signal.shown_ = onTrack;
        signal.left_ = std::move(left);
        signal.top_ = std::move(top);
        changed = true;
    }
    if (changed) modelHandle_.DirtyVariable("signalPanel");
}

bool RmlUiSignalPanelView::UpdateBlockButton(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight,
                                             const AnchoredArea& area, double dpRatio) {
    const auto edge = graph_->edges.find(presentation_.blockEdge_);
    const bool onTrack = presentation_.canCreateBlock_ && edge != graph_->edges.end();
    Rml::String left = presentation_.blockButtonLeft_;
    Rml::String top = presentation_.blockButtonTop_;
    if (onTrack) {
        const rs::world::TrackFrame frame = rs::world::EvaluateAt(edge->second, presentation_.blockS_, +1);
        const AnchoredElementSizes measured =
            MeasureAnchoredElementSizes(document_, kBlockButtonSelector, kBlockButtonAttribute);
        PlaceMeasuredAnchoredElement(measured, kBlockButtonId,
            {ProjectAnchoredPoint(frame.pos, viewProj), screenWidth, screenHeight, area.left, area.top, area.right,
             area.bottom, kDefaultBlockButtonWidthDp * dpRatio, kDefaultCardHeightDp * dpRatio, kEdgeMarginDp * dpRatio,
             kAnchorGapDp * dpRatio},
            left, top);
    }
    if (presentation_.blockButtonShown_ == onTrack && presentation_.blockButtonLeft_ == left &&
        presentation_.blockButtonTop_ == top) return false;
    presentation_.blockButtonShown_ = onTrack;
    presentation_.blockButtonLeft_ = std::move(left);
    presentation_.blockButtonTop_ = std::move(top);
    return true;
}

void RmlUiSignalPanelView::OnUi(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    if (EventArgumentAt<std::string>(arguments, 0, "") == "select") {
        presentation_.Select(EventArgumentAt<int>(arguments, 1, -1));
    }
    modelHandle_.DirtyVariable("signalPanel");
}

void RmlUiSignalPanelView::OnAction(Rml::Event& event, const Rml::VariantList& arguments) {
    event.StopPropagation();
    using namespace signal_panel_action;
    const std::string kind = EventArgumentAt<std::string>(arguments, 0, "");
    const int signal = EventArgumentAt<int>(arguments, 1, -1);
    const std::string value = EventArgumentAt<std::string>(arguments, 2, "");
    if (signal < 0 || static_cast<std::size_t>(signal) >= presentation_.signals_.size() || !withAction_) return;
    const SignalPanelSignal& card = presentation_.signals_[static_cast<std::size_t>(signal)];
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(card.id_);
    if (!guid) return;
    const rs::world::Guid signalGuid = *guid;
    if (kind == "direction") {
        withAction_([&](SignalBlockAction& action, const rs::world::WarpLinkTable* warpLinks, std::size_t sceneIndex) {
            SetDirection(action, queue_, signalGuid, value, warpLinks, sceneIndex);
        });
    } else if (kind == "forceRed") {
        const bool forced = event.GetCurrentElement() != nullptr && event.GetCurrentElement()->HasAttribute("checked");
        withAction_([&](SignalBlockAction& action, const rs::world::WarpLinkTable*, std::size_t) { SetForceRed(action, signalGuid, forced); });
    } else if (kind == "addStage") {
        // `+`のボタンの直前のコンボで選んでいる速度を足す(コンボを変えただけでは足さない)。
        Rml::Element* const button = event.GetCurrentElement();
        Rml::ElementFormControl* const select = rmlui_dynamic_cast<Rml::ElementFormControl*>(button != nullptr ? button->GetPreviousSibling() : nullptr);
        const std::string speed = select != nullptr ? select->GetValue() : std::string();
        if (speed.empty()) return;
        withAction_([&](SignalBlockAction& action, const rs::world::WarpLinkTable*, std::size_t) { AddStage(action, signalGuid, std::atoi(speed.c_str())); });
    } else if (kind == "removeStage") {
        const int stage = EventArgumentAt<int>(arguments, 2, -1);
        if (stage < 0 || static_cast<std::size_t>(stage) >= card.stages_.size()) return;
        const int speed = std::atoi(card.stages_[static_cast<std::size_t>(stage)].speed_.c_str());
        withAction_([&](SignalBlockAction& action, const rs::world::WarpLinkTable*, std::size_t) { RemoveStage(action, signalGuid, speed); });
    } else if (kind == "delete") {
        withAction_([&](SignalBlockAction& action, const rs::world::WarpLinkTable*, std::size_t) { DeleteSignal(action, signalGuid); });
    }
}

} // namespace rs::app::rmlui
