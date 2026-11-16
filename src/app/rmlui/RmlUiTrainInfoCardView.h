//
// RmlUiTrainInfoCardView.h - 列車モードの編成情報カード(先頭車の画面上に追従するカード)をデータモデル"workspace"へ結ぶ
//
// 配置済みの全編成に1枚ずつ出す。内容は種別・行先・運用番号、先頭車アイコン・編成名・両数、運転席視点ボタン、
// 速度ボタン(後退・停止・前進)、微調整(−/+、押しっぱなしで連続)、現在速度(運行状態は色)。
// 値の読み書きは編成の操作(FormationAction)を通す。RmlUiWorkspaceViewがメンバとして持ち、
// CreateDataModelの中でRegisterModelを呼んで同じデータモデルへ変数とイベントを足す(別のdata-modelは作らない)。
//
// 更新は2段に分ける。文字・色などカメラに依存しない値はRefresh(WorkspaceEditorBridge::Tick)で、
// 画面上の位置と表示の有無(距離による省略を含む)はカメラ確定後にUpdatePositionsで求める。
// どちらも前回と比べて変わった変数だけDirtyVariableする(DirtyAllVariablesは呼ばない)。
//
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"
#include "../action/KeyRepeatStepper.h"

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app {
struct FormationRefs;
} // namespace rs::app

namespace rs::app::rmlui {

struct TrainInfoCardsModel;
class RmlUiConfirmDialog;
class RmlUiFormationImages;

class RmlUiTrainInfoCardView {
public:
    RmlUiTrainInfoCardView();
    ~RmlUiTrainInfoCardView();
    RmlUiTrainInfoCardView(const RmlUiTrainInfoCardView&) = delete;
    RmlUiTrainInfoCardView& operator=(const RmlUiTrainInfoCardView&) = delete;

    // 編成の操作の対象を渡す(呼び出し側が所有し、このビューより長く生きること)。
    void Attach(const FormationRefs& refs);

    // 列車の削除の確認ダイアログを結ぶ(呼び出し側が所有し、このビューより長く生きること)。
    void RegisterPopups(RmlUiConfirmDialog& dialog);

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼び、変数とイベントを登録する。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 毎フレーム呼ぶ。activeは「列車モードのとき」。運転台視点が有効な間と、アクティブSceneにいない編成は
    // カードを作らない。imagesは編成リストと共有する画像で、呼び出し側が1フレームに1回だけBeginFrame/EndFrameで
    // 囲む(activeがtrueのときは囲みの中で呼ぶ)。documentは文書(押している微調整ボタンと、カードの大きさを引く)。
    // 位置と表示の有無は前回UpdatePositionsの結果を引き継ぐ。Attach前は何もしない。
    void Refresh(bool active, std::size_t activeSceneIndex, double deltaSeconds, Rml::ElementDocument* document,
                 RmlUiFormationImages& images);

    // カメラ確定後に呼ぶ。各カードの先頭車の上の点をviewProjで画面へ投影し、位置(画面端へのクランプを含む)と、
    // カメラからの距離による省略を決める。screenWidth/Heightはビューポートの大きさ[px](RmlUiのコンテキストと同じ)。
    void UpdatePositions(const rs::core::Mat4& viewProj, const rs::core::Vec3& eye, float screenWidth,
                         float screenHeight);

private:
    // 押している微調整ボタン(列車と向き)。
    struct HeldStep {
        rs::world::Guid trainId = rs::world::NilGuid();
        int direction = 0;
        bool operator==(const HeldStep&) const = default;
    };

    // 押した瞬間の1歩。以降の連続入力はRefreshが押している間だけ進める。
    void OnBeginStep(const std::string& cardId, int direction);
    // 押している微調整ボタンをドキュメントから引き、押している間の連続入力を進める。
    void AdvanceHeldStep(double deltaSeconds);
    void ApplySteps(const HeldStep& held, int steps);
    // 確認ダイアログを開く。確定したら列車を削除する。
    void RequestRemove(const std::string& cardId);

    std::unique_ptr<FormationRefs> refs_;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<TrainInfoCardsModel> model_;
    std::vector<rs::world::Guid> trainIds_; // model_->cardsと同じ並びの列車(TrainIdはGuid)
    RmlUiConfirmDialog* confirmDialog_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    KeyRepeatStepper stepper_;
    std::optional<HeldStep> held_;
};

} // namespace rs::app::rmlui
