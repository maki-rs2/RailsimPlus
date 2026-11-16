//
// TimeControlAction.h - 下部バーの時間操作(一時停止・倍率・日時)から呼ぶ操作の入口
//
// ゲーム設定と同じGameSettingsActionを通して、ゲーム内時計を書き換える。
//
#pragma once

#include <string_view>

namespace rs::app {
class GameSettingsAction;
class ToastQueue;
namespace time_control_action {
// 倍率を0にする。
void Pause(GameSettingsAction& action);
// 倍率をscaleにする(GameSettingsAction::SetTimeScaleが範囲に丸める)。段の選択と倍率の入力で同じ。
void SetScale(GameSettingsAction& action, double scale);
// 「年/月/日」「時:分」を読んで日時にする。読めない・存在しない日付は理由をトーストで出して何も変えない。
void ChangeDateTime(GameSettingsAction& action, ToastQueue& queue, std::string_view date, std::string_view time);
}
}
