//
// HeadlightCollector.h - 描く灯具(Headlight)を、シーンの全スロットから集める
//
// 音源の収集(PlacedSoundRequestCollector)と同じ形。発動条件(If/Else/ApplySwitch)が偽の灯具と、
// ワールド位置をまだ求めていない灯具は含めない。フレアのスタイル(FlareStyle)は、灯具の定義の内容ごとに
// 初めて見たときに1回だけ作って共有する(画像のスロットは台帳 FlareTextureSlots から割り当てる)。
//
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../render/effects/HeadlightFlare.h"
#include "../plugin_session/FlareTextureSlots.h"
#include "SceneSlotPool.h"
#include "StaticHeadlightRoster.h"
#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::world { struct SceneEntity; }

namespace rs::app {

struct PluginSession;

// 静的バッチで描く個体(信号のモデルを含む)の灯具の入力。roster の個体を findEntity で引き、配置をグラフで解決する。
struct StaticHeadlightSource {
    const StaticHeadlightRoster* roster = nullptr;
    const rs::world::TrackGraph* graph = nullptr;
    // 個体IDから個体を引く。signalModel が真なら信号のモデル、偽なら SceneRegistry の個体。無ければ nullptr。
    std::function<const rs::world::SceneEntity*(const rs::world::Guid& entityId, bool signalModel)> findEntity;
    // 灯具の組(共有状態×_CAMDISTの距離の区分)を選ぶ入力。静的バッチの個体の切り替えと同じ値を渡す。
    rs::world::SceneSharedSwitchValues sharedState;
    bool hasView = false;  // 偽なら視点が未設定で、距離の区分は0として扱う
    float eye[3] = {0.0f, 0.0f, 0.0f};
    float fovYRadians = 0.0f;
};

class HeadlightCollector {
public:
    // slots の使用中のスロット(編成車両はスナップショットが確立したもの)と、
    // staticSource の静的バッチで描いている個体(無ければ roster が nullptr)の灯具を集める。
    // 新しいスタイルの画像は pass へ上げる(描画スレッドの外から呼ぶこと)。
    std::vector<rs::render::HeadlightLight> Collect(const std::vector<SceneSlotPool::Slot>& slots,
                                                    const StaticHeadlightSource& staticSource,
                                                    FlareTextureSlots& textureSlots,
                                                    rs::render::LensFlarePass& pass);

private:
    void AppendSession(const PluginSession& session, FlareTextureSlots& textureSlots,
                       std::vector<rs::render::HeadlightLight>& out);
    void AppendStatic(const StaticHeadlightSource& source, FlareTextureSlots& textureSlots,
                      std::vector<rs::render::HeadlightLight>& out);
    // 灯具1件を描く光源にする。スタイルは定義の内容ごとに共有する。
    rs::render::HeadlightLight MakeLight(const HeadlightAttachment& attachment, const rs::core::Vec3& position,
                                         const rs::core::Vec3& direction, FlareTextureSlots& textureSlots);

    // styleKey ごとの共有スタイル。一度作ったら解放しない(定義の内容ごとに1つで、小さい)。
    std::unordered_map<std::string, std::shared_ptr<const rs::render::FlareStyle>> styles_;
};

}  // namespace rs::app
