#include "RenderSettingsRows.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include "../settings/FpsLimitChoices.h"
#include "../../render/postprocess/AoQuality.h"
#include "../../render/postprocess/PostProcessPlan.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../../render/shadow/ShadowDistance.h"
#include "../../render/shadow/ShadowQuality.h"

namespace rs::app {
namespace {

// 保存用のkeyを付けた翻訳キー(例: render_settings.shadow_quality.low)の文言。
std::string KeyedLabel(const char* prefix, const char* key, const Localization& localization) {
    return localization.Text(std::string(prefix) + key);
}

// 添字そのものが値の選択肢。
int IdentityValue(std::size_t index) {
    return static_cast<int>(index);
}

const RenderChoiceList kShadowQualityList{
    rs::render::kShadowQualityChoices.size(),
    [](std::size_t i, const Localization& l) {
        return KeyedLabel("render_settings.shadow_quality.", rs::render::kShadowQualityChoices[i].key, l);
    },
    IdentityValue};

const RenderChoiceList kShadowDistanceList{
    rs::render::kShadowDistanceChoices.size(),
    [](std::size_t i, const Localization& l) {
        return KeyedLabel("render_settings.shadow_distance.", rs::render::kShadowDistanceChoices[i].key, l);
    },
    IdentityValue};

const RenderChoiceList kAntiAliasingList{
    rs::render::kAntiAliasingChoices.size(),
    [](std::size_t i, const Localization& l) {
        return KeyedLabel("render_settings.anti_aliasing.", rs::render::kAntiAliasingChoices[i].key, l);
    },
    IdentityValue};

const RenderChoiceList kAoQualityList{
    rs::render::kAoQualityChoices.size(),
    [](std::size_t i, const Localization& l) {
        return KeyedLabel("render_settings.ao_quality.", rs::render::kAoQualityChoices[i].key, l);
    },
    IdentityValue};

const RenderChoiceList kFpsLimitList{
    kFpsLimitChoices.size(),
    [](std::size_t i, const Localization& l) {
        const int fps = kFpsLimitChoices[i];
        return fps == 0 ? l.Text("render_settings.fps_limit.unlimited") : std::to_string(fps) + " fps";
    },
    IdentityValue};

// トーンマップの曲線の選択肢(Offは配管確認用なので出さない)。既定はTriple section(UserSettingsData::tonemapMode)。名前は曲線の固有名なので翻訳しない。
struct TonemapEntry {
    rs::render::TonemapMode mode;
    const char* label;
};
constexpr std::array<TonemapEntry, 7> kTonemapEntries{{
    {rs::render::TonemapMode::Agx, "AgX"},
    {rs::render::TonemapMode::Hable, "Hable"},
    {rs::render::TonemapMode::AcesFitted, "ACES(Hill)"},
    {rs::render::TonemapMode::Aces, "ACES(Narkowicz)"},
    {rs::render::TonemapMode::Neutral, "Neutral(Khronos)"},
    {rs::render::TonemapMode::Reinhard, "Reinhard"},
    {rs::render::TonemapMode::TripleSection, "Triple section"},
}};
const RenderChoiceList kTonemapList{
    kTonemapEntries.size(),
    [](std::size_t i, const Localization&) { return std::string(kTonemapEntries[i].label); },
    [](std::size_t i) { return static_cast<int>(kTonemapEntries[i].mode); }};

constexpr const char* kSectionDisplay = "render_settings.section.display";
constexpr const char* kSectionSky = "render_settings.section.sky";
constexpr const char* kSectionPost = "render_settings.section.post";
constexpr const char* kSectionShadow = "render_settings.section.shadow";
constexpr const char* kSectionLensFlare = "render_settings.section.lens_flare";

// 描画設定パネルの並び(ImGui版の並びと同じ)。
const RenderSettingRow kRows[] = {
    {.key = "render_distance", .kind = RenderSettingKind::LogSlider, .sectionKey = kSectionDisplay,
     .labelKey = "render_settings.render_distance",
     .getFloat = [](const RenderSettingsViewModel& v) { return v.renderDistance; },
     .setFloat = [](RenderSettingsAction& a, float x) { a.SetRenderDistance(x); }, .unit = "m"},
    // FPS上限はVSyncがオフのときだけ有効(オンの間は画面の更新に合わせる)。
    {.key = "fps_limit", .tab = RenderSettingTab::General, .kind = RenderSettingKind::Choice, .sectionKey = kSectionDisplay,
     .labelKey = "render_settings.fps_limit", .get = [](const RenderSettingsViewModel& v) { return v.fpsLimitIndex; },
     .set = [](RenderSettingsAction& a, int x) { a.SetFpsLimitIndex(x); },
     .noteKey = "render_settings.fps_limit.vsync_note", .noteOnlyWhenDisabled = true,
     .disabledWhen = RenderSettingDisabledWhen::VSyncOn, .choices = &kFpsLimitList},
    {.key = "vsync", .tab = RenderSettingTab::General, .kind = RenderSettingKind::Toggle, .sectionKey = kSectionDisplay,
     .labelKey = "render_settings.vsync", .get = [](const RenderSettingsViewModel& v) { return v.vsync ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetVSync(x != 0); }},
    {.key = "atmospheric_perspective", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionSky,
     .labelKey = "render_settings.atmospheric_perspective",
     .get = [](const RenderSettingsViewModel& v) { return v.atmosphericPerspective ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetAtmosphericPerspective(x != 0); }},
    {.key = "height_fog", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionSky,
     .labelKey = "render_settings.height_fog",
     .get = [](const RenderSettingsViewModel& v) { return v.heightFog ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetHeightFog(x != 0); }},
    {.key = "tonemap", .kind = RenderSettingKind::Choice, .sectionKey = kSectionSky,
     .labelKey = "render_settings.tonemap", .get = [](const RenderSettingsViewModel& v) { return v.tonemapMode; },
     .set = [](RenderSettingsAction& a, int x) { a.SetTonemapMode(x); },
     .choices = &kTonemapList},
    {.key = "anti_aliasing", .kind = RenderSettingKind::Choice, .sectionKey = kSectionPost,
     .labelKey = "render_settings.anti_aliasing",
     .get = [](const RenderSettingsViewModel& v) { return v.antiAliasingIndex; },
     .set = [](RenderSettingsAction& a, int x) { a.SetAntiAliasingIndex(x); },
     .choices = &kAntiAliasingList},
    {.key = "ambient_occlusion", .kind = RenderSettingKind::Choice, .sectionKey = kSectionPost,
     .labelKey = "render_settings.ambient_occlusion",
     .get = [](const RenderSettingsViewModel& v) { return v.ambientOcclusionIndex; },
     .set = [](RenderSettingsAction& a, int x) { a.SetAmbientOcclusionIndex(x); },
     .choices = &kAoQualityList},
    {.key = "bloom", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionPost,
     .labelKey = "render_settings.bloom", .get = [](const RenderSettingsViewModel& v) { return v.bloom ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetBloom(x != 0); }},
    {.key = "motion_blur", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionPost,
     .labelKey = "render_settings.motion_blur",
     .get = [](const RenderSettingsViewModel& v) { return v.motionBlur ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetMotionBlur(x != 0); }},
    {.key = "shadow_quality", .kind = RenderSettingKind::Choice, .sectionKey = kSectionShadow,
     .labelKey = "render_settings.shadow_quality",
     .get = [](const RenderSettingsViewModel& v) { return v.shadowQualityIndex; },
     .set = [](RenderSettingsAction& a, int x) { a.SetShadowQualityIndex(x); }, .choices = &kShadowQualityList},
    {.key = "shadow_distance", .kind = RenderSettingKind::Choice, .sectionKey = kSectionShadow,
     .labelKey = "render_settings.shadow_distance",
     .get = [](const RenderSettingsViewModel& v) { return v.shadowDistanceIndex; },
     .set = [](RenderSettingsAction& a, int x) { a.SetShadowDistanceIndex(x); }, .choices = &kShadowDistanceList},
    {.key = "sun_lens_flare", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionLensFlare,
     .labelKey = "render_settings.sun_lens_flare",
     .get = [](const RenderSettingsViewModel& v) { return v.sunLensFlare ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetSunLensFlare(x != 0); }},
    {.key = "sun_whiteout", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionLensFlare,
     .labelKey = "render_settings.sun_whiteout",
     .get = [](const RenderSettingsViewModel& v) { return v.sunWhiteout ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetSunWhiteout(x != 0); }},
    {.key = "misc_lens_flare", .kind = RenderSettingKind::Toggle, .sectionKey = kSectionLensFlare,
     .labelKey = "render_settings.misc_lens_flare",
     .get = [](const RenderSettingsViewModel& v) { return v.miscLensFlare ? 1 : 0; },
     .set = [](RenderSettingsAction& a, int x) { a.SetMiscLensFlare(x != 0); }},
};

// RmlUiの描画タブの上から並べる行(残りは表の並びで続ける)。画質プリセットの次から。
constexpr std::array<std::string_view, 5> kRenderTabLeadingKeys{"anti_aliasing", "shadow_quality", "shadow_distance",
                                                                "ambient_occlusion", "tonemap"};

// 描画タブでの並びの順位。先頭に並べる行は表の順、それ以外は後ろで表の並びのまま。
std::size_t RenderTabRank(std::string_view key) {
    for (std::size_t i = 0; i < kRenderTabLeadingKeys.size(); ++i) {
        if (kRenderTabLeadingKeys[i] == key) return i;
    }
    return kRenderTabLeadingKeys.size();
}

const char* TabKey(RenderSettingTab tab) {
    switch (tab) {
    case RenderSettingTab::Render: return "render";
    case RenderSettingTab::General: return "general";
    case RenderSettingTab::None: return "none";
    }
    return "none";
}

const char* KindKey(RenderSettingKind kind) {
    switch (kind) {
    case RenderSettingKind::Toggle: return "toggle";
    case RenderSettingKind::Choice: return "choice";
    case RenderSettingKind::LogSlider: return "slider";
    }
    return "toggle";
}

} // namespace

std::span<const RenderSettingRow> RenderSettingRows() {
    return std::span<const RenderSettingRow>(kRows);
}

const RenderSettingRow* FindRenderSettingRow(std::string_view key) {
    for (const RenderSettingRow& row : kRows) {
        if (key == row.key) return &row;
    }
    return nullptr;
}

bool IsRenderSettingRowDisabled(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel) {
    switch (row.disabledWhen) {
    case RenderSettingDisabledWhen::Never: return false;
    case RenderSettingDisabledWhen::VSyncOn: return viewModel.vsync;
    }
    return false;
}

bool ShouldShowRenderSettingNote(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel) {
    if (row.noteKey == nullptr) return false;
    return !row.noteOnlyWhenDisabled || IsRenderSettingRowDisabled(row, viewModel);
}

std::size_t RenderChoiceSelectedIndex(const RenderSettingRow& row, const RenderSettingsViewModel& viewModel) {
    if (row.choices == nullptr || row.get == nullptr) return 0;
    const int current = row.get(viewModel);
    for (std::size_t i = 0; i < row.choices->count; ++i) {
        if (row.choices->value(i) == current) return i;
    }
    return 0;
}

int LogValueToSliderPosition(float value, float minimum, float maximum) {
    if (!(minimum > 0.0f) || !(maximum > minimum)) return 0;
    const double ratio = std::log(static_cast<double>(std::clamp(value, minimum, maximum)) / minimum) /
                         std::log(static_cast<double>(maximum) / minimum);
    return static_cast<int>(std::lround(ratio * kRenderSliderSteps));
}

float SliderPositionToLogValue(int position, float minimum, float maximum) {
    if (!(minimum > 0.0f) || !(maximum > minimum)) return minimum;
    const double ratio = std::clamp(position, 0, kRenderSliderSteps) / static_cast<double>(kRenderSliderSteps);
    const double value = minimum * std::pow(static_cast<double>(maximum) / minimum, ratio);
    return std::clamp(static_cast<float>(std::round(value)), minimum, maximum);
}

std::vector<RenderSettingRowView> BuildRenderSettingRowViews(const RenderSettingsViewModel& viewModel,
                                                             const Localization& localization,
                                                             float sliderMinimum, float sliderMaximum) {
    std::vector<RenderSettingRowView> views;
    const char* previousSection = nullptr;
    for (const RenderSettingRow& row : kRows) {
        RenderSettingRowView view;
        view.key = row.key;
        view.kind = KindKey(row.kind);
        view.tab = TabKey(row.tab);
        if (previousSection == nullptr || std::string_view(previousSection) != row.sectionKey) {
            view.section = localization.Text(row.sectionKey);
        }
        previousSection = row.sectionKey;
        view.label = localization.Text(row.labelKey);
        view.disabled = IsRenderSettingRowDisabled(row, viewModel);
        if (ShouldShowRenderSettingNote(row, viewModel)) view.note = localization.Text(row.noteKey);
        switch (row.kind) {
        case RenderSettingKind::Toggle:
            view.checked = row.get(viewModel) != 0;
            break;
        case RenderSettingKind::Choice:
            for (std::size_t i = 0; i < row.choices->count; ++i) view.options.push_back(row.choices->label(i, localization));
            view.valueText = std::to_string(RenderChoiceSelectedIndex(row, viewModel));
            break;
        case RenderSettingKind::LogSlider: {
            const float value = row.getFloat(viewModel);
            view.sliderPosition = LogValueToSliderPosition(value, sliderMinimum, sliderMaximum);
            view.valueLabel = std::to_string(static_cast<long long>(std::llround(value))) + " " + row.unit;
            break;
        }
        }
        views.push_back(std::move(view));
    }
    return views;
}

void OrderRenderTabRows(std::vector<RenderSettingRowView>& views) {
    std::stable_sort(views.begin(), views.end(), [](const RenderSettingRowView& left, const RenderSettingRowView& right) {
        return RenderTabRank(left.key) < RenderTabRank(right.key);
    });
    for (RenderSettingRowView& view : views) view.section.clear();
}

bool ApplyRenderSettingToggle(std::string_view key, bool enabled, RenderSettingsAction& action) {
    const RenderSettingRow* const row = FindRenderSettingRow(key);
    if (row == nullptr || row->kind != RenderSettingKind::Toggle) return false;
    row->set(action, enabled ? 1 : 0);
    return true;
}

bool ApplyRenderSettingChoice(std::string_view key, std::size_t optionIndex, RenderSettingsAction& action) {
    const RenderSettingRow* const row = FindRenderSettingRow(key);
    if (row == nullptr || row->kind != RenderSettingKind::Choice || optionIndex >= row->choices->count) return false;
    row->set(action, row->choices->value(optionIndex));
    return true;
}

bool ApplyRenderSettingSlider(std::string_view key, int position, float sliderMinimum, float sliderMaximum,
                              RenderSettingsAction& action) {
    const RenderSettingRow* const row = FindRenderSettingRow(key);
    if (row == nullptr || row->kind != RenderSettingKind::LogSlider) return false;
    row->setFloat(action, SliderPositionToLogValue(position, sliderMinimum, sliderMaximum));
    return true;
}

} // namespace rs::app
