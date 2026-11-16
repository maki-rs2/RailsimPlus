#include "ScreenshotCapture.h"
#include "../gpu/App.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "ScreenshotFileName.h"
#include "../diagnostics/BackBufferCapturePng.h"
#include "../settings/UserSettings.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {

bool ScreenshotCapture::RequestIfTriggered(const rs::app::input::InputSource& input, App& app,
                                           bool otherCaptureRequested) {
    if (!input.Triggered(rs::app::input::InputAction::TakeScreenshot)) return false;
    if (otherCaptureRequested) return false;
    app.RequestBackBufferCapture(BackBufferCaptureStage::SceneOnly);
    pending_ = true;
    return true;
}

void ScreenshotCapture::SaveIfPending(App& app) {
    if (!pending_) return;
    pending_ = false;

    std::vector<uint8_t> pixels;
    UINT width = 0, height = 0, rowPitch = 0;
    if (!app.TakeCapturedBackBuffer(pixels, width, height, rowPitch)) {
        spdlog::error("[screenshot] バックバッファを読み戻せませんでした");
        return;
    }

    SaveScreenshotPng(pixels, width, height, rowPitch);
}

ScreenshotSaveResult SaveScreenshotPng(const std::vector<std::uint8_t>& pixels,
                                       std::uint32_t width, std::uint32_t height, std::uint32_t rowPitch) {
    const std::filesystem::path directory = UserSettings::ExecutableDirectory() / L"screenshots";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        spdlog::error("[screenshot] 保存先フォルダを作れません: {}", error.message());
        return {{}, "camera.photo.directory_failed"};
    }

    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime = {};
    localtime_s(&localTime, &now);
    const std::string fileName = ChooseScreenshotFileName(localTime, [&](const std::string& name) {
        std::error_code existsError;
        return std::filesystem::exists(directory / rs::platform_text::Utf8ToWide(name), existsError);
    });
    const std::string pathUtf8 =
        rs::platform_text::WideToUtf8((directory / rs::platform_text::Utf8ToWide(fileName)).wstring());

    std::vector<std::string> log;
    const bool saved = SaveBackBufferPng(pathUtf8, pixels, width, height, rowPitch, log);
    for (const std::string& line : log) {
        if (saved) spdlog::info("{}", line);
        else spdlog::error("{}", line);
    }
    if (!saved) return {{}, "camera.photo.write_failed"};
    return {pathUtf8, {}};
}

} // namespace rs::app
