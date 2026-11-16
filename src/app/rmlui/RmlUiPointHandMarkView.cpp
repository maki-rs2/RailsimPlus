#include "RmlUiPointHandMarkView.h"

#include <optional>
#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "RmlUiAnchoredArea.h"
#include "RmlUiAnchoredElementPlacement.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiUpdateVariable.h"
#include "../ui/OverlayLineTuning.h"
#include "../../editor/tool/Editor.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app::rmlui {

struct PointHandMarkModel {
    Rml::String id;
    Rml::String node; // 切り替える分岐のguid
    Rml::String label;
    int link = 0;
    bool selected = false;
    Rml::String left;
    Rml::String top;
    bool operator==(const PointHandMarkModel&) const = default;
};

struct PointHandMarksModel {
    Rml::Vector<PointHandMarkModel> marks;
};

namespace {

constexpr const char* kMarksKey = "pointHandMarks";
constexpr const char* kMarkSelector = ".point-hand-mark";
constexpr const char* kMarkIdAttribute = "data-mark-id";

// 未表示の1フレームだけ使う大きさの目安。寸法はworkspace.rcssの.point-hand-markに合わせる。
constexpr double kDefaultMarkWidthDp = 48.0;
constexpr double kDefaultMarkHeightDp = 32.0;
constexpr double kEdgeMarginDp = 8.0; // = --gap-x
constexpr double kAnchorGapDp = 8.0;  // = --gap-x

} // namespace

RmlUiPointHandMarkView::RmlUiPointHandMarkView() : model_(std::make_unique<PointHandMarksModel>()) {}
RmlUiPointHandMarkView::~RmlUiPointHandMarkView() = default;

void RmlUiPointHandMarkView::Attach(ChangeOpening changeOpening) { changeOpening_ = std::move(changeOpening); }

void RmlUiPointHandMarkView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<PointHandMarkModel> handle = constructor.RegisterStruct<PointHandMarkModel>()) {
        handle.RegisterMember("id", &PointHandMarkModel::id);
        handle.RegisterMember("node", &PointHandMarkModel::node);
        handle.RegisterMember("label", &PointHandMarkModel::label);
        handle.RegisterMember("link", &PointHandMarkModel::link);
        handle.RegisterMember("selected", &PointHandMarkModel::selected);
        handle.RegisterMember("left", &PointHandMarkModel::left);
        handle.RegisterMember("top", &PointHandMarkModel::top);
    }
    constructor.RegisterArray<Rml::Vector<PointHandMarkModel>>();
    constructor.Bind(kMarksKey, &model_->marks);
    constructor.BindEventCallback("change_point_hand",
        [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            event.StopPropagation();
            if (!changeOpening_) return;
            const std::optional<rs::world::Guid> node =
                rs::world::GuidFromHex(FirstEventArgument<Rml::String>(arguments, Rml::String()));
            const int link = EventArgumentAt<int>(arguments, 1, -1);
            if (node.has_value() && link >= 0) changeOpening_(*node, link);
        });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiPointHandMarkView::Refresh(bool active, const rs::editor::Editor& editor, Rml::ElementDocument* document) {
    document_ = document;
    marks_.clear();
    node_ = rs::world::NilGuid();
    if (active) {
        const auto found = editor.graph.nodes.find(editor.selectedNode);
        if (found != editor.graph.nodes.end() && found->second.IsPoint()) {
            node_ = found->first;
            marks_ = workspace::BuildPointHandMarks(editor.graph, found->second,
                                                    OverlayLineTuning::Embedded().pointHandMarkDistanceM);
        }
    }
    if (marks_.empty() && !model_->marks.empty() && modelHandle_) {
        model_->marks.clear();
        modelHandle_.DirtyVariable(kMarksKey);
    }
}

void RmlUiPointHandMarkView::UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight) {
    if (!modelHandle_ || marks_.empty()) return;
    const double dpRatio = document_ != nullptr && document_->GetContext() != nullptr
                               ? document_->GetContext()->GetDensityIndependentPixelRatio() : 1.0;
    const AnchoredElementSizes measured = MeasureAnchoredElementSizes(document_, kMarkSelector, kMarkIdAttribute);
    const AnchoredArea area = MeasureAnchoredArea(document_, screenWidth, screenHeight);
    Rml::Vector<PointHandMarkModel> marks;
    marks.reserve(marks_.size());
    for (const workspace::PointHandMark& mark : marks_) {
        PointHandMarkModel model;
        model.id = std::to_string(mark.linkIndex);
        model.node = rs::world::GuidToHex(node_);
        model.label = mark.label;
        model.link = mark.linkIndex;
        model.selected = mark.active;
        const auto found = measured.find(model.id);
        const workspace::AnchoredElementPlacement placement = workspace::PlaceAnchoredElement(
            {ProjectAnchoredPoint(mark.position, viewProj), screenWidth, screenHeight, area.left, area.top, area.right,
             area.bottom, found != measured.end() ? found->second.width : kDefaultMarkWidthDp * dpRatio,
             found != measured.end() ? found->second.height : kDefaultMarkHeightDp * dpRatio, kEdgeMarginDp * dpRatio,
             kAnchorGapDp * dpRatio});
        PlaceAnchoredElementPosition(placement, model.left, model.top);
        marks.push_back(std::move(model));
    }
    UpdateVariable(modelHandle_, kMarksKey, model_->marks, marks);
}

} // namespace rs::app::rmlui
