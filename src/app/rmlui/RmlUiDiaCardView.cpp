#include "RmlUiDiaCardView.h"

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "RmlUiAnchoredArea.h"
#include "RmlUiAnchoredElementPlacement.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"
#include "../action/DiaPanelBranchBuilder.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/PlatformRegistry.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"

namespace rs::app::rmlui {

struct DiaCardModel {
    Rml::String id;
    Rml::String name;
    Rml::String title;
    bool selected = false;
    Rml::String left;
    Rml::String top;
    bool operator==(const DiaCardModel&) const = default;
};

struct DiaCardsModel {
    Rml::Vector<DiaCardModel> cards;
};

namespace {

using workspace::DiaCardCandidate;
using workspace::DiaCardKind;

constexpr const char* kCardsKey = "diaCards";
constexpr const char* kEditingIdKey = "diaCardEditingId";
constexpr const char* kEditingInputSelector = ".dia-card.is-editing .dia-card-name-input";
constexpr const char* kCardSelector = ".dia-card";
constexpr const char* kCardIdAttribute = "data-card-id";

// 同時に出すカードの上限(画面の中心に近い順)。
constexpr std::size_t kMaxCards = 64;
// 未表示の1フレームだけ使う大きさの目安。寸法はworkspace.rcssの.dia-cardに合わせる。
constexpr double kDefaultCardWidthDp = 96.0;
constexpr double kDefaultCardHeightDp = 32.0;
constexpr double kEdgeMarginDp = 8.0; // = --gap-x
constexpr double kAnchorGapDp = 8.0;  // = --gap-x

// 番線の中ほど(真ん中のレールの中点)。レールが解決できなければnullopt。
std::optional<rs::core::Vec3> PlatformAnchorPoint(const rs::world::TrackGraph& graph,
                                                  const rs::world::PlatformStopInfo& platform) {
    if (platform.railEdges.empty()) return std::nullopt;
    const auto edge = graph.edges.find(platform.railEdges[platform.railEdges.size() / 2]);
    if (edge == graph.edges.end()) return std::nullopt;
    return rs::world::EvaluateAt(edge->second, rs::world::EdgeLength(edge->second) * 0.5, +1).pos;
}

const char* TitleOf(DiaCardKind kind) { return kind == DiaCardKind::Point ? "分岐の画面を開く" : "番線の画面を開く"; }

} // namespace

RmlUiDiaCardView::RmlUiDiaCardView()
    : inlineRename_(kEditingIdKey, kEditingInputSelector), model_(std::make_unique<DiaCardsModel>()) {}
RmlUiDiaCardView::~RmlUiDiaCardView() = default;

void RmlUiDiaCardView::Attach(OpenCard open, RenameCard rename) {
    open_ = std::move(open);
    rename_ = std::move(rename);
}

void RmlUiDiaCardView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<DiaCardModel> handle = constructor.RegisterStruct<DiaCardModel>()) {
        handle.RegisterMember("id", &DiaCardModel::id);
        handle.RegisterMember("name", &DiaCardModel::name);
        handle.RegisterMember("title", &DiaCardModel::title);
        handle.RegisterMember("selected", &DiaCardModel::selected);
        handle.RegisterMember("left", &DiaCardModel::left);
        handle.RegisterMember("top", &DiaCardModel::top);
    }
    constructor.RegisterArray<Rml::Vector<DiaCardModel>>();
    constructor.Bind(kCardsKey, &model_->cards);
    inlineRename_.RegisterModel(constructor);
    constructor.BindEventCallback("open_dia_card",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            event.StopPropagation();
            OnOpen(FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback("begin_rename_dia_card",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            event.StopPropagation();
            OnBeginRename(event, FirstEventArgument<Rml::String>(arguments, Rml::String()));
        });
    constructor.BindEventCallback("commit_dia_card_name",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { OnCommitName(event); });
    constructor.BindEventCallback("key_dia_card_name",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) { inlineRename_.OnKey(event); });
    modelHandle_ = constructor.GetModelHandle();
}

const DiaCardCandidate* RmlUiDiaCardView::FindShown(const std::string& id) const {
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(id);
    if (!guid.has_value()) return nullptr;
    const auto found = std::find_if(shown_.begin(), shown_.end(),
                                    [&guid](const DiaCardCandidate& card) { return card.id == *guid; });
    return found == shown_.end() ? nullptr : &*found;
}

void RmlUiDiaCardView::OnOpen(const std::string& id) {
    // dblclickの直後のclickと入力中のクリックでは、開き直さない。
    if (!open_ || inlineRename_.IsEditing(id)) return;
    if (const DiaCardCandidate* card = FindShown(id)) open_(card->kind, card->id);
}

