//
// RmlUiDiaCardView.h - ダイヤモードで、3D画面の番線と分岐の上に出すカード(workspace.rmlの`diaCards`)
//
// 停車できてレールを持つ番線と、分岐(ポイント)ごとに、画面上へ名前のカードを1枚出す。番線は中ほど、
// 分岐は分岐の位置に出す。カードを押すと、左タブがその番線・分岐の画面へ移る。名前をダブルクリック
// すると、その場で名前を変えられる(押したときと名前を変えたときの動作は呼び出し側が渡す)。
// 画面内にあるものだけを、番線と分岐を合わせて画面の中心に近い順に上限まで出す(全部のDOMは作らない)。
// 追従の置き方は閉塞ラベルと同じ(`RmlUiAnchoredElementPlacement`)。
//
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "RmlUiInlineRename.h"
#include "../../core/Math.h"
#include "../../world/alignment/Guid.h"
#include "../workspace/DiaCardCandidates.h"

namespace Rml {
class ElementDocument;
class Event;
}

namespace rs::editor {
class Editor;
}

namespace rs::app::rmlui {

struct DiaCardsModel;

class RmlUiDiaCardView {
public:
    using OpenCard = std::function<void(workspace::DiaCardKind, rs::world::Guid)>;
    using RenameCard = std::function<void(workspace::DiaCardKind, rs::world::Guid, const std::string&)>;

    RmlUiDiaCardView();
    ~RmlUiDiaCardView();
    RmlUiDiaCardView(const RmlUiDiaCardView&) = delete;
    RmlUiDiaCardView& operator=(const RmlUiDiaCardView&) = delete;

    void Attach(OpenCard open, RenameCard rename);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    // 毎フレーム呼ぶ。activeはダイヤモードのとき。editorは開いているシーンの軌道と選択を持つ。
    void Refresh(bool active, const rs::editor::Editor& editor, Rml::ElementDocument* document);
    // カメラが決まったあとに呼ぶ。画面内の番線と分岐を選んで、カードの位置を決める。
    void UpdatePositions(const rs::core::Mat4& viewProj, double screenWidth, double screenHeight);

private:
    const workspace::DiaCardCandidate* FindShown(const std::string& id) const;
    void OnOpen(const std::string& id);
    void OnBeginRename(Rml::Event& event, const std::string& id);
    void OnCommitName(Rml::Event& event);
    bool TargetExists(const workspace::DiaCardKind& kind, const rs::world::Guid& id) const;

    OpenCard open_;
    RenameCard rename_;
    RmlUiInlineRename inlineRename_;
    workspace::DiaCardKind editingKind_ = workspace::DiaCardKind::Platform;
    bool active_ = false;
    const rs::editor::Editor* editor_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    // 直近のUpdatePositionsで出したカード(押したときに、idから番線か分岐かを引く)。
    std::vector<workspace::DiaCardCandidate> shown_;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<DiaCardsModel> model_;
};

} // namespace rs::app::rmlui
