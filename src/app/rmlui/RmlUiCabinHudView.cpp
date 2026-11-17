#include "RmlUiCabinHudView.h"

#include <cmath>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiUpdateVariable.h"
#include "../../world/alignment/GuidJson.h"
#include "../action/FormationAction.h"
#include "../workspace/TrainInfoCardCatalog.h"

namespace rs::app::rmlui {

// 右上パネル1枚ぶん。空の項目はRML側で段ごと隠す。
struct CabinHudModel {
    bool shown = false;
    Rml::String trainType;
    Rml::String destination;
    Rml::String previousStation;
    Rml::String nextStation;
    Rml::String nextPlatform;
    int speedKmh = 0;
    // 速度操作の対象列車(16進)。-/+のボタンが持つ値で、押しっぱなしの連続入力は編成情報カードの処理が受け持つ。
    Rml::String trainId;
    Rml::String speedDirection;
    int targetKmh = 0;
    Rml::String speedClass;
    bool operator==(const CabinHudModel&) const = default;
};

namespace {

constexpr const char* kHudKey = "cabinHud";

} // namespace

RmlUiCabinHudView::RmlUiCabinHudView() : model_(std::make_unique<CabinHudModel>()) {}
RmlUiCabinHudView::~RmlUiCabinHudView() = default;

void RmlUiCabinHudView::Attach(const FormationRefs& refs) {
    refs_ = std::make_unique<FormationRefs>(refs);
}

void RmlUiCabinHudView::RegisterModel(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<CabinHudModel> handle = constructor.RegisterStruct<CabinHudModel>()) {
        handle.RegisterMember("shown", &CabinHudModel::shown);
        handle.RegisterMember("trainType", &CabinHudModel::trainType);
        handle.RegisterMember("destination", &CabinHudModel::destination);
        handle.RegisterMember("previousStation", &CabinHudModel::previousStation);
        handle.RegisterMember("nextStation", &CabinHudModel::nextStation);
        handle.RegisterMember("nextPlatform", &CabinHudModel::nextPlatform);
        handle.RegisterMember("speedKmh", &CabinHudModel::speedKmh);
        handle.RegisterMember("trainId", &CabinHudModel::trainId);
        handle.RegisterMember("speedDirection", &CabinHudModel::speedDirection);
        handle.RegisterMember("targetKmh", &CabinHudModel::targetKmh);
        handle.RegisterMember("speedClass", &CabinHudModel::speedClass);
    }
    constructor.Bind(kHudKey, model_.get());

    // 「戻る」ボタンとEscは同じ経路(FormationAction::LeaveCabinView)を通る。
    constructor.BindEventCallback("leave_cabin_view", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (refs_) FormationAction(*refs_).LeaveCabinView();
    });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiCabinHudView::Refresh() {
    if (!refs_ || !modelHandle_) return;
    const CabinHudViewModel viewModel = FormationAction(*refs_).CabinHud();
    CabinHudModel next;
    next.shown = viewModel.active;
    next.trainType = viewModel.trainType;
    next.destination = viewModel.destination;
    next.previousStation = viewModel.previousStation;
    next.nextStation = viewModel.nextStation;
    next.nextPlatform = viewModel.nextPlatform;
    next.speedKmh = viewModel.speedKmh;
    if (viewModel.active) {
        next.trainId = rs::world::GuidToHex(viewModel.trainId);
        next.speedDirection = workspace::GetSpeedPreset(workspace::DirectionOfTarget(viewModel.targetSpeedKmh)).key;
        next.targetKmh = static_cast<int>(std::lround(viewModel.targetSpeedKmh));
        next.speedClass = workspace::GetRunStateStyle(viewModel.diaState).colorClass;
    }
    UpdateVariable(modelHandle_, kHudKey, *model_, next);
}

} // namespace rs::app::rmlui
