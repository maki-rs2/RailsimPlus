#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rs::editor { class Editor; }
namespace rs::app {
class PluginSelector;
class ToastQueue;
namespace template_action {
// テンプレートの保存が使う参照。呼び出し側が所有し、使う間より長く生きること。
struct TemplateSaveRefs {
    rs::editor::Editor& editor;
    PluginSelector& selector;
    // プラグインルートの登録順。先頭の下へ保存する。
    const std::vector<std::string>& pluginRoots;
};
// 今の敷設プラグインセット(targetIdは対象の識別子。対象はTrackPluginSetActionが見ているもの)を保存する。
void SaveLayingTemplate(ToastQueue& queue, const TemplateSaveRefs& refs, std::string_view targetId,
                        std::string_view name);
// targetId(編成のguid)の編成を保存する。
void SaveFormationTemplate(ToastQueue& queue, const TemplateSaveRefs& refs, std::string_view targetId,
                           std::string_view name);
// 保存済みの敷設テンプレート(RailwayPluginSet。プラグインルートの先頭の下)を、今の敷設プラグインセットの対象へ足す。
void LoadLayingTemplate(ToastQueue& queue, const TemplateSaveRefs& refs, std::string_view name);
// 保存済みの編成テンプレートの車両を、すべて編成の挿入位置へ入れる。
void InsertFormationTemplate(ToastQueue& queue, std::string_view name);
}
}
