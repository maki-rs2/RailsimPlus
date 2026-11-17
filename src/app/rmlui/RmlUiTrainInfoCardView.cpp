#include "RmlUiTrainInfoCardView.h"

#include <cmath>
#include <string>
#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Traits.h>
#include <RmlUi/Core/Types.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiFormationImages.h"
#include "RmlUiAnchoredArea.h"
#include "RmlUiAnchoredElementPlacement.h"
#include "RmlUiUpdateVariable.h"
#include "../../world/alignment/GuidJson.h"
#include "../action/FormationAction.h"
#include "../ui/FormationPanel.h"
#include "../workspace/TrainInfoCardCatalog.h"
#include "../workspace/AnchoredElementPlacement.h"
#include "../workspace/TrainInfoCardTuning.h"

namespace rs::app::rmlui {

// カード1枚。idは列車のguid(16進)で、ボタンの対象に使う。left/topはカードの左上(コンテキストのpx)。
struct TrainCardModel {
    Rml::String id;
    // 画面に出すか。UpdatePositionsが、先頭車の位置が引けて距離の上限内のときだけ立てる。
    bool shown = false;
    // 折りたたんでいるか。広げているのは1本だけで、ほかは折りたたむ(アイコンと編成名だけを残す)。
    bool collapsed = false;
    Rml::String trainType;
    Rml::String destination;
    Rml::String trainNumber;
    Rml::String name;
    int carCount = 0;
    // 先頭車のアイコンのソース名。車両が無い・画像が届くまでは空。
    Rml::String icon;
    // 目標速度の符号に当たる速度ボタンの名前(kSpeedPresetsのkey)。
    Rml::String speedDirection;
    // 目標速度[km/h]。後退は負。
    int targetKmh = 0;
    // 現在速度の絶対値[km/h]。
    int speedKmh = 0;
    // 運行状態の色クラス(kRunStateStylesのcolorClass)。
    Rml::String speedClass;
    Rml::String left = "0px";
    Rml::String top = "0px";
    bool operator==(const TrainCardModel&) const = default;
};

struct TrainInfoCardsModel {
    Rml::Vector<TrainCardModel> cards;
};

namespace {

constexpr const char* kCardsKey = "trainInfoCards";

// workspace.rmlのクラス名・属性名。
constexpr const char* kCardSelector = ".train-info-card";
constexpr const char* kPressedStepSelector = ".train-card-step:active";
constexpr const char* kTrainIdAttribute = "data-train-id";
constexpr const char* kStepAttribute = "data-step";

// 微調整ボタンを押し続けたときの連続入力。押してから0.4秒待ち、以降は0.1秒ごとに1歩(= 10歩/秒)。
constexpr double kStepRepeatDelaySeconds = 0.4;
constexpr double kStepRepeatIntervalSeconds = 0.1;
constexpr KeyRepeatTiming kStepRepeatTiming{kStepRepeatDelaySeconds, 1.0 / kStepRepeatIntervalSeconds};

// カードの余白・大きさ(dp)。値の出どころはworkspace.rcssの寸法トークン(C++からRCSSの変数は読めないため合わせて書く)。
constexpr float kEdgeMarginDp = 8.0f;       // = --gap-x。動かせる範囲の端からカードまで
constexpr float kAnchorGapDp = 8.0f;        // = --gap-x。先頭車の上面からカードの下端まで
constexpr float kDefaultCardWidthDp = 240.0f;  // = --train-card-width。カードがまだ描かれていない最初の1フレームだけ使う
constexpr float kDefaultCardHeightDp = 190.0f; // カードの高さの目安。同上

std::optional<rs::world::Guid> ParseId(const std::string& id) {
    return rs::world::GuidFromHex(id);
}

void RegisterModelTypes(Rml::DataModelConstructor& constructor) {
    if (Rml::StructHandle<TrainCardModel> handle = constructor.RegisterStruct<TrainCardModel>()) {
        handle.RegisterMember("id", &TrainCardModel::id);
        handle.RegisterMember("shown", &TrainCardModel::shown);
        handle.RegisterMember("collapsed", &TrainCardModel::collapsed);
        handle.RegisterMember("trainType", &TrainCardModel::trainType);
        handle.RegisterMember("destination", &TrainCardModel::destination);
        handle.RegisterMember("trainNumber", &TrainCardModel::trainNumber);
        handle.RegisterMember("name", &TrainCardModel::name);
        handle.RegisterMember("carCount", &TrainCardModel::carCount);
        handle.RegisterMember("icon", &TrainCardModel::icon);
        handle.RegisterMember("speedDirection", &TrainCardModel::speedDirection);
        handle.RegisterMember("targetKmh", &TrainCardModel::targetKmh);
        handle.RegisterMember("speedKmh", &TrainCardModel::speedKmh);
        handle.RegisterMember("speedClass", &TrainCardModel::speedClass);
        handle.RegisterMember("left", &TrainCardModel::left);
        handle.RegisterMember("top", &TrainCardModel::top);
    }
    constructor.RegisterArray<Rml::Vector<TrainCardModel>>();
}

const FormationRowViewModel* FindDef(const FormationViewModel& viewModel, const rs::world::Guid& formationId) {
    for (const FormationRowViewModel& def : viewModel.defs) {
        if (def.guid == formationId) return &def;
    }
    return nullptr;
}

// 配置元の編成が引けない列車(定義が削除済みなど)も、速度の操作と運転席視点のためカードは作る。
// 編成名・両数・アイコンは空のまま(RML側で、編成名が空のときはその段を隠す)。
TrainCardModel BuildCard(const PlacedGroupViewModel& group, const FormationViewModel& viewModel,
                         RmlUiFormationImages& images) {
    TrainCardModel out;
    out.id = rs::world::GuidToHex(group.trainId);
    out.trainType = group.trainType;
    out.destination = group.destination;
    out.trainNumber = group.trainNumber;
    const FormationRowViewModel* def = FindDef(viewModel, group.sourceFormationId);
    if (def != nullptr) {
        out.name = def->name;
        out.carCount = static_cast<int>(def->carCount);
        if (!def->cars.empty()) {
            out.icon = images.WantHeadIcon(def->cars.front().folderUtf8, def->cars.front().reversed);
        }
    }
    out.speedDirection = workspace::GetSpeedPreset(workspace::DirectionOfTarget(group.targetSpeedKmh)).key;
    out.targetKmh = static_cast<int>(std::lround(group.targetSpeedKmh));
    out.speedKmh = static_cast<int>(std::lround(std::abs(group.currentSpeedKmh)));
    out.speedClass = workspace::GetRunStateStyle(group.diaState).colorClass;
    return out;
}

} // namespace

RmlUiTrainInfoCardView::RmlUiTrainInfoCardView() : model_(std::make_unique<TrainInfoCardsModel>()) {}
RmlUiTrainInfoCardView::~RmlUiTrainInfoCardView() = default;

void RmlUiTrainInfoCardView::Attach(const FormationRefs& refs) {
    refs_ = std::make_unique<FormationRefs>(refs);
}

void RmlUiTrainInfoCardView::RegisterModel(Rml::DataModelConstructor& constructor) {
    RegisterModelTypes(constructor);
    constructor.Bind(kCardsKey, &model_->cards);

    constructor.BindEventCallback(
        "set_train_speed", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const std::optional<rs::world::Guid> trainId = ParseId(EventArgumentAt<Rml::String>(arguments, 0, Rml::String()));
            const Rml::String key = EventArgumentAt<Rml::String>(arguments, 1, Rml::String());
            if (!trainId.has_value()) return;
            for (const workspace::SpeedPreset& preset : workspace::kSpeedPresets) {
                if (key == preset.key) FormationAction(*refs_).SetGroupTargetSpeedKmh(*trainId, preset.targetKmh);
            }
        });
    constructor.BindEventCallback(
        "begin_train_speed_step", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
            // 左ボタンだけ。右ボタンなどでは微調整しない。
            if (event.GetParameter<int>("button", 0) != 0) return;
            OnBeginStep(EventArgumentAt<Rml::String>(arguments, 0, Rml::String()), EventArgumentAt<int>(arguments, 1, 0));
        });
    constructor.BindEventCallback(
        "toggle_train_card_fold", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const std::optional<rs::world::Guid> trainId = ParseId(EventArgumentAt<Rml::String>(arguments, 0, Rml::String()));
            if (!trainId.has_value()) return;
            // 広げているカードを押すと閉じ、閉じているカードを押すとそのカードだけを広げる。
            rs::world::TrainId& focused = refs_->uiState.focusedTrainId;
            focused = (focused == *trainId) ? rs::world::NilGuid() : *trainId;
        });
    constructor.BindEventCallback(
        "remove_train_card", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            RequestRemove(EventArgumentAt<Rml::String>(arguments, 0, Rml::String()));
        });
    // 3D画面の編成情報カードのダブルクリックで、その編成を追跡する(列車一覧の配置済みカードと同じ追跡)。
    constructor.BindEventCallback(
        "follow_train", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const std::optional<rs::world::Guid> trainId = ParseId(EventArgumentAt<Rml::String>(arguments, 0, Rml::String()));
            if (trainId.has_value()) FormationAction(*refs_).FollowTrain(*trainId);
        });
    constructor.BindEventCallback(
        "begin_train_cabin_view", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
            if (!refs_) return;
            const std::optional<rs::world::Guid> trainId = ParseId(EventArgumentAt<Rml::String>(arguments, 0, Rml::String()));
            if (trainId.has_value()) FormationAction(*refs_).SetCabinViewActive(*trainId, true);
        });

    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiTrainInfoCardView::RegisterPopups(RmlUiConfirmDialog& dialog) {
    confirmDialog_ = &dialog;
}

