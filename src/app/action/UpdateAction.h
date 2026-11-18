//
// UpdateAction.h - アプリ内更新の操作(ダウンロード・中止・再起動して更新)。
//
// 設定画面のボタンとトーストのボタンが、同じ入口を通るようにするための小さな束。
// 渡すものはどれも呼び出し側が持ち、このオブジェクトより長く生きること。
//
#pragma once

#include <functional>

namespace rs::app {

class UpdateChecker;
class UpdateDownloader;

class UpdateAction {
public:
    // requestRestart は、未保存の確認つきの終了を「更新のため」として頼む関数。nullptr(または空)なら再起動はしない。
    UpdateAction(UpdateChecker* checker, UpdateDownloader* downloader, const std::function<void()>* requestRestart)
        : checker_(checker), downloader_(downloader), requestRestart_(requestRestart) {}

    // 新しい版があるときだけ働く。アプリ内更新が使えなければ、ダウンロードページを開く。
    void Download();
    void Cancel();
    // 準備ができているとき(Ready)だけ、終了の流れを始める。
    void Restart();

private:
    UpdateChecker* checker_;
    UpdateDownloader* downloader_;
    const std::function<void()>* requestRestart_;
};

} // namespace rs::app
