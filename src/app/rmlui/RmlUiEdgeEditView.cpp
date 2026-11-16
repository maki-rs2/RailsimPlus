#include "RmlUiEdgeEditView.h"

#include <cstdio>
#include <optional>
#include <string>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Traits.h>

#include "RmlUiDataEventArgs.h"
#include "RmlUiFieldCommit.h"
#include "RmlUiNumberParse.h"
#include "RmlUiUpdateVariable.h"
#include "../action/EdgePropertyAction.h"
#include "../action/EdgeSectionEditAction.h"
#include "../action/NodePropertyAction.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginSelector.h"
#include "../../editor/tool/Editor.h"
#include "../../editor/tool/WarpLinkAuthoring.h"

namespace rs::app::rmlui {
struct EdgeEditModel {
    bool visible = false;
    bool multi = false;
    bool mixedSpeed = false;
    bool speedEnabled = false;
    bool hasCantMid = false;
    bool hasTransition = false;
    bool centerline = false;
    bool hasEdge = false;
    bool hasNode = false;
    bool hasWarp = false;
    bool warpPending = false;
    bool mergeAllowed = false;
    bool sectionHasBoundary = false;
    bool splitMode = false;
    bool mixedCantAuto = false;
    bool mixedCantMax = false;
    bool cantAuto = false;
    bool showCantAutoMax = false;
    bool cantAutoDisabled = false;
    std::string heading;
    std::string speed;
    std::string cantMax;
    std::string cantA;
    std::string cantB;
    std::string cantMid;
    std::string cantRatio;
    std::string count;
    std::string transition;
    std::string transitionLength;
    std::string speedPlaceholder;
    std::string cantMaxPlaceholder;
    std::string sectionRange;
    std::string boundaryS;
    std::string elevation;
    std::string nodeConnections;
    std::string warpPartner;
};

RmlUiEdgeEditView::RmlUiEdgeEditView() : model_(std::make_unique<EdgeEditModel>()) {}
RmlUiEdgeEditView::~RmlUiEdgeEditView() = default;
void RmlUiEdgeEditView::Attach(rs::editor::Editor& editor, LayoutSession& session,
                               PluginSelector& selector, rs::editor::WarpLinkAuthoring& warpAuthoring) {
    editor_ = &editor; session_ = &session; selector_ = &selector; warpAuthoring_ = &warpAuthoring;
}
void RmlUiEdgeEditView::RegisterModel(Rml::DataModelConstructor& c) {
    if (Rml::StructHandle<EdgeEditModel> h = c.RegisterStruct<EdgeEditModel>()) {
#define MEMBER(name) h.RegisterMember(#name, &EdgeEditModel::name)
        MEMBER(visible); MEMBER(multi); MEMBER(mixedSpeed); MEMBER(speedEnabled); MEMBER(mixedCantAuto); MEMBER(mixedCantMax);
        MEMBER(hasCantMid); MEMBER(hasTransition);
        MEMBER(hasEdge); MEMBER(sectionHasBoundary); MEMBER(splitMode);
        MEMBER(centerline); MEMBER(hasNode); MEMBER(hasWarp); MEMBER(warpPending); MEMBER(mergeAllowed);
        MEMBER(cantAuto); MEMBER(showCantAutoMax); MEMBER(cantAutoDisabled); MEMBER(heading); MEMBER(speed);
        MEMBER(cantMax); MEMBER(cantA); MEMBER(cantB); MEMBER(cantMid); MEMBER(cantRatio); MEMBER(count);
        MEMBER(transition); MEMBER(transitionLength); MEMBER(speedPlaceholder); MEMBER(cantMaxPlaceholder);
        MEMBER(sectionRange); MEMBER(boundaryS);
        MEMBER(elevation); MEMBER(nodeConnections); MEMBER(warpPartner);
#undef MEMBER
    }
    c.Bind("edgeEdit", model_.get());
    c.BindEventCallback("edge_edit_action", [this](auto, Rml::Event& e, const Rml::VariantList& a) { OnAction(e, a); });
    c.BindEventCallback("focus_edge_elevation", [this](auto, Rml::Event&, const Rml::VariantList&) {
        elevationFocused_ = true;
    });
    modelHandle_ = c.GetModelHandle();
}
void RmlUiEdgeEditView::Refresh() {
    if (!editor_ || !session_ || !selector_ || !warpAuthoring_ || !modelHandle_) return;
    const EdgePropertyViewModel v = EdgePropertyAction(*editor_, *session_, *selector_).ViewModel();
    const EdgeSectionEditViewModel section = EdgeSectionEditAction(*editor_).ViewModel();
    NodePropertyAction nodeAction(*editor_, *warpAuthoring_, *session_);
    nodeAction.AutoConfirmWarpPending();
    const NodePropertyViewModel node = nodeAction.ViewModel();
    EdgeEditModel next;
    auto f = [](float x, const char* fmt) { char b[64]; std::snprintf(b, sizeof(b), fmt, x); return std::string(b); };
    next.visible = true;
    next.hasEdge = v.visible;
    next.centerline = v.isCenterline;
    next.hasNode = node.visible;
    next.hasWarp = node.hasWarpLink;
    next.warpPending = node.hasWarpPending;
    next.mergeAllowed = node.mergeRefusal == rs::world::EdgeMergeRefusal::None;
    next.multi = v.selectedCount > 1;
    next.mixedSpeed = v.speedLimitMixed;
    next.speedEnabled = v.speedLimitMixed || v.speedLimit >= 0.0f;
    next.hasCantMid = v.hasCantMid;
    next.hasTransition = v.hasTransitionPlan;
    next.splitMode = editor_->sectionSplitMode;
    next.mixedCantAuto = v.cantAutoMixed;
    next.mixedCantMax = v.cantAutoMaxMixed;
    next.cantAuto = v.cantAuto;
    next.showCantAutoMax = v.cantAuto || v.cantAutoMixed;
    next.cantAutoDisabled = !v.cantAuto && !v.cantAutoMixed;
    next.heading = v.isCenterline ? "複線中心線" : (next.multi ? "選択中のレール" : "レール");
    next.count = std::to_string(v.selectedCount);
    next.elevation = f(node.elevation, "%.2f");
    next.nodeConnections = std::to_string(node.linkCount);
    next.warpPartner = node.hasWarpLink ? std::to_string(node.warpPartnerScene + 1) : std::string();
    if (section.visible && section.hasSpline && section.selectedIndex < section.rows.size()) {
        const EdgeSectionRowViewModel& row = section.rows[section.selectedIndex];
        next.sectionRange = f(static_cast<float>(row.sStart), "%.2f") + "–" +
                            f(static_cast<float>(row.sEnd), "%.2f") + " m";
        next.sectionHasBoundary = section.hasSelectedBoundary;
        next.boundaryS = f(static_cast<float>(section.selectedBoundaryS), "%.2f");
    }
    next.speed = v.speedLimitMixed ? "" : f(v.speedLimit, "%.3f");
    next.speedPlaceholder = v.speedLimitMixed ? "(複数の値)" : "";
    next.cantMax = v.cantAutoMaxMixed ? "" : f(static_cast<float>(v.CantToMm(v.cantAutoMax)), "%.3f");
    next.cantMaxPlaceholder = v.cantAutoMaxMixed ? "(複数の値)" : "";
    next.cantA = f(static_cast<float>(v.CantToMm(v.cantA)), "%.3f");
    next.cantB = f(static_cast<float>(v.CantToMm(v.cantB)), "%.3f");
    next.cantMid = f(static_cast<float>(v.CantToMm(v.cantMid)), "%.3f");
    next.cantRatio = f(v.cantTransitionRatio, "%.3f");
    next.transition = std::to_string(v.transitionComboIndex); next.transitionLength = f(v.transitionLength, "%.1f");
    // 変数名はトップレベルの"edgeEdit"だけをDirtyにできる(ドット入りの名前は通知が届かない)。
    bool changed = false;
#define UPDATE(name) if (!(model_->name == next.name)) { model_->name = next.name; changed = true; }
    UPDATE(visible); UPDATE(hasEdge); UPDATE(sectionHasBoundary); UPDATE(splitMode);
    UPDATE(centerline); UPDATE(hasNode); UPDATE(hasWarp); UPDATE(warpPending); UPDATE(mergeAllowed);
    UPDATE(multi); UPDATE(mixedSpeed); UPDATE(speedEnabled); UPDATE(mixedCantAuto); UPDATE(mixedCantMax);
    UPDATE(cantAuto); UPDATE(showCantAutoMax); UPDATE(cantAutoDisabled); UPDATE(heading); UPDATE(speed);
    UPDATE(cantMax); UPDATE(cantA); UPDATE(cantB); UPDATE(cantMid); UPDATE(cantRatio); UPDATE(count);
    UPDATE(transition); UPDATE(transitionLength); UPDATE(speedPlaceholder); UPDATE(cantMaxPlaceholder);
    UPDATE(sectionRange); UPDATE(boundaryS);
    if (!elevationFocused_) UPDATE(elevation);
    UPDATE(nodeConnections); UPDATE(warpPartner);
#undef UPDATE
    if (changed) modelHandle_.DirtyVariable("edgeEdit");
}
void RmlUiEdgeEditView::OnAction(Rml::Event& event, const Rml::VariantList& args) {
    if (!editor_ || !session_ || !selector_ || !warpAuthoring_) return;
    const std::string key = FirstEventArgument<Rml::String>(args, Rml::String());
    EdgePropertyAction action(*editor_, *session_, *selector_);
    EdgeSectionEditAction sectionAction(*editor_);
    NodePropertyAction nodeAction(*editor_, *warpAuthoring_, *session_);
    const EdgePropertyViewModel initial = action.ViewModel();
    Rml::Element* element = event.GetCurrentElement();
    // 標高の欄はフォーカス中だけ表示の更新を止めている。確定(blur)で止めるのをやめる。
    if (key == "elevation") elevationFocused_ = false;
    // Esc で取り消した欄は、入力前の値に戻っているので確定しない。
    if (ConsumeFieldCancel(element)) return;
    auto value = [&]() -> std::string {
        Rml::ElementFormControl* control = element ? rmlui_dynamic_cast<Rml::ElementFormControl*>(element) : nullptr;
        return control ? control->GetValue() : std::string();
    };
    if (key == "split_toggle") sectionAction.SetSplitMode(!editor_->sectionSplitMode);
        else if (key == "remove_boundary") sectionAction.RemoveSelectedBoundary();
        else if (key == "warp_begin") nodeAction.BeginWarpPending();
        else if (key == "warp_cancel") nodeAction.CancelWarpPending();
        else if (key == "warp_remove") nodeAction.RemoveWarp();
        else if (key == "merge") nodeAction.MergeEdges();
        else if (key == "elevation") {
            float parsed = 0.0f;
            if (ParseFloatText(value(), parsed)) editor_->SetSelectedNodeElevation(parsed);
        }
        else if (key == "boundary") {
            float parsed = 0.0f;
            if (ParseFloatText(value(), parsed)) sectionAction.MoveSelectedBoundary(parsed);
        }
        else if (key == "delete") action.RemoveSelectedEdge();
        else if (key == "speed") {
            float parsed = 0.0f;
            if (ParseFloatText(value(), parsed)) action.SetSpeedLimit(parsed);
        }
        else if (key == "speed_toggle") {
            const bool enabled = element && element->HasAttribute("checked");
            action.SetSpeedLimit(enabled ? 0.0f : -1.0f);
        }
        else if (key == "cant_auto") action.SetCantAuto(element && element->HasAttribute("checked"));
        else if (key == "cant_max") {
            float parsed = 0.0f;
            if (ParseFloatText(value(), parsed)) action.SetCantAutoMax(static_cast<float>(initial.CantFromMm(parsed)));
        } else if (key == "cant_a" || key == "cant_b" || key == "cant_mid" || key == "cant_ratio") {
            float parsed = 0.0f;
            if (ParseFloatText(value(), parsed)) {
                const float a = key == "cant_a" ? static_cast<float>(initial.CantFromMm(parsed)) : initial.cantA;
                const float b = key == "cant_b" ? static_cast<float>(initial.CantFromMm(parsed)) : initial.cantB;
                std::optional<float> mid = initial.hasCantMid ? std::optional<float>(initial.cantMid) : std::nullopt;
                std::optional<float> ratio = initial.cantTransitionRatio > 0.0f
                    ? std::optional<float>(initial.cantTransitionRatio) : std::nullopt;
                if (key == "cant_mid") mid = static_cast<float>(initial.CantFromMm(parsed));
                if (key == "cant_ratio") ratio = parsed;
                action.SetCant(a, b, mid, ratio);
            }
        } else if (key == "transition") {
            int index = 0;
            float length = 0.0f;
            if (ParseIntText(value(), index)) action.SetTransition(index, initial.transitionLength);
            else if (ParseFloatText(value(), length)) action.SetTransition(initial.transitionComboIndex, length);
        } else if (key == "transition_length") {
            float length = 0.0f;
            if (ParseFloatText(value(), length)) action.SetTransition(initial.transitionComboIndex, length);
        }
    Refresh();
}
}
