#include "TemplateAction.h"

#include <algorithm>
#include <string>

#include "FormationAction.h"
#include "TemplateDefBuilder.h"
#include "ToastQueue.h"
#include "TrackPluginSetAction.h"
#include "TrackPluginSetText.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/TemplateSaver.h"
#include "../plugin_session/TrainGroupTemplateParse.h"
#include "../../editor/tool/Editor.h"
#include "../../platform/text/Encoding.h"
#include "../../world/alignment/GuidJson.h"

namespace rs::app::template_action {

namespace {

// 結果を日本語のトーストにする。診断文言(英語)は保存側がログへ出している。
void ReportSaveResult(ToastQueue& queue, const TemplateSaveResult& result, std::string_view name) {
    if (result.ok) {
        queue.Post(ToastSeverity::Info, "テンプレート「" + std::string(name) + "」を保存しました");
        return;
    }
    std::string text = "テンプレートを保存できませんでした";
    if (!result.badField.empty()) text += "(" + result.badField + " に使えない文字があります)";
    queue.Post(ToastSeverity::Error, std::move(text));
}

// 保存先のルート(登録順の先頭)。無ければ通知して空を返す。
std::string SaveRoot(ToastQueue& queue, const TemplateSaveRefs& refs) {
    if (refs.pluginRoots.empty()) {
        queue.Post(ToastSeverity::Warning, "プラグインのルートが未登録のため保存できません");
        return {};
    }
    return refs.pluginRoots.front();
}

} // namespace

void SaveLayingTemplate(ToastQueue& queue, const TemplateSaveRefs& refs, std::string_view, std::string_view name) {
    const std::string root = SaveRoot(queue, refs);
    if (root.empty()) return;
    const std::string nameUtf8(name);
    const rs::legacy_layout::RailwayPluginSetDef def = BuildRailwayPluginSetDef(
        TrackPluginSetAction(refs.editor, refs.selector).ViewModel(), refs.editor.layingSettings, nameUtf8,
        refs.pluginRoots);
    ReportSaveResult(queue, SaveRailwayPluginSetTemplate(root, nameUtf8, def), name);
}

void LoadLayingTemplate(ToastQueue& queue, const TemplateSaveRefs& refs, std::string_view name) {
    if (refs.pluginRoots.empty()) {
        queue.Post(ToastSeverity::Warning, "プラグインのルートが未登録のため読み込めません");
        return;
    }
    const std::filesystem::path path = TemplateSavePath(refs.pluginRoots.front(), TemplateKind::RailwayPluginSet, std::string(name));
    const TrackPluginSetResult result = TrackPluginSetAction(refs.editor, refs.selector)
                                            .ApplyTemplateFile(rs::platform_text::WideToUtf8(path.wstring()));
    const std::string error = TrackPluginSetErrorText(result);
    if (error.empty()) {
        queue.Post(ToastSeverity::Info, "テンプレート「" + std::string(name) + "」を読み込みました");
    } else {
        queue.Post(ToastSeverity::Warning, error);
    }
}

void SaveFormationTemplate(ToastQueue& queue, const TemplateSaveRefs& refs, std::string_view targetId,
                           std::string_view name) {
    const std::string root = SaveRoot(queue, refs);
    if (root.empty()) return;
    const std::optional<rs::world::Guid> guid = rs::world::GuidFromHex(std::string(targetId));
    const std::vector<Formation>& formations = refs.selector.Formations();
    std::vector<Formation>::const_iterator found = formations.end();
    if (guid.has_value()) found = std::ranges::find(formations, *guid, &Formation::guid);
    if (found == formations.end() || found->cars.empty()) {
        queue.Post(ToastSeverity::Warning, "保存する編成が見つからないか、車両がありません");
        return;
    }
    const std::string nameUtf8(name);
    ReportSaveResult(queue,
                     SaveTrainGroupTemplate(root, nameUtf8, BuildTrainGroupTemplateDef(*found, refs.pluginRoots)),
                     name);
}

void InsertFormationTemplate(ToastQueue& queue, const FormationRefs& refs, const std::string& pluginRoot,
                             std::string_view name, std::size_t defIndex, std::size_t slot) {
    const std::string nameUtf8(name);
    if (pluginRoot.empty()) {
        queue.Post(ToastSeverity::Warning, "プラグインのルートが未登録のため読み込めません");
        return;
    }
    const std::filesystem::path path = TemplateSavePath(pluginRoot, TemplateKind::TrainGroupTemplate, nameUtf8);
    const TrainGroupTemplateParseResult parsed =
        ParseTrainGroupTemplateFile(rs::platform_text::WideToUtf8(path.wstring()), pluginRoot);
    if (!parsed.ok || parsed.cars.empty()) {
        queue.Post(ToastSeverity::Warning, "テンプレート「" + nameUtf8 + "」を読み込めませんでした");
        return;
    }
    std::vector<std::string> warnings;
    const std::size_t inserted =
        FormationAction(refs).InsertTemplateCars(defIndex, slot, parsed.cars, parsed.installRootUtf8, warnings);
    const std::size_t skipped = parsed.cars.size() - inserted;
    if (inserted == 0) {
        queue.Post(ToastSeverity::Warning, "テンプレート「" + nameUtf8 + "」の車両が見つからないため、入れられませんでした");
    } else if (skipped > 0) {
        queue.Post(ToastSeverity::Warning, "テンプレート「" + nameUtf8 + "」から" + std::to_string(inserted) +
                                               "両を入れました(見つからない" + std::to_string(skipped) + "両は入れていません)");
    } else {
        queue.Post(ToastSeverity::Info,
                   "テンプレート「" + nameUtf8 + "」から" + std::to_string(inserted) + "両を入れました");
    }
}
} // namespace rs::app::template_action
