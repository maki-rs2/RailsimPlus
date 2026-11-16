// BatchResidencyEviction.h - GPUインスタンス描画バッチの常駐上限判定(純粋ロジック)。
//
// Scene切替で一時的に非表示になった静的シーナリーのGPUバッチを即座に破棄せず
// park(GPUリソースは保持したまま配置数0で待機)させ、常駐数が上限を超えた分だけ
// 最も長くliveでないものから実破棄する運用(TrainInstanceBatchManager::
// PruneUnusedCapped)で使う。
// 「どのキーを破棄すべきか」の決定だけを担い、実際のGPUリソース解放
// (SrvRangeAllocator::Free等)は呼び出し側が行う。ID3D12Deviceに一切依存しないため
// Catch2で直接検証できる(SrvRangeAllocator.hと同じ「GPU非依存ロジックを切り出して
// テスト可能にする」方針)。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rs::render {

class BatchResidencyEviction {
public:
    // lastLiveTickByKey: 各バッチキーの「最後にliveだった時刻」(呼び出し側が管理する
    // 単調増加カウンタの値)。
    // liveKeys: 現在アクティブな(破棄してはいけない)キー集合。
    // maxResidentBatches: 常駐上限(バッチ個数)。
    //
    // 戻り値: 破棄すべきキーの一覧(lastLiveTickが古いものから順)。liveKeysに含まれる
    // キーは常駐数が上限を超えていても対象にしない。liveKeysだけで既に上限を超えて
    // いる場合は、これ以上破棄できないため空を返す(常駐上限より表示中の内容維持を
    // 優先する)。
    static std::vector<std::string> SelectEvictions(
        const std::unordered_map<std::string, uint64_t>& lastLiveTickByKey,
        const std::unordered_set<std::string>& liveKeys, std::size_t maxResidentBatches);
};

} // namespace rs::render
