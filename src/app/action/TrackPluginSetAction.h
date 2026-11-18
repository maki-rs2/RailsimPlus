//
// TrackPluginSetAction.h - レールの敷設プラグインセット(敷設テンプレート/選択区間)の編集
//
// 対象は、既存レールの区間を選んでいればその区間のpluginSections、選んでいなければ
// 敷設テンプレート(layingSettings.defaultTrackPlugins)。ImGui版とRmlUi版のブラウザが
// 同じ操作を呼ぶ。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ActionError.h"
#include "../../plugins/schema/PluginHeader.h"

namespace rs::editor { class Editor; }

namespace rs::app {

class PluginSelector;

// 失敗の分類。表示文言はstatusとdetailからUIの端で組み立てる。
enum class TrackPluginSetStatus {
    Ok,
    LoadFailed,           // プラグイン定義を読めなかった(detailに理由)
    RejectedMaterialized, // 区間が実体化済みのため変更できない
    TemplateLoadFailed,   // 敷設テンプレートファイルを読めなかった
};

ErrorSeverity SeverityOf(TrackPluginSetStatus status);

struct TrackPluginSetResult {
    TrackPluginSetStatus status = TrackPluginSetStatus::Ok;
    std::string detail;
    bool Ok() const { return status == TrackPluginSetStatus::Ok; }
};

enum class TrackPluginSetTarget {
    Template, // 次に敷設するプラグインの一覧
    Section,  // 選択中のedgeの選択区間
};

struct TrackPluginSetItem {
    std::string folderUtf8;
    std::string displayName;      // 未読込なら空
    rs::legacy_pi::PluginType type = rs::legacy_pi::PluginType::Unknown;
    bool loaded = false;
    bool discrete = false;        // 離散配置(Pier)か
    float intervalMeters = 0.0f;  // discreteのときの間隔[m](PI定義値、未定義なら既定値)
};

struct TrackPluginSetViewModel {
    TrackPluginSetTarget target = TrackPluginSetTarget::Template;
    std::size_t sectionIndex = 0; // Sectionのとき
    std::size_t sectionCount = 0; // Sectionのとき
    // 2以上のとき、一覧は全レールの全区間に共通するもので、足す・外すは全レールの全区間に作用する。
    std::size_t selectedEdgeCount = 0;
    std::vector<TrackPluginSetItem> items;
};

class TrackPluginSetAction {
public:
    TrackPluginSetAction(rs::editor::Editor& editor, PluginSelector& selector)
        : editor_(editor), selector_(selector) {}

    TrackPluginSetViewModel ViewModel() const;
    // SamePluginPathで比べる。
    bool Contains(const std::string& folderUtf8) const;
    // 読み込んだうえで対象へ足す。既にあれば成功扱いで何もしない。
    TrackPluginSetResult Add(const std::string& folderUtf8);
    // SamePluginPathで一致する項目を外す。
    TrackPluginSetResult Remove(const std::string& folderUtf8);
    // 敷設テンプレート(RailwayPluginSet配下のtxt)のCheck=yesの要素を対象へまとめて足す。
    TrackPluginSetResult ApplyTemplateFile(const std::string& pathUtf8);

private:
    bool TargetsSection() const;
    std::size_t SectionIndex() const;
    // 対象の今のfolder一覧。
    std::vector<std::string> CurrentFolders() const;
    // 区間ならSetSelectedEdgeSectionFoldersで丸ごと置き換え(実体化済みならfalse)、
    // テンプレートなら置き換えて常にtrue。
    bool ReplaceFolders(std::vector<std::string> folders);

    rs::editor::Editor& editor_;
    PluginSelector& selector_;
};

} // namespace rs::app
