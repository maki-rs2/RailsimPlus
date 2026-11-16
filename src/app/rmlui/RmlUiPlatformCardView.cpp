#include "RmlUiPlatformCardView.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "RmlUiAnchoredArea.h"
#include "RmlUiAnchoredElementPlacement.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/TrackGraph.h"
#include "../../world/alignment/TrackTraversal.h"

namespace rs::app::rmlui {

struct PlatformCardModel {
    Rml::String id;
    Rml::String name;
    bool selected = false;
    Rml::String left;
    Rml::String top;
    bool operator==(const PlatformCardModel&) const = default;
};

struct PlatformCardsModel {
    Rml::Vector<PlatformCardModel> cards;
};

namespace {

constexpr const char* kCardsKey = "platformCards";
constexpr const char* kCardSelector = ".platform-card";
constexpr const char* kPlatformIdAttribute = "data-platform-id";

// 同時に出すカードの上限(画面の中心に近い順)。
constexpr std::size_t kMaxCards = 64;
// 未表示の1フレームだけ使う大きさの目安。寸法はworkspace.rcssの.platform-cardに合わせる。
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

struct Candidate {
    const rs::world::PlatformStopInfo* platform = nullptr;
    workspace::ClipPoint clip;
    double distanceFromCenter = 0.0;
};

} // namespace

RmlUiPlatformCardView::RmlUiPlatformCardView() : model_(std::make_unique<PlatformCardsModel>()) {}
RmlUiPlatformCardView::~RmlUiPlatformCardView() = default;

void RmlUiPlatformCardView::Attach(std::function<void(rs::world::Guid)> openPlatform) {
    openPlatform_ = std::move(openPlatform);
}

void RmlUiPlatformCardView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<PlatformCardModel> handle = constructor.RegisterStruct<PlatformCardModel>()) {
        handle.RegisterMember("id", &PlatformCardModel::id);
        handle.RegisterMember("name", &PlatformCardModel::name);
        handle.RegisterMember("selected", &PlatformCardModel::selected);
        handle.RegisterMember("left", &PlatformCardModel::left);
        handle.RegisterMember("top", &PlatformCardModel::top);
    }
    constructor.RegisterArray<Rml::Vector<PlatformCardModel>>();
    constructor.Bind(kCardsKey, &model_->cards);
    constructor.BindEventCallback("open_platform_card",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            event.StopPropagation();
            if (!openPlatform_) return;
            const std::optional<rs::world::Guid> guid =
                rs::world::GuidFromHex(FirstEventArgument<Rml::String>(arguments, Rml::String()));
            if (guid.has_value()) openPlatform_(*guid);
        });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiPlatformCardView::Refresh(bool active, const rs::world::TrackGraph& graph,
                                    const rs::world::Guid& selectedPlatform, Rml::ElementDocument* document) {
    active_ = active;
    graph_ = &graph;
    selected_ = selectedPlatform;
    document_ = document;
    if (!active && !model_->cards.empty() && modelHandle_) {
        model_->cards.clear();
        modelHandle_.DirtyVariable(kCardsKey);
    }
}

void RmlUiPlatformCardView::UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight) {
    if (!modelHandle_ || !active_ || graph_ == nullptr) return;
    std::vector<Candidate> candidates;
    for (const rs::world::PlatformStopInfo& platform : graph_->platforms) {
        if (!platform.stoppable) continue;
        const std::optional<rs::core::Vec3> point = PlatformAnchorPoint(*graph_, platform);
        if (!point.has_value()) continue;
        const workspace::ClipPoint clip = ProjectAnchoredPoint(*point, viewProj);
        // カメラの後ろと画面の外の番線にはカードを出さない。
        if (clip.w <= workspace::kClipFrontMinW || std::abs(clip.x) > clip.w || std::abs(clip.y) > clip.w) continue;
        candidates.push_back({&platform, clip, std::hypot(clip.x / clip.w, clip.y / clip.w)});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.distanceFromCenter < b.distanceFromCenter; });
    if (candidates.size() > kMaxCards) candidates.resize(kMaxCards);

    const double dpRatio = document_ != nullptr && document_->GetContext() != nullptr
                               ? document_->GetContext()->GetDensityIndependentPixelRatio() : 1.0;
    const AnchoredElementSizes measured = MeasureAnchoredElementSizes(document_, kCardSelector, kPlatformIdAttribute);
    const AnchoredArea area = MeasureAnchoredArea(document_, screenWidth, screenHeight);
    Rml::Vector<PlatformCardModel> cards;
    cards.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        PlatformCardModel card;
        card.id = rs::world::GuidToHex(candidate.platform->id);
        card.name = candidate.platform->name;
        card.selected = candidate.platform->id == selected_;
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
