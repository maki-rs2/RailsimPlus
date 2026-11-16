# JSONを1バイトずつの16進配列にしたヘッダーを生成するスクリプト。
# CMakeLists.txt の rs_embed_config_json から `cmake -P` で呼ばれる(ビルド時に実行)。
# 入力: JSON_PATH(元のJSON) / SYMBOL_NAME(配列の名前) / OUT_PATH(出力ヘッダー)

file(READ "${JSON_PATH}" hexContent HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," byteList "${hexContent}")
file(WRITE "${OUT_PATH}"
"// 自動生成ファイル - 編集しないこと。生成元: ${JSON_PATH}
// 生成規則は cmake/EmbedJson.cmake を参照。
#pragma once

#include <cstddef>

namespace rs::embedded {

inline constexpr unsigned char ${SYMBOL_NAME}[] = { ${byteList} };
inline constexpr std::size_t ${SYMBOL_NAME}Size = sizeof(${SYMBOL_NAME});

} // namespace rs::embedded
")
