#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rs::app {

enum class ToastSeverity { Info, Warning, Error };

struct ToastTuning {
    double infoSeconds = 5.0;
    double warningSeconds = 7.0;
    double errorSeconds = 9.0;
    std::size_t maxVisible = 4;

    static ToastTuning FromJson(std::string_view json);
    static const ToastTuning& Embedded();
};

// 見た目の種類。重大度とは別の軸で、Update は更新の通知(緑)。
enum class ToastStyle { Default, Update };

// 投稿の付加情報。sticky の通知は時間で消えず、閉じる操作でだけ消える(重大度 Error は指定がなくても sticky)。
// key が空でないものは UpsertKeyed で中身を入れ替えられる(同じキーの通知は1つだけ)。
// closable は閉じる操作(×)を出すか。指定しなければ sticky と同じで、重大度 Error は常に真。
// keepOpenOnAction が真なら、ボタンを押しても通知を閉じない。
struct ToastPostOptions {
    ToastStyle style = ToastStyle::Default;
    bool sticky = false;
    std::string actionLabel;
    std::function<void()> onAction;
    std::string key;
    std::optional<bool> closable;
    bool keepOpenOnAction = false;
};

struct ToastItem {
    ToastSeverity severity = ToastSeverity::Info;
    std::string text;
    // 操作つきの通知のボタンの文言(空なら操作なし)と、押したときの処理。
    std::string actionLabel;
    std::function<void()> onAction;
    ToastStyle style = ToastStyle::Default;
    // 真なら Advance で消えず、Dismiss でだけ消える。
    bool sticky = false;
    std::string key;
    // 真なら×を出す。
    bool closable = false;
    bool keepOpenOnAction = false;
};

class ToastQueue {
public:
    explicit ToastQueue(ToastTuning tuning);

    void Post(ToastSeverity severity, std::string text);
    // ボタンつきの通知。ボタンを押すと onAction が呼ばれ、通知は閉じる。
    void PostWithAction(ToastSeverity severity, std::string text, std::string actionLabel,
                        std::function<void()> onAction);
    // スタイルと sticky を指定できる投稿。同じ重大度・文言が表示中なら、sticky は何もせず、それ以外は表示時間を延ばす。
    void PostWithOptions(ToastSeverity severity, std::string text, ToastPostOptions options);
    // options.key の通知が出ていれば、位置を変えずに中身を入れ替える(同じ中身なら何もせず、Version も上げない)。
    // 無ければ新しく作る(PostedCount を数えるのはこのときだけ)。入れ替えのとき、sticky でなければ残り時間を数え直す。
    // key が空のときは PostWithOptions と同じ。
    void UpsertKeyed(ToastSeverity severity, std::string text, ToastPostOptions options);
    // 同じキーの通知を閉じる。無ければ何もしない。
    void DismissKey(std::string_view key);
    // items[index] の通知を閉じる。範囲外なら何もしない。
    void Dismiss(std::size_t index);
    void Advance(double deltaSeconds);

    const std::vector<ToastItem>& Items() const { return items_; }
    std::size_t Version() const { return version_; }
    // これまでに新しく出した通知の数(重大度ごと)。表示中の同じ内容の再Postは、表示時間を延ばすだけで数えない。
    // 効果音などの通知に使う。
    std::size_t PostedCount(ToastSeverity severity) const { return postedCounts_[static_cast<std::size_t>(severity)]; }

private:
    struct ActiveToast {
        ToastItem item;
        double remainingSeconds = 0.0;
    };

    double Duration(ToastSeverity severity) const;
    void RefreshItems();
    ToastItem MakeItem(ToastSeverity severity, std::string text, ToastPostOptions options) const;
    void AddItem(ToastItem item);

    ToastTuning tuning_;
    std::vector<ActiveToast> activeItems_;
    std::vector<ToastItem> items_;
    std::size_t version_ = 0;
    std::array<std::size_t, 3> postedCounts_{};
};

}  // namespace rs::app
