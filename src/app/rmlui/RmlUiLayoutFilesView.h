//
// RmlUiLayoutFilesView.h - レイアウトの一覧の画面(ロードと名前を付けて保存)をデータモデル"workspace"へ結ぶ。
//
// RmlUiPluginRootsViewと同じ形で、RmlUiWorkspaceViewがメンバとして持つ。ここは値を見せて操作を受けるだけで、
// 読込や保存はしない。押された結果はTakeCommandで呼び出し側(LayoutDocumentController)へ渡す。
// 画面の開閉はWorkspaceShellの重ね表示が受け持つ。
//
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

#include "../plugin_session/LayoutFileCatalog.h"
#include "../plugin_session/LayoutThumbnail.h"
#include "../../platform/file/ShellPickerDialog.h"
#include "../../render/resource/TextureLoader.h"

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace rs::app::rmlui {

struct LayoutFilesModel;

enum class LayoutFilesMode { Load, SaveAs };

struct LayoutFilesCommand {
    enum class Kind { LoadFile, SaveName, Close };
    Kind kind = Kind::LoadFile;
    std::string value; // LoadFile=ファイルのパス、SaveName=入力された名前
};

class RmlUiLayoutFilesView {
public:
    RmlUiLayoutFilesView();
    ~RmlUiLayoutFilesView();
    RmlUiLayoutFilesView(const RmlUiLayoutFilesView&) = delete;
    RmlUiLayoutFilesView& operator=(const RmlUiLayoutFilesView&) = delete;

    // データモデルの構築中(RmlUiWorkspaceView::CreateDataModel)に呼ぶ。
    void RegisterModel(Rml::DataModelConstructor& constructor);

    // 画面の中身を入れ替える。initialNameはSaveAsのときの名前欄の初期値。rs2はロードで出す RailSim2 の
    // レイアウト(サムネイルなし。空ならその段を出さない)。
    void SetContent(LayoutFilesMode mode, std::vector<LayoutFileEntry> recent, std::vector<LayoutFileEntry> folder,
                    const std::string& initialName, std::vector<LayoutFileEntry> rs2 = {});
    // 画面が見えているフレームに呼ぶ。SaveAsで開いた直後は名前欄へフォーカスを移す。読めたサムネイルをカードへ入れる。
    void Refresh(Rml::ElementDocument& document);
    // ファイル選択ダイアログの親にするウィンドウ(HWND)。
    void SetOwnerWindow(void* ownerWindow) { ownerWindow_ = ownerWindow; }
    // サムネイルの画像の名前(ltimg:...)をレンダラへ渡す。この画面の名前でなければfalse。
    bool ResolveImageSource(const std::string& source, rs::render::ImageData& out) const;

    std::optional<LayoutFilesCommand> TakeCommand();

private:
    void OnCardClick(const std::vector<LayoutFileEntry>& entries, int index);
    void OnNameChange(Rml::Event& event);
    void OnNameKey(Rml::Event& event);
    void SetName(const std::string& name);
    void SubmitName();
    void OnOpenOther();
    void OnRs2Search(Rml::Event& event);
    void OnSelectTab(const Rml::VariantList& arguments);
    // 検索語に当たる .rs2 だけをカードにする。
    void RebuildRs2Cards();
    void ReleaseImages();
    void RebuildCards();

    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<LayoutFilesModel> model_;
    LayoutFilesMode mode_ = LayoutFilesMode::Load;
    std::vector<LayoutFileEntry> recent_;
    std::vector<LayoutFileEntry> folder_;
    std::vector<LayoutFileEntry> rs2_;
    std::vector<std::size_t> rs2Shown_; // 今のカードが指す rs2_ の添字
    std::optional<LayoutFilesCommand> command_;
    bool pendingFocus_ = false;
    bool pendingScrollTop_ = false; // タブを切り替えたら、一覧を先頭から見せる
    void* ownerWindow_ = nullptr;
    rs::platform_file::ShellPicker picker_;
    LayoutThumbnailLoader thumbnailLoader_;
    std::uint64_t imageGeneration_ = 0;
    // パスごとの画像の名前と、名前ごとの画素。カードはパスから名前を引く。
    std::map<std::string, std::string> imageSourceByPath_;
    std::map<std::string, rs::render::ImageData> imageBySource_;
};

} // namespace rs::app::rmlui
