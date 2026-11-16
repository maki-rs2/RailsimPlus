#include "WavDecoder.h"

#include <cstddef>

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace rs::sim {

bool WavDecoder::Decode(const std::wstring& path, DecodedSound& decoded) const {
    decoded = {};
    if (path.empty()) {
        spdlog::error("[audio] 空のファイルパスはデコードできません");
        return false;
    }
    // ログ用のUTF-8表示名のみ作る。実際のファイルオープンはma_decoder_init_file_w
    // (ワイド文字版)で行う: ma_decoder_init_file(char*版)はma_fopen内部でfopen_s/fopen
    // へそのまま渡されるため、非ASCIIパスをUTF-8で渡すと現在のANSIコードページ
    // (日本語WindowsではCP932)で誤解釈され、ファイルが存在するのに見つからない
    // (MA_DOES_NOT_EXIST)扱いになる(日本語フォルダ名のPIで起きる)。
    const std::string pathUtf8 = rs::platform_text::WideToUtf8(path);

    const ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
    ma_decoder decoder{};
    const ma_result initResult = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
    if (initResult == MA_DOES_NOT_EXIST) {
        // プラグインが存在しないファイルを指している(元データ側の誤り)。こちらの不具合ではない。
        spdlog::warn("[audio] サウンドファイルが見つかりません: path={}", pathUtf8);
        return false;
    }
    if (initResult != MA_SUCCESS) {
        spdlog::error("[audio] サウンドのデコード初期化に失敗しました: path={}, error={}",
                      pathUtf8, static_cast<int>(initResult));
        return false;
    }

    decoded.sampleRate = decoder.outputSampleRate;
    decoded.channels = decoder.outputChannels;
    if (decoded.sampleRate == 0 || decoded.channels == 0) {
        spdlog::error("[audio] デコード結果の音声形式が不正です: path={}", pathUtf8);
        ma_decoder_uninit(&decoder);
        return false;
    }

    constexpr ma_uint64 kFramesPerRead = 4096;
    std::vector<ma_int16> frames(static_cast<size_t>(kFramesPerRead * decoded.channels));
    for (;;) {
        ma_uint64 framesRead = 0;
        const ma_result readResult = ma_decoder_read_pcm_frames(&decoder, frames.data(),
                                                                 kFramesPerRead, &framesRead);
        if (readResult != MA_SUCCESS && readResult != MA_AT_END) {
            spdlog::error("[audio] PCMデコード中に失敗しました: path={}, error={}", pathUtf8,
                          static_cast<int>(readResult));
            ma_decoder_uninit(&decoder);
            decoded = {};
            return false;
        }
        const size_t samplesRead = static_cast<size_t>(framesRead * decoded.channels);
        decoded.pcm.insert(decoded.pcm.end(), frames.begin(), frames.begin() + samplesRead);
        if (framesRead == 0 || readResult == MA_AT_END) break;
    }

    ma_decoder_uninit(&decoder);
    if (decoded.pcm.empty()) {
        spdlog::error("[audio] PCMデータを取得できませんでした: path={}", pathUtf8);
        return false;
    }
    return true;
}

} // namespace rs::sim
