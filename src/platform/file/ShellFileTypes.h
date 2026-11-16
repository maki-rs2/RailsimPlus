//
// ShellFileTypes.h - ShellPickerのファイル選択で使う、ファイルの種類の絞り込みの定義
//
// ImGui側とRmlUi側が同じ種類を選ばせるので、定義はここに1つだけ置く。
//
#pragma once

#include "ShellPickerDialog.h"

namespace rs::platform_file {

// レイアウト(.rsl / .rs2)。
inline const ShellFileType kLayoutFileType{"レイアウト (*.rsl, *.rs2)", "*.rsl;*.rs2"};
// テンプレートなどのテキスト(.txt)。
inline const ShellFileType kTextFileType{"テキスト (*.txt)", "*.txt"};

} // namespace rs::platform_file
