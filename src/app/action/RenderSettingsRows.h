//
// RenderSettingsRows.h - 描画設定の画面の行を、1つの表から作る
//
// 描画設定の項目は、種類(チェック・選択肢・対数スライダー)、見出し、翻訳キー、説明文、無効にする条件、
// 値の読み書きをこの表の1行に持つ。ImGui版のパネルとRmlUi版の描画タブは、どちらもこの表を歩いて行を作る。
// 項目を足すときは、RenderSettingsViewModel・RenderSettingsActionに値の出し入れを足し、ここに1行足す。
// 文言は翻訳キー(assets/locale/strings.json の render_settings.*)で、選択肢の文言は選択肢の表
// (kShadowQualityChoicesなど)の保存用のkeyを付けたキーで引く。
//
// 画面に出す値(RenderSettingRowView)の組み立てと、画面の操作を値の書き込みにする関数は、状態を持たない
// 純粋関数。
//
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "RenderSettingsAction.h"
#include "../locale/Localization.h"

namespace rs::app {

enum class RenderSettingKind { Toggle, Choice, LogSlider };

// RmlUiのゲーム設定のどのタブに出すか。Noneは出さない(シーン設定など、別の画面に移した項目。ImGuiのパネルには残る)。
enum class RenderSettingTab { Render, General, None };

// 行を無効にする条件。無効の行は、説明文(noteOnlyWhenDisabledの行)を出す。
enum class RenderSettingDisabledWhen { Never, VSyncOn };

// 選択肢の並び。画面の並びは、各選択肢の表(kShadowQualityChoicesなど)の並びと同じ。
struct RenderChoiceList {
    std::size_t count;
    std::string (*label)(std::size_t index, const Localization& localization);
    // 選択肢が表す値(Set*に渡す値)。表の添字そのものなら、添字を返す。
    int (*value)(std::size_t index);
};

struct RenderSettingRow {
    const char* key; // イベントの引数に使う名前(行の中で一意)
    RenderSettingTab tab = RenderSettingTab::Render;
    RenderSettingKind kind;
    const char* sectionKey; // 見出しの翻訳キー。続く行が同じなら見出しは1回だけ出す
    const char* labelKey;
    // Toggle(0/1)・Choice(選択肢が表す値)の読み書き。
    int (*get)(const RenderSettingsViewModel&) = nullptr;
    void (*set)(RenderSettingsAction&, int) = nullptr;
    const char* noteKey = nullptr; // 説明文の翻訳キー(無ければnullptr)
    bool noteOnlyWhenDisabled = false;
    RenderSettingDisabledWhen disabledWhen = RenderSettingDisabledWhen::Never;
    const RenderChoiceList* choices = nullptr; // Choiceだけ
    // LogSlider(範囲は呼び出し側が渡す)の読み書き。
    float (*getFloat)(const RenderSettingsViewModel&) = nullptr;
    void (*setFloat)(RenderSettingsAction&, float) = nullptr;
    const char* unit = nullptr; // LogSliderの値の単位(例: "m")
};

// 画面の行の表(描画設定パネルの並び)。
std::span<const RenderSettingRow> RenderSettingRows();
const RenderSettingRow* FindRenderSettingRow(std::string_view key);

// 行が今、無効か。
bool IsRenderSettingRowDisabled(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel);
// 行の説明文を今出すか(noteKeyがあり、noteOnlyWhenDisabledなら無効のときだけ)。
bool ShouldShowRenderSettingNote(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel);
// Choiceの行で、今の値が何番目の選択肢か(どれにも当たらなければ0)。
std::size_t RenderChoiceSelectedIndex(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel);

// 対数スライダーの位置(0〜kRenderSliderSteps)と値の変換。位置は範囲の最小〜最大を対数で等分する。
inline constexpr int kRenderSliderSteps = 1000;
int LogValueToSliderPosition(float value, float minimum, float maximum);
// 位置から値へ。値は1単位に丸める。
float SliderPositionToLogValue(int position, float minimum, float maximum);

// RmlUiの1行ぶんの表示値。kindは"toggle"・"choice"・"slider"。sectionは、その行から新しい見出しが
// 始まるときだけ入る。noteは出すときだけ入る。
struct RenderSettingRowView {
    std::string key;
    std::string kind;
    std::string tab; // "render"・"general"・"none"(RenderSettingTabの名前)
    std::string section;
    std::string label;
    std::string note;
    bool disabled = false;
    bool checked = false;           // toggle
    std::string valueText;          // choice: 選択中の選択肢の添字
    std::vector<std::string> options; // choice: 選択肢の文言
    int sliderPosition = 0;         // slider: 0〜kRenderSliderSteps
    std::string valueLabel;         // slider: 値の表示(例: 1500 m)
    bool operator==(const RenderSettingRowView&) const = default;
};

std::vector<RenderSettingRowView> BuildRenderSettingRowViews(const RenderSettingsViewModel& viewModel,
                                                             const Localization& localization,
                                                             float sliderMinimum, float sliderMaximum);

// 描画タブの並びにそろえる(画質プリセットの次に、AA・影・AO・トーンマップ、続けてそれ以外を表の並びで)。
// 描画タブは見出しを出さないので、並べ替えたあと見出しは空にする。
void OrderRenderTabRows(std::vector<RenderSettingRowView>& views);

// 画面の操作を値の書き込みにする。keyが表に無い・種類が違う・範囲外ならfalseで何もしない。
bool ApplyRenderSettingToggle(std::string_view key, bool enabled, RenderSettingsAction& action);
bool ApplyRenderSettingChoice(std::string_view key, std::size_t optionIndex, RenderSettingsAction& action);
bool ApplyRenderSettingSlider(std::string_view key, int position, float sliderMinimum, float sliderMaximum,
                              RenderSettingsAction& action);

} // namespace rs::app
