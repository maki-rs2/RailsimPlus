#include "TrackPluginSetText.h"

namespace rs::app {

std::string TrackPluginSetErrorText(const TrackPluginSetResult& result) {
    switch (result.status) {
    case TrackPluginSetStatus::Ok: return {};
    case TrackPluginSetStatus::LoadFailed: return result.detail;
    case TrackPluginSetStatus::RejectedMaterialized:
        return "実体化済みのため変更できません(解除してからやり直してください)";
    case TrackPluginSetStatus::TemplateLoadFailed: return "テンプレートとして読めませんでした(ファイルの形式を確認してください)";
    }
    return {};
}

} // namespace rs::app
