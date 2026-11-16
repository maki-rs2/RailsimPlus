#pragma once

#include <string>
#include <vector>

#include "../../world/alignment/Guid.h"

namespace rs::app::workspace {

struct DiaPanelChoice {
    std::string value_;
    bool selected_ = false;
    // 選んだとき何を指すか。層の候補は層のキー(編成なら0)と編成のGuid、書換の候補は運行情報の項目の番号。
    int key_ = 0;
    rs::world::Guid id_{};
};

struct DiaPanelField {
    std::string label_;
    std::string draft_;
    std::vector<DiaPanelChoice> choices_;
    bool hovered_ = false;
    int key_ = 0; // 運行情報の書換の項目の番号(RunInfoChange::Field)
};

struct DiaPanelElement {
    std::string summary_;
    bool overwrite_ = false;
    bool disabled_ = false;
    bool open_ = false;
    bool hovered_ = false;
    std::string operation_;
    std::string departure_;
    std::string seconds_;
    std::vector<DiaPanelField> times_;
    bool timesOpen_ = true;
    bool connection_ = false;
    std::vector<DiaPanelChoice> platforms_;
    std::string maximum_;
    std::string stopPosition_;
    // 要素で停止位置を指定しているか。指定していなければスライダーは0(中央)に置く。
    bool stopCustom_ = false;
    // 要素の停止位置(正規化[-1,+1]。未指定のときは0)。
    double stopOffset_ = 0.0;
    std::vector<DiaPanelField> overwrites_;
    std::string hand_;
    // 層の中で次に使う要素(先頭の有効な要素)。走行中のカーソルは見ない。
    bool next_ = false;
};

struct DiaPanelCondition {
    std::string kind_;
    std::string summary_;
    bool open_ = false;
    std::string start_;
    std::string end_;
    std::vector<DiaPanelChoice> blocks_;
    bool all_ = false;
    std::string hand_;
};

struct DiaPanelLayer {
    std::string name_;
    std::string kind_;
    bool deletable_ = false;
    bool reorderable_ = false;
    bool selected_ = false;
    bool hovered_ = false;
    std::vector<DiaPanelCondition> conditions_;
    std::vector<DiaPanelElement> elements_;
    std::string summary_;
    bool empty_ = true;
    // 層の持ち主を指すキー。共通=kCommonRoutineLayerKey、タグ=TagRoutineLayerKey(タグ番号)、
    // 編成=kDiaFormationLayerKey(編成のGuidはformationId_)。
    int routineKey_ = -1;
    rs::world::Guid formationId_{};
};

struct DiaPanelTarget {
    std::string breadcrumb_;
    bool branch_ = false;
    bool selected_ = false;
    bool doorLeft_ = false;
    bool doorRight_ = false;
    std::string opening_;
    std::vector<DiaPanelLayer> layers_;
    rs::world::Guid platformId_{};
    int sceneIndex_ = 0;
};

struct DiaPanelListRow {
    std::string kind_;
    std::string name_;
    int target_ = -1;
    bool gray_ = false;
    bool highlighted_ = false;
    bool hovered_ = false;
    bool editing_ = false;
    std::string draft_;
    bool draggable_ = false;
    // 階層(シーン=0、駅・未所属の番線=1、駅の番線=2)と折りたたみ。visible_は祖先が畳まれていない結果。
    int depth_ = 0;
    bool collapsed_ = false;
    bool visible_ = true;
    // 駅の行は駅のGuid、シーンの行は使わない。シーンの添字は全種類の行が持つ。
    rs::world::Guid id_{};
    int sceneIndex_ = 0;
    // 駅の行の「カメラを動かす先」。番線にレールが無い駅は持たない。
    bool hasFlyTarget_ = false;
    float flyX_ = 0.0f;
    float flyY_ = 0.0f;
    float flyZ_ = 0.0f;
};

// 編成の層を表す層のキー(PlatformRoutine.h の共通・タグ・編成ごとのキーと重ならない値)。
inline constexpr int kDiaFormationLayerKey = 0;

// 値と操作中の表示状態を持ち、RmlUiには依存しない。
struct DiaPanelPresentation {
    bool active_ = false;
    // 一覧=0、番線(層を行で並べ、開いた層の要素をその下に出す)=1、要素編集=2(番線の画面を残したまま、右隣に編集を出す)。
    int screen_ = 0;
    int sceneCount_ = 0;
    // 開いているシーン(アクティブ)の添字。操作ができるのはこのシーンの番線だけ。
    int activeScene_ = 0;
    std::string breadcrumb_;    // 番線の画面の見出し(駅・番線の名前)
    std::string inspectorTitle_; // 要素編集の見出し(層の名前 > 番号 動作)
    std::vector<DiaPanelListRow> rows_;
    std::vector<DiaPanelTarget> targets_;
    // 追加できる層(編成とタグ)の全部。開いている番線にまだ無いものが layerChoices_ に入る。
    std::vector<DiaPanelChoice> layerCatalog_;
    std::vector<DiaPanelChoice> layerChoices_;
    std::vector<DiaPanelChoice> overwriteChoices_;
    bool addingLayer_ = false;
    bool addingOverwrite_ = false;
    int targetIndex_ = -1;
    int layerIndex_ = -1;
    int elementIndex_ = -1;
    int dragIndex_ = -1;
    std::string dragKind_;

    void SetActive(bool active);
    bool OpenTarget(int index);
    bool OpenLayer(int index);
    bool OpenElement(int index);
    void Back();
    // 一覧(screen_ = 0)まで戻る。
    void BackToList();
    void ToggleCondition(int index);
    void ToggleTimes(int index);
    // シーン・駅の行を折りたたむ/広げる。配下の行(depth_が大きい続きの行)の表示を更新する。
    void ToggleFold(int index);
    void BeginRename(int index);
    void EndRename();
    void ClearHover();
    void RefreshDerived();
    // 開いている番線・要素に合わせて、層の追加と書換の追加の候補を作り直す。
    void RefreshChoices();
    // 畳まれた行より深い行が続く間は隠す(visible_を collapsed_ から計算し直す)。
    void RecomputeRowVisibility();
    DiaPanelLayer* CurrentLayer();

private:
    void CloseRows();
    void UpdateBreadcrumb();
};

} // namespace rs::app::workspace