void RmlUiTrainInfoCardView::RequestRemove(const std::string& cardId) {
    if (!refs_ || confirmDialog_ == nullptr) return;
    const std::optional<rs::world::Guid> trainId = ParseId(cardId);
    if (!trainId.has_value()) return;
    std::string name;
    for (const TrainCardModel& card : model_->cards) {
        if (card.id == cardId) name = card.name;
    }
    const std::string message = name.empty() ? std::string("この列車を撤去しますか?") : "「" + name + "」を撤去しますか?";
    // 確認の間に列車が消えても困らないよう、確定時はidだけで削除する(無い列車の削除は何も起きない)。
    const rs::world::TrainId target = *trainId;
    confirmDialog_->Open(message, "撤去", true, [this, target]() {
        if (refs_) FormationAction(*refs_).RemoveGroup(target);
    });
}

void RmlUiTrainInfoCardView::OnBeginStep(const std::string& cardId, int direction) {
    if (!refs_ || direction == 0) return;
    const std::optional<rs::world::Guid> trainId = ParseId(cardId);
    if (!trainId.has_value()) return;
    // 押した瞬間の1歩はここで進める(Refreshを待つと、次の更新の前に離した短いクリックが落ちる)。
    // 以降の連続入力は、同じボタンを押している間だけRefreshが進める。
    const HeldStep held{*trainId, direction};
    held_ = held;
    stepper_.Reset();
    ApplySteps(held, stepper_.Update(direction, 0.0, kStepRepeatTiming));
}

