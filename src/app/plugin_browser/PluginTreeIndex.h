//
// PluginTreeIndex.h - プラグインのディレクトリ分け(*PluginTree)とカタログを突き合わせた、階層の索引
//
// 1つの種別タブぶんの木を、ブラウザに出せる形にする(RmlUi非依存の純C++)。
//   - File 行の種別がそのタブの種別フォルダ名と一致するものだけを使い、<種別フォルダ名>/<名前> を
//     相対パスのキーとしてカタログから引く。カタログに無い名前と、そのタブに入らないエントリは読み飛ばす
//   - フォルダの件数は、下の階層まで数えた「カタログにあってそのタブに入る」エントリの数。
//     0件のフォルダは索引に入れない
//   - 同じ階層に同名のフォルダが複数あるときは、先に出たものを使う
//   - 木のどこにも載っていないエントリは、最上位の直下に出す
// 階層は、最上位からのフォルダ名の列で指す。
//
#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "../../plugins/layout/PluginTreeSchema.h"
#include "BrowserKindCatalog.h"
#include "PluginCatalog.h"

namespace rs::app::plugin_browser {

// 階層に並べるフォルダ1つ(表示用)。
struct PluginTreeFolder {
    std::string name;
    std::size_t count = 0; // 下の階層まで数えたエントリ数
    bool operator==(const PluginTreeFolder&) const = default;
};

class PluginTreeIndex {
public:
    // 索引を作り直す。treeがnullptrなら木なし(HasTree()が偽)。
    void Build(const rs::legacy_layout::PluginTreeDir* tree, BrowserKind kind, const PluginCatalog& catalog);

    bool HasTree() const { return hasTree_; }

    // pathが指す階層があるか(空の列は最上位で、木があれば常に真)。
    bool Contains(const std::vector<std::string>& path) const;

    // pathの階層の直下のフォルダ(木のファイル順)。階層が無ければ空。
    std::vector<PluginTreeFolder> FoldersAt(const std::vector<std::string>& path) const;

    // pathの階層の直下のエントリ(並びは不定)。最上位は木に載っていないエントリも含む。階層が無ければ空。
    std::vector<EntryId> EntriesAt(const std::vector<std::string>& path) const;

private:
    struct Node {
        std::string name;
        std::size_t count = 0;
        std::vector<Node> children;
        std::vector<EntryId> entries;
    };

    const Node* Find(const std::vector<std::string>& path) const;

    // dirの中身をnodeへ詰める(子のフォルダは再帰)。載せたエントリはlistedへ足す。
    static void FillNode(Node& node, const rs::legacy_layout::PluginTreeDir& dir, BrowserKind kind,
                         const PluginCatalog& catalog, std::unordered_set<EntryId>& listed);

    bool hasTree_ = false;
    Node root_;
};

} // namespace rs::app::plugin_browser
