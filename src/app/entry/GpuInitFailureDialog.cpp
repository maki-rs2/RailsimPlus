#include "GpuInitFailureDialog.h"

#include <format>

#include "StartupFailureDialog.h"
#include "../locale/Localization.h"

namespace rs::app {

std::string GpuInitFailureReason(const Localization& text, const GpuInitFailure& failure) {
    using Kind = GpuInitFailure::Kind;
    switch (failure.kind) {
    case Kind::NoDevice:
        return text.Text("app.gpu_init_failed.reason_no_device");
    case Kind::ShaderModel:
    {
        const std::string version = std::format("{}.{}", failure.shaderModelMajor, failure.shaderModelMinor);
        return std::vformat(text.Text("app.gpu_init_failed.reason_shader_model"),
                            std::make_format_args(version));
    }
    case Kind::Other:
        if (failure.hr != S_OK) {
            const std::string code = std::format("0x{:08X}", static_cast<unsigned>(failure.hr));
            return std::vformat(text.Text("app.gpu_init_failed.reason_other"), std::make_format_args(code));
        }
        break;
    case Kind::None:
        break;
    }
    return text.Text("app.gpu_init_failed.reason_unknown");
}

void ShowGpuInitFailureDialog(const Localization& text, const GpuInitFailure& failure) {
    const std::string gpu =
        failure.adapterName.empty() ? text.Text("app.gpu_init_failed.gpu_none") : failure.adapterName;
    const std::string reason = GpuInitFailureReason(text, failure);
    const std::string body = std::vformat(text.Text("app.gpu_init_failed.body"),
                                          std::make_format_args(gpu, reason));
    ShowFatalErrorBox(text.Text("app.gpu_init_failed.title"), body);
}

} // namespace rs::app
