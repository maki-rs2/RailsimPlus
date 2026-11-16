//
// RmlUiPointHandMarkView.h - ダイヤモードで、選んでいる分岐の分かれる線ごとに出す「左」「右」の印(workspace.rmlの`pointHandMarks`)
//
// 分かれる線のレール上、分岐から一定の距離の点の画面上に印を出す。今開通している側の印は選択の見た目にする。
// 印を押すと、その線へ開通先を切り替える(切り替えの処理は呼び出し側が渡す)。位置・名前・開通状態の
// 求め方は `workspace::BuildPointHandMarks` で、追従の置き方は閉塞ラベルと同じ(`RmlUiAnchoredElementPlacement`)。
//
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"
#include "../workspace/PointHandMarks.h"

namespace Rml {
class ElementDocument;
}

namespace rs::editor {
class Editor;
}

namespace rs::app::rmlui {

struct PointHandMarksModel;

class RmlUiPointHandMarkView {
public:
    // 引数は分岐のguidと、切り替え先の links の添字。
    using ChangeOpening = std::function<void(rs::world::Guid, int)>;

    RmlUiPointHandMarkView();
    ~RmlUiPointHandMarkView();
    RmlUiPointHandMarkView(const RmlUiPointHandMarkView&) = delete;
    RmlUiPointHandMarkView& operator=(const RmlUiPointHandMarkView&) = delete;

    void Attach(ChangeOpening changeOpening);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 毎フレーム呼ぶ。activeはダイヤモードのとき。選んでいる分岐が無ければ印を出さない。
    void Refresh(bool active, const rs::editor::Editor& editor, Rml::ElementDocument* document);
    // カメラが決まったあとに呼ぶ。印の位置を決める。
    void UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight);

private:
    ChangeOpening changeOpening_;
    rs::world::Guid node_ = rs::world::NilGuid();
    std::vector<workspace::PointHandMark> marks_;
    Rml::ElementDocument* document_ = nullptr;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<PointHandMarksModel> model_;
};

} // namespace rs::app::rmlui
