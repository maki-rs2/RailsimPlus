#include "GameSettingsScreenModel.h"
#include "InputSettingsRows.h"

namespace rs::app {
workspace::GameSettingsPresentation BuildGameSettingsPresentation(workspace::GameSettingsPresentation presentation,
                                                                 const Localization& localization) {
    presentation.label_ = localization.Text("game_settings.fullscreen");
    presentation.choices_ = {{"off", localization.Text("game_settings.off")},
                             {"on", localization.Text("game_settings.on")}};
    return presentation;
}

workspace::RenderSettingsPresentation BuildRenderSettingsPresentation(workspace::RenderSettingsPresentation presentation,
                                                                     const Localization& localization) {
    presentation.label_ = localization.Text("game_settings.quality_preset");
    presentation.customLabel_ = localization.Text("game_settings.quality_preset.custom");
    presentation.choices_ = {{"custom", presentation.customLabel_},
                             {"low", localization.Text("game_settings.quality_preset.low")},
                             {"medium", localization.Text("game_settings.quality_preset.medium")},
                             {"high", localization.Text("game_settings.quality_preset.high")}};
    return presentation;
}

workspace::InputSettingsPresentation BuildInputSettingsPresentation(workspace::InputSettingsPresentation presentation,
                                                                   const Localization& localization,
                                                                   const std::vector<input::InputBinding>& bindings) {
    FillInputSettingsRows(presentation, bindings, localization);
    presentation.mouseLabel_ = localization.Text("game_settings.mouse_operation");
    presentation.keysLabel_ = localization.Text("game_settings.key_operations");
    presentation.waitingLabel_ = localization.Text("game_settings.input_waiting");
    presentation.resetLabel_ = localization.Text("game_settings.reset_to_default");
    presentation.resetAllLabel_ = localization.Text("game_settings.reset_all_bindings");
    presentation.warningLabel_ = localization.Text("game_settings.input_warning");
    presentation.unassignedLabel_ = localization.Text("game_settings.input_unassigned");
    presentation.confirmMessage_ = localization.Text("game_settings.input_conflict_message");
    presentation.confirmLabel_ = localization.Text("game_settings.input_confirm");
    presentation.cancelLabel_ = localization.Text("game_settings.input_cancel");
    presentation.note_ = localization.Text("game_settings.placeholder");
    presentation.unassignableMessage_ = localization.Text("game_settings.input_unassignable_key");
    presentation.reservedKeyMessage_ = localization.Text("game_settings.input_reserved_key");
    presentation.resetAllConfirmMessage_ = localization.Text("game_settings.input_reset_all_message");
    presentation.mouseChoices_ = {{"wasd", localization.Text("game_settings.mouse_operation.wasd")},
                                  {"drag", localization.Text("game_settings.mouse_operation.drag")}};
    return presentation;
}
} // namespace rs::app