void RmlUiDiaCardView::OnBeginRename(Rml::Event& event, const std::string& id) {
    const DiaCardCandidate* const card = FindShown(id);
    Rml::Element* const element = event.GetCurrentElement();
    if (!rename_ || card == nullptr || element == nullptr) return;
    if (open_) open_(card->kind, card->id);
    editingKind_ = card->kind;
    inlineRename_.Begin(id, card->id, card->name, element->GetOwnerDocument());
}

void RmlUiDiaCardView::OnCommitName(Rml::Event& event) {
    if (!rename_) return;
    const DiaCardKind kind = editingKind_;
    const std::optional<RmlUiInlineRename::Commit> commit = inlineRename_.OnCommit(event);
    // 空の名前は確定しない。変わっていないときは、取り消し履歴を作らない。
    if (!commit.has_value() || commit->name.empty()) return;
    const DiaCardCandidate* const card = FindShown(rs::world::GuidToHex(commit->target));
    if (card != nullptr && card->name == commit->name) return;
    rename_(kind, commit->target, commit->name);
}

bool RmlUiDiaCardView::TargetExists(const DiaCardKind& kind, const rs::world::Guid& id) const {
    if (editor_ == nullptr) return false;
    if (kind == DiaCardKind::Point) {
        const auto node = editor_->graph.nodes.find(id);
        return node != editor_->graph.nodes.end() && node->second.IsPoint();
    }
    return rs::world::FindPlatformById(editor_->graph, id) != nullptr;
}

void RmlUiDiaCardView::Refresh(bool active, const rs::editor::Editor& editor, Rml::ElementDocument* document) {
    active_ = active;
    editor_ = &editor;
    document_ = document;
    if (!active || (inlineRename_.Active() && !TargetExists(editingKind_, inlineRename_.Target()))) inlineRename_.End();
    inlineRename_.FocusPending(document);
    if (!active) {
        shown_.clear();
        if (!model_->cards.empty() && modelHandle_) {
            model_->cards.clear();
            modelHandle_.DirtyVariable(kCardsKey);
        }
    }
}

void RmlUiDiaCardView::UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight) {
    if (!modelHandle_ || !active_ || editor_ == nullptr) return;
    const rs::world::TrackGraph& graph = editor_->graph;
    std::vector<DiaCardCandidate> candidates;
    const auto add = [&](DiaCardKind kind, const rs::world::Guid& id, const std::string& name, bool selected,
                         const rs::core::Vec3& anchor) {
        candidates.push_back({kind, id, name, selected, ProjectAnchoredPoint(anchor, viewProj)});
    };
    for (const rs::world::PlatformStopInfo& platform : graph.platforms) {
        if (!platform.stoppable) continue;
        const std::optional<rs::core::Vec3> point = PlatformAnchorPoint(graph, platform);
        if (point.has_value()) {
            add(DiaCardKind::Platform, platform.id, platform.name, platform.id == editor_->selectedPlatform, *point);
        }
    }
    for (const auto& [id, node] : graph.nodes) {
        if (!node.IsPoint()) continue;
        add(DiaCardKind::Point, id, dia_builder::PointDisplayName(node), id == editor_->selectedNode,
            editor_->NodeRailTopPos(node));
    }
    shown_ = workspace::ChooseVisibleDiaCards(std::move(candidates), kMaxCards);

    const double dpRatio = document_ != nullptr && document_->GetContext() != nullptr
                               ? document_->GetContext()->GetDensityIndependentPixelRatio() : 1.0;
    const AnchoredElementSizes measured = MeasureAnchoredElementSizes(document_, kCardSelector, kCardIdAttribute);
    const AnchoredArea area = MeasureAnchoredArea(document_, screenWidth, screenHeight);
    Rml::Vector<DiaCardModel> cards;
    cards.reserve(shown_.size());
    for (const DiaCardCandidate& candidate : shown_) {
        DiaCardModel card;
        card.id = rs::world::GuidToHex(candidate.id);
        card.name = candidate.name;
        card.title = TitleOf(candidate.kind);
        card.selected = candidate.selected;
        const auto found = measured.find(card.id);
        const workspace::AnchoredElementPlacement placement = workspace::PlaceAnchoredElement(
            {candidate.clip, screenWidth, screenHeight, area.left, area.top, area.right, area.bottom,
             found != measured.end() ? found->second.width : kDefaultCardWidthDp * dpRatio,
             found != measured.end() ? found->second.height : kDefaultCardHeightDp * dpRatio,
             kEdgeMarginDp * dpRatio, kAnchorGapDp * dpRatio});
        PlaceAnchoredElementPosition(placement, card.left, card.top);
        cards.push_back(std::move(card));
    }
    UpdateVariable(modelHandle_, kCardsKey, model_->cards, cards);
}

} // namespace rs::app::rmlui
