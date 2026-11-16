//
// RmlUiSignalPanelView.h - 信号モードの「信号の設定」カードをデータモデル"workspace"へ結ぶ
//
// 信号ごとのカードを、その信号の3D位置へ追従させて出す(閉塞ラベルと同じ配置の仕組み)。カードを押すと選び、
// 選んだカードだけ種別・向き・現示・強制R・段の編集を広げる。強制Rの切り替えは信号のguidで実際の信号へ書き、
// 種別・向き・段・削除はSignalPanelActionが「この操作は使えない」という通知を出す。同じデータモデルへ変数とイベントを足す。
//
#pragma once

#include <functional>
#include <optional>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../core/Math.h"
#include "../../world/alignment/TrackGraph.h"
#include "../workspace/SignalPanelPresentation.h"

namespace Rml {
class ElementDocument;
class Event;
} // namespace Rml
namespace rs::world {
class WarpLinkTable;
} // namespace rs::world
namespace rs::app {
class SignalBlockAction;
class ToastQueue;
} // namespace rs::app

namespace rs::app::rmlui {

struct AnchoredArea;

class RmlUiSignalPanelView {
public:
    explicit RmlUiSignalPanelView(ToastQueue& queue);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 書き込み先を呼び出し側のActionへつなぐ。createBlockは3D上の「閉塞にする」(選んでいるレールから閉塞を作る)、
    // withActionは渡した関数へ信号のActionを貸して呼ぶ(信号ごとの操作はこれを通す)。
    // 向きの変更の検証に使う、ワープのつながりと表示中のシーン番号も一緒に渡す。
    using SignalActionFn = std::function<void(rs::app::SignalBlockAction&, const rs::world::WarpLinkTable*, std::size_t)>;
    void Attach(std::function<void()> createBlock, std::function<void(const SignalActionFn&)> withAction) {
        createBlock_ = std::move(createBlock);
        withAction_ = std::move(withAction);
    }
    // 毎フレーム呼ぶ。activeは信号モードのとき。cardsは実際の信号から作ったカードの一覧(activeでなければ使わない)で、
    // 表示中の一覧と中身が変わったときだけ入れ替える。
    // blockAnchorは、今の選択から閉塞を作れるときの、ボタンを置くレール(edgeとs)。作れないときは値なし。
    struct BlockAnchor {
        rs::world::Guid edge;
        double s = 0.0;
    };
    void Refresh(bool active, const rs::world::TrackGraph& graph, const std::optional<BlockAnchor>& blockAnchor,
                 Rml::ElementDocument* document, std::vector<workspace::SignalPanelSignal> cards);
    // 毎フレーム、カメラが決まったあとに呼び、カードを信号の画面上の位置へ置く。
    void UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight);

private:
    bool UpdateBlockButton(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight,
                           const AnchoredArea& area, double dpRatio);
    void OnUi(Rml::Event& event, const Rml::VariantList& arguments);
    void OnAction(Rml::Event& event, const Rml::VariantList& arguments);

    ToastQueue& queue_;
    workspace::SignalPanelPresentation presentation_;
    Rml::DataModelHandle modelHandle_;
    const rs::world::TrackGraph* graph_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    std::function<void()> createBlock_;
    std::function<void(const SignalActionFn&)> withAction_;
};

} // namespace rs::app::rmlui
