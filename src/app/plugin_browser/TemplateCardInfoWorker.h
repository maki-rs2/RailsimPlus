//
// TemplateCardInfoWorker.h - テンプレートのカードの両数・警告・画像のキーを、ワーカースレッドで求める
//
// 描画スレッドが一覧を読み直すたびに Begin で新しい「回」を始め、行ごとに Post する。結果は Drain で受け取り、
// 古い回の結果と、プラグインの読込の世代が変わったあとの結果は捨てる(捨てた行は、呼び出し側が読み直したときに出し直す)。
// 計算結果は、ファイルの更新時刻と世代をキーにして使い回す。画像を撮るワーカーとは別のスレッドで動く(撮影の順番待ちに混ぜない)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../platform/thread/WorkerThread.h"
#include "TemplateCardInfo.h"

namespace rs::app::plugin_browser {

struct TemplateCardInfoRequest {
    std::uint64_t round = 0;
    std::size_t row = 0;
    std::uint64_t generation = 0; // 要求したときのプラグインの読込の世代
    TemplateImageSource source;
};

struct TemplateCardInfoResult {
    std::uint64_t round = 0;
    std::size_t row = 0;
    std::uint64_t generation = 0;
    TemplateCardInfo info;
};

class TemplateCardInfoWorker {
public:
    // parse が空なら、本物の ParseTrainGroupTemplateFile を使う。
    explicit TemplateCardInfoWorker(TemplateParseFunc parse = {});

    // 新しい回を始める。処理を始めていない古い要求は捨て、返した番号の結果だけが Drain で届く。
    std::uint64_t Begin();
    void Post(std::uint64_t round, std::size_t row, const TemplateImageSource& source, std::uint64_t generation);
    // 現在の回かつ generation が同じ結果を、出来上がった順に返す。それ以外の結果は捨てる。
    std::vector<TemplateCardInfoResult> Drain(std::uint64_t generation);
    // 受付が空で処理中でもないとき true(出口に未回収の結果があっても true)。
    bool Idle() const { return worker_.Idle(); }

private:
    TemplateCardInfoResult Process(const TemplateCardInfoRequest& request) const;

    TemplateParseFunc parse_;
    mutable TemplateCardInfoCache cache_;
    std::uint64_t round_ = 0; // 描画スレッドだけが触る
    // ワーカーは他のメンバが揃ってから起動するため、最後に置く。
    rs::platform_thread::WorkerThread<TemplateCardInfoRequest, TemplateCardInfoResult> worker_;
};

} // namespace rs::app::plugin_browser
