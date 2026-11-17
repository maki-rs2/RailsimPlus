//
// SignalPanelPresentation.h - 信号モードの「信号の設定」カードの表示値(RmlUiには依存しない)
//
// 信号ごとのカードを、その信号の3D位置へ追従させて出す。カードの中身は実際の信号
// (TrackGraph::speedSignals)と現示の状態から作る(action層のBuildSignalPanelSignals)。
// データを変える操作は action層のSignalPanelActionの名前付きの関数が受ける。
//
#pragma once

#include <string>
#include <vector>

#include "../../world/alignment/TrackGraph.h"

namespace rs::app::workspace {

struct SignalPanelStage {
    std::string speed_; // 段の速度の文字(例: 45)
    bool removable_ = false; // 先頭の0の段は外せない
    bool operator==(const SignalPanelStage&) const = default;
};

struct SignalPanelSignal {
    std::string id_;        // 信号のguid(16進)。カードの測定と選択の引き当てに使う
    std::string name_;
    std::string direction_; // "forward"・"reverse"
    std::string aspectText_;
    bool forcedRed_ = false;
    bool selected_ = false;
    std::vector<SignalPanelStage> stages_;
    std::vector<std::string> addableSpeeds_; // 足せる速度(速度表にあってまだ無いもの)
    // カードを追従させる位置(信号のedgeとs)。
    rs::world::Guid edge_ = rs::world::NilGuid();
    double s_ = 0.0;
    // 画面上の位置(毎フレームの配置の結果。"123px"の形)。画面の外・後ろならshown_がfalse。
    bool shown_ = false;
    std::string left_ = "0px";
    std::string top_ = "0px";
};

struct SignalPanelPresentation {
    bool visible_ = false;
    bool canCreateBlock_ = false; // 今の選択から閉塞を作れるか(3D上の「閉塞にする」ボタンを出すか)
    // ボタンを追従させる位置(選んだレールのedgeとs)と、画面上の位置(信号のカードと同じ"123px"の形)。
    rs::world::Guid blockEdge_ = rs::world::NilGuid();
    double blockS_ = 0.0;
    bool blockButtonShown_ = false;
    std::string blockButtonLeft_ = "0px";
    std::string blockButtonTop_ = "0px";
    std::vector<SignalPanelSignal> signals_;

    // 信号を1つ選ぶ(同じ信号をもう一度押すと選択を外す)。範囲外は何もしない。
    void Select(int index);
    // 作り直した一覧に入れ替える。選択と画面上の位置はidで引き継ぐ。中身が同じなら何もせずfalse。
    bool Sync(std::vector<SignalPanelSignal> built);
};

} // namespace rs::app::workspace
