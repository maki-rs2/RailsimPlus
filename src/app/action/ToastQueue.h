#pragma once

#include <array>
#include <cstddef>
#include <functional>
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

struct ToastItem {
    ToastSeverity severity = ToastSeverity::Info;
    std::string text;
    // 操作つきの通知のボタンの文言(空なら操作なし)と、押したときの処理。
    std::string actionLabel;
    std::function<void()> onAction;
};

class ToastQueue {
public:
    explicit ToastQueue(ToastTuning tuning);

    void Post(ToastSeverity severity, std::string text);
    // ボタンつきの通知。ボタンを押すと onAction が呼ばれ、通知は閉じる。
    void PostWithAction(ToastSeverity severity, std::string text, std::string actionLabel,
                        std::function<void()> onAction);
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

    ToastTuning tuning_;
    std::vector<ActiveToast> activeItems_;
    std::vector<ToastItem> items_;
    std::size_t version_ = 0;
    std::array<std::size_t, 3> postedCounts_{};
};

}  // namespace rs::app
