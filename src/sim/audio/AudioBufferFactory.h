#pragma once

#include <AL/al.h>

#include "WavDecoder.h"

namespace rs::sim {

// OpenALバッファ生成に共通の32本上限。AudioSourceManagerとPlacedSoundSourcePoolの
// 双方が同時に鳴らせるアクティブソース数の上限として使う。
inline constexpr std::size_t kMaxActiveAudioSources = 32;

// CreateOpenAlBufferの結果種別。
//  - PermanentFailure: 音源データ自体が原因(チャンネル数不正・PCM空)で、再試行しても
//    結果は変わらない。呼び出し側はこの結果をキャッシュして再試行を打ち切ってよい。
//  - RetryableFailure: alGenBuffers/alBufferDataがOpenAL側のエラーを返した場合。
//    デバイス起因の一時的な失敗の可能性があり、再試行の余地がある。呼び出し側は
//    キャッシュせず、次回のフレーム等で再試行させる。
enum class BufferCreateOutcome { Success, PermanentFailure, RetryableFailure };

// decode済みPCMからOpenALバッファを生成する。チャンネル数判定・PCM検証・
// alGenBuffers・alBufferDataと、失敗時のログ出力・バッファ削除をここへ集約する。
// 成功時はoutBufferへ生成したバッファ名を書き込む。失敗時はoutBufferを変更しない。
BufferCreateOutcome CreateOpenAlBuffer(const DecodedSound& decoded, ALuint& outBuffer);

} // namespace rs::sim
