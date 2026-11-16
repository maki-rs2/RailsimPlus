//
// RmlUiPlatformCardView.h - ダイヤモードで、3D画面の番線の上に出すカード(workspace.rmlの`platformCards`)
//
// 停車できてレールを持つ番線ごとに、番線の中ほどの画面上へ名前のカードを1枚出す。カードを押すと、
// 左タブがその番線の画面へ移る(押したときの動作は呼び出し側が渡す)。画面内にある番線だけを、
// 画面の中心に近い順に上限まで出す(全番線ぶんのDOMは作らない)。追従の置き方は閉塞ラベルと同じ
// (`RmlUiAnchoredElementPlacement`)。
//
#pragma once

#include <functional>
#include <memory>
#include <string>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"

namespace Rml {
class ElementDocument;
}

namespace rs::world {
struct TrackGraph;
}

namespace rs::app::rmlui {

struct PlatformCardsModel;

class RmlUiPlatformCardView {
public:
    RmlUiPlatformCardView();
    ~RmlUiPlatformCardView();
    RmlUiPlatformCardView(const RmlUiPlatformCardView&) = delete;
    RmlUiPlatformCardView& operator=(const RmlUiPlatformCardView&) = delete;

    // カードを押したときに呼ぶ(引数は番線のguid)。
    void Attach(std::function<void(rs::world::Guid)> openPlatform);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 毎フレーム呼ぶ。activeはダイヤモードのとき。graphは開いているシーンの軌道。
    void Refresh(bool active, const rs::world::TrackGraph& graph, const rs::world::Guid& selectedPlatform,
                 Rml::ElementDocument* document);
    // カメラが決まったあとに呼ぶ。画面内の番線を選んで、カードの位置を決める。
    void UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight);

private:
    std::function<void(rs::world::Guid)> openPlatform_;
    bool active_ = false;
    const rs::world::TrackGraph* graph_ = nullptr;
    rs::world::Guid selected_ = rs::world::NilGuid();
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<PlatformCardsModel> model_;
};

} // namespace rs::app::rmlui
