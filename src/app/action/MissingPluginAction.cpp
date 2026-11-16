#include "MissingPluginAction.h"

#include <memory>
#include <utility>

#include "SignalPluginRules.h"
#include "../../editor/tool/Editor.h"
#include "../../platform/text/Encoding.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../plugins/schema/sidecar/StationSidecar.h"
#include "../../world/scene/SceneRegistry.h"
#include "../plugin_session/ActiveEnv.h"
#include "../plugin_session/DeletePluginReferencesCommand.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/PluginReferenceReplace.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/ReplacePluginReferencesCommand.h"
#include "../plugin_session/RslLayoutSerialize.h"

namespace rs::app {
namespace {

MissingPluginReplaceResult Failure(MissingPluginReplaceStatus status, std::string detail = {}) {
    MissingPluginReplaceResult result;
    result.status = status;
    result.detail = std::move(detail);
    return result;
}

}  // namespace

ErrorSeverity SeverityOf(MissingPluginReplaceStatus status) {
    switch (status) {
    // 差し替え先を選び直せば再試行できる。
    case MissingPluginReplaceStatus::Ok:
    case MissingPluginReplaceStatus::NothingToReplace:
    case MissingPluginReplaceStatus::SameAsSource:
    case MissingPluginReplaceStatus::LoadFailed:
    case MissingPluginReplaceStatus::TypeMismatch:
    case MissingPluginReplaceStatus::EnvOutsideRoots:
    case MissingPluginReplaceStatus::NotDeletable:
        return ErrorSeverity::Recoverable;
    }
    return ErrorSeverity::Critical;
}

std::vector<MissingPluginEntry> MissingPluginAction::Collect() const {
    const std::vector<std::string> roots = refs_.pluginRoots ? refs_.pluginRoots() : std::vector<std::string>();
    const std::vector<PluginReference> references = CollectPluginReferences(
        refs_.editor, refs_.sceneRegistry, refs_.session, refs_.selector, &refs_.importedWireSpans, &roots);
    return AggregateMissingPlugins(references, refs_.folderExists ? refs_.folderExists : PluginExistsFn(PluginFolderExists));
}

std::size_t MissingPluginAction::CountReferences(const MissingPluginEntry& row,
                                                 const std::vector<std::string>& roots) const {
    const std::string key = PluginFolderKey(row.folderUtf8);
    size_t matching = 0;
    const ConstPluginReferenceContext readOnly{refs_.editor,  refs_.sceneRegistry,      refs_.session,
                                               refs_.selector, &refs_.importedWireSpans, &roots};
    VisitPluginReferences(readOnly, [&](PluginReferenceSlot& slot) {
        if (slot.site == row.site && PluginFolderKey(slot.folder) == key) ++matching;
        return false;
    });
    return matching;
}

MissingPluginReplaceResult MissingPluginAction::Replace(const MissingPluginEntry& row,
                                                        const std::string& toFolderUtf8) {
    const std::string fromKey = PluginFolderKey(row.folderUtf8);
    if (fromKey == PluginFolderKey(toFolderUtf8)) return Failure(MissingPluginReplaceStatus::SameAsSource);

    const std::vector<std::string> roots = refs_.pluginRoots ? refs_.pluginRoots() : std::vector<std::string>();
    PluginReferenceContext context{refs_.editor,  refs_.sceneRegistry,      refs_.session,
                                   refs_.selector, &refs_.importedWireSpans, &roots};

    // 行が指す参照が残っているか(一覧を出したあとに変わっていないか)。
    if (CountReferences(row, roots) == 0) return Failure(MissingPluginReplaceStatus::NothingToReplace);

    // 差し替え先の種類が、その場所に置けるものか。
    const rs::legacy_pi::PluginType type = refs_.classifyPluginType ? refs_.classifyPluginType(toFolderUtf8)
                                                                    : DefaultClassifyPluginType(toFolderUtf8);
    if (type == rs::legacy_pi::PluginType::Unknown) {
        return Failure(MissingPluginReplaceStatus::LoadFailed, toFolderUtf8);
    }
    if (!SiteAcceptsPluginType(row.site, type)) {
        return Failure(MissingPluginReplaceStatus::TypeMismatch, rs::legacy_pi::PluginTypeName(type));
    }

    PluginReplacePlan plan;
    plan.site = row.site;
    plan.fromFolderUtf8 = row.folderUtf8;
    plan.toFolderUtf8 = refs_.selector.RegisteredTrackPluginFolder(toFolderUtf8);
    std::string error;
    if (row.site == PluginReferenceSite::TrackSection) {
        // 区間の比較は綴りの完全一致なので、読込済みの一覧の綴りに揃えて書く。
        const std::optional<std::string> registered =
            refs_.selector.EnsureTrackPluginLoaded(toFolderUtf8, error);
        if (!registered) {
            return Failure(MissingPluginReplaceStatus::LoadFailed, error);
        }
        plan.toFolderUtf8 = *registered;
    } else if (row.site == PluginReferenceSite::FormationCar) {
        FormationCar car;
        if (!refs_.selector.LoadFormationCar(toFolderUtf8, car, error)) {
            return Failure(MissingPluginReplaceStatus::LoadFailed, error);
        }
        plan.car = std::move(car);
    } else if (row.site == PluginReferenceSite::Signal) {
        const std::string reason = ValidateSignalSidecar(
            rs::legacy_pi::sidecar::LoadStationSidecar(rs::platform_text::Utf8ToWide(toFolderUtf8)));
        if (!reason.empty()) return Failure(MissingPluginReplaceStatus::TypeMismatch, reason);
    } else if (row.site == PluginReferenceSite::Environment) {
        // .rslが保存するのは環境の名前だけ。名前から同じフォルダに戻れる場所のものだけを受け付ける。
        const std::string resolved = ResolveEnvPluginFolder(roots, PluginFolderLeafName(toFolderUtf8));
        if (resolved.empty() || PluginFolderKey(resolved) != PluginFolderKey(toFolderUtf8)) {
            return Failure(MissingPluginReplaceStatus::EnvOutsideRoots, toFolderUtf8);
        }
    }

    std::unique_ptr<ReplacePluginReferencesCommand> command =
        std::make_unique<ReplacePluginReferencesCommand>(context, std::move(plan), roots);
    ReplacePluginReferencesCommand* raw = command.get();
    refs_.editor.ExecutePlatformCommand(std::move(command));

    MissingPluginReplaceResult result;
    result.replacedCount = raw->ReplacedCount();
    return result;
}

MissingPluginReplaceResult MissingPluginAction::Delete(const MissingPluginEntry& row) {
    if (row.site == PluginReferenceSite::Environment) return Failure(MissingPluginReplaceStatus::NotDeletable);

    const std::vector<std::string> roots = refs_.pluginRoots ? refs_.pluginRoots() : std::vector<std::string>();
    if (CountReferences(row, roots) == 0) return Failure(MissingPluginReplaceStatus::NothingToReplace);

    PluginReferenceContext context{refs_.editor,  refs_.sceneRegistry,      refs_.session,
                                   refs_.selector, &refs_.importedWireSpans, &roots};
    std::unique_ptr<DeletePluginReferencesCommand> command =
        std::make_unique<DeletePluginReferencesCommand>(context, row.site, row.folderUtf8, roots);
    DeletePluginReferencesCommand* raw = command.get();
    refs_.editor.ExecutePlatformCommand(std::move(command));

    MissingPluginReplaceResult result;
    result.replacedCount = raw->DeletedCount();
    return result;
}

}  // namespace rs::app