void RmlUiTrainInfoCardView::ApplySteps(const HeldStep& held, int steps) {
    if (!refs_ || steps == 0) return;
    // stepsの符号は押したボタンの向きと同じ。
    FormationAction(*refs_).StepGroupTargetSpeedKmh(held.trainId, steps);
}

void RmlUiTrainInfoCardView::AdvanceHeldStep(double deltaSeconds) {
    Rml::Element* const pressed = document_ != nullptr ? document_->QuerySelector(kPressedStepSelector) : nullptr;
    if (pressed == nullptr) {
        held_.reset();
        stepper_.Reset();
        return;
    }
    const std::optional<rs::world::Guid> trainId = ParseId(pressed->GetAttribute<Rml::String>(kTrainIdAttribute, Rml::String()));
    const int direction = pressed->GetAttribute<int>(kStepAttribute, 0);
    if (!trainId.has_value() || direction == 0) return;

    const HeldStep current{*trainId, direction};
    if (!held_.has_value() || *held_ != current) {
        // 押した瞬間のイベントを受けていない(別のボタンへ移った場合など)。新しい押下として1歩から始める。
        held_ = current;
        stepper_.Reset();
        ApplySteps(current, stepper_.Update(direction, 0.0, kStepRepeatTiming));
        return;
    }
    ApplySteps(current, stepper_.Update(direction, deltaSeconds, kStepRepeatTiming));
}

