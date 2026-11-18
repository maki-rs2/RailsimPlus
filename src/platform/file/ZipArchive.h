//
// ZipArchive.h - 更新用ZIPの一覧と安全な展開(miniz)。Windows専用。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rs::platform_file {

// 配布ZIPのエントリは、すべてこの名前のフォルダの下に置く。展開先では、この接頭辞を外した相対パスにする。
inline constexpr std::string_view kArchiveRootPrefix = "RailsimPlus/";

// 接頭辞を含むエントリ名が、展開してよい名前か。接頭辞が無い・空・絶対パス・ドライブ文字・`..` や空の要素・
// `\` や `:` などNTFSで使えない文字・予約名(CON、NUL、COM1 など)・末尾のドットや空白・長すぎる名前は偽。
// 末尾が `/` のエントリはフォルダとして扱う。複数のエントリにまたがる重複は検査しない(HasDuplicateArchivePath)。
bool IsSafeArchiveEntryPath(std::string_view entryName);

// 安全と確認したエントリ名から、接頭辞と末尾の `/` を外した相対パスを返す。接頭辞だけのときは空。
std::string ArchiveRelativePath(std::string_view entryName);

// 大文字小文字を区別せずに、同じパスになるエントリ名の組があるか(NTFSでは同じファイルになるため)。
bool HasDuplicateArchivePath(const std::vector<std::string>& entryNames);

struct ZipEntryInfo {
    std::string name;  // ZIP内の名前(接頭辞つき)
    std::uint64_t uncompressedSize = 0;
    bool isDirectory = false;
    bool isSymlink = false;
    bool isEncrypted = false;
};

struct ZipLimits {
    std::uint64_t maxTotalUncompressedBytes = 0;  // 全エントリの展開後サイズの合計の上限
    std::size_t maxEntryCount = 0;
};

// ZIPのエントリを中央ディレクトリの順に返す。開けない・壊れているときは、outErrorへ英語の診断文言を入れて空を返す。
std::vector<ZipEntryInfo> ListZipEntries(const std::filesystem::path& zipPath, std::string& outError);

// 全エントリを検査してから、destinationの下へ展開する。1つでも不正(安全でない名前・重複・シンボリックリンク・
// 暗号化・件数や合計サイズの超過)があれば、何も書かずに失敗する。展開中の失敗では、書いたファイルを残す
// (展開先を捨てるのは呼び出し側)。失敗時はoutErrorへ英語の診断文言を入れる。
bool ExtractZip(const std::filesystem::path& zipPath, const std::filesystem::path& destination,
                const ZipLimits& limits, std::string& outError);

}  // namespace rs::platform_file
