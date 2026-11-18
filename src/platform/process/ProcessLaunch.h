//
// ProcessLaunch.h - 別の実行ファイルの起動(CreateProcessW の薄い包み)。Windows専用。
//
#pragma once

#include <filesystem>

namespace rs::platform_process {

// executable を新しいプロセスとして起動する。起動した子の終了は待たず、ハンドルも持たない。
// executable は絶対パスだけを受け付ける(相対パスだと、起動するファイルが作業フォルダや PATH で変わるため)。
// 作業フォルダは workingDirectory。起動できたら真、パスが不正・ファイルが無い・起動を拒まれたときは偽。
bool LaunchProcess(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory);

}  // namespace rs::platform_process