void RmlUiTrainInfoCardView::Refresh(bool active, std::size_t activeSceneIndex, double deltaSeconds,
                                     Rml::ElementDocument* document, RmlUiFormationImages& images) {
    if (!refs_ || !modelHandle_) return;
    document_ = document;
    AdvanceHeldStep(deltaSeconds);

    Rml::Vector<TrainCardModel> cards;
    std::vector<rs::world::Guid> trainIds;
    const Rml::String focusedId = rs::world::GuidToHex(refs_->uiState.focusedTrainId);
    // 列車モードの間だけ編成の値を読む。運転台視点の間は全カードを消し、アクティブSceneにいない列車も出さない。
    const FormationViewModel viewModel = active ? FormationAction(*refs_).ViewModel() : FormationViewModel();
    if (active && !viewModel.cabinViewActive) {
        for (const PlacedGroupViewModel& group : viewModel.placedGroups) {
            if (!group.sceneIndex.has_value() || *group.sceneIndex != activeSceneIndex) continue;
            TrainCardModel card = BuildCard(group, viewModel, images);
            card.collapsed = card.id != focusedId;
            // 位置と表示の有無は前回のUpdatePositionsの結果を引き継ぐ。
            for (const TrainCardModel& previous : model_->cards) {
                if (previous.id != card.id) continue;
                card.shown = previous.shown;
                card.left = previous.left;
                card.top = previous.top;
                break;
            }
            cards.push_back(std::move(card));
            trainIds.push_back(group.trainId);
        }
    }
    trainIds_ = std::move(trainIds);
    UpdateVariable(modelHandle_, kCardsKey, model_->cards, cards);
}

void RmlUiTrainInfoCardView::UpdatePositions(const rs::core::Mat4& viewProj, const rs::core::Vec3& eye,
                                             float screenWidth, float screenHeight) {
    if (!refs_ || !modelHandle_ || model_->cards.empty()) return;
    const float dpRatio = (document_ != nullptr && document_->GetContext() != nullptr)
                              ? document_->GetContext()->GetDensityIndependentPixelRatio()
                              : 1.0f;
    const AnchoredElementSize defaultSize{kDefaultCardWidthDp * dpRatio, kDefaultCardHeightDp * dpRatio};

    // 描かれているカードの大きさ(前のフレームのレイアウト)。まだ描かれていないカードは目安の大きさで置く。
    const AnchoredElementSizes measured = MeasureAnchoredElementSizes(document_, kCardSelector, kTrainIdAttribute);

    // 左右のタブ・上のバナー・下のバーに重ならない範囲。
    const AnchoredArea area = MeasureAnchoredArea(document_, screenWidth, screenHeight);
    const float maxDistanceM = static_cast<float>(workspace::TrainInfoCardTuning::Embedded().maxDistanceM);
    const FormationAction action(*refs_);
    bool changed = false;
    for (std::size_t i = 0; i < model_->cards.size() && i < trainIds_.size(); ++i) {
        TrainCardModel& card = model_->cards[i];
        bool shown = false;
        Rml::String left = card.left;
        Rml::String top = card.top;

        rs::core::Vec3 anchor;
        if (action.ResolveLeadCarAnchor(trainIds_[i], anchor) &&
            workspace::IsWithinCardDistance((anchor - eye).Length(), maxDistanceM)) {
            PlaceMeasuredAnchoredElement(measured, card.id,
                {ProjectAnchoredPoint(anchor, viewProj), screenWidth, screenHeight, area.left, area.top, area.right, area.bottom,
                 defaultSize.width, defaultSize.height,
                 kEdgeMarginDp * dpRatio, kAnchorGapDp * dpRatio}, left, top);
            shown = true;
        }
        if (card.shown != shown || card.left != left || card.top != top) {
            card.shown = shown;
            card.left = std::move(left);
            card.top = std::move(top);
            changed = true;
        }
    }
    if (changed) modelHandle_.DirtyVariable(kCardsKey);
}

} // namespace rs::app::rmlui
