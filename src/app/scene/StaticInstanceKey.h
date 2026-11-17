//
// StaticInstanceKey.h - 静的配置(GPUインスタンス化)のキャッシュ/バッチキー生成
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rs::world { struct SceneEntity; }

namespace rs::app {

// 読み込むプラグインの出どころ: フォルダと、読む定義ファイルのファイル名(空なら既定の選び方)。
// ParsedPluginCacheのキー、スロットのフォルダ一致の再利用、contentKeyが、同じ判定で同じものを
// 別物として扱うための1つの型。フォルダだけの指定(std::string)から暗黙に作れる。
struct PluginSource {
    std::string folderUtf8;
    std::string definitionFile;

    PluginSource() = default;
    PluginSource(std::string folder, std::string definition = std::string())
        : folderUtf8(std::move(folder)), definitionFile(std::move(definition)) {}

    bool operator==(const PluginSource& other) const;
};

// PluginSourceを1本の文字列にしたキー。定義ファイル名が空なら正規化したフォルダだけ。
// Windowsのファイル名に使えない'?'で区切るので、フォルダ名・ファイル名のどちらとも衝突しない。
std::string MakePluginSourceKey(const std::string& folderUtf8, const std::string& definitionFile);
inline std::string MakePluginSourceKey(const PluginSource& source) {
    return MakePluginSourceKey(source.folderUtf8, source.definitionFile);
}

// StaticMeshCacheのキャッシュキー、InstanceBatchManagerのバッチキー、
// SceneSlotPoolのcontentKeyに共通で使う「フォルダ+スイッチ確定値」の複合キー。
// 末尾の0は落として正規化する(ApplySwitchOptionsAndBakeは配列が短ければ前方のみ
// 対応させ残りは既定値0のままにするため、末尾0の有無で焼き上がりは変わらない)。
// GPUにもプラグイン読込にも依存しない純関数にしてある(単体テスト用)。
// signalModelが真(信号のモデルの個体)なら、同じフォルダ・スイッチの普通の個体とは別のキーにする
// (静的判定で駅舎の実行時スイッチの扱いが違い、焼き上がりが変わるため)。
// definitionFileが空でなければ、同じフォルダでも読む定義ファイルが違う個体は別のキーにする。
std::string MakeStaticInstanceKey(const std::string& folderUtf8,
                                  const std::vector<int32_t>& switchOptions, bool signalModel = false,
                                  const std::string& definitionFile = std::string());

// スロットの内容を表すキー(contentKey)。スロットの新規読込と完全一致の復活は、必ずこれで揃える。
std::string MakeSlotContentKey(const rs::world::SceneEntity& entity);

// バンド別バリアントのバッチキー。variantCountが1以下(_CAMDIST非参照、または全バンドが
// 同一の焼き上がり)ならbaseKeyのまま返す。そうでなければ"|cd"+添字を足す
// (baseKeyの形は"folder|値,値,"で、"|cd"は値の列に現れないので衝突しない)。
std::string MakeStaticVariantKey(const std::string& baseKey, size_t variantIndex,
                                 size_t variantCount);

} // namespace rs::app
