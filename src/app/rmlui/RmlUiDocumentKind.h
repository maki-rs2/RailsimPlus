//
// RmlUiDocumentKind.h - RmlUiで読み込む文書の種類
//
#pragma once

namespace rs::app::rmlui {

// どの文書を読み込むか。Protoはウィジェット・配色確認用ギャラリー、Workspaceは固定ワークスペース、
// Viewerはプラグインビューアー(起動引数にPI定義txtを渡したときの画面)。
enum class RmlUiDocumentKind { Proto, Workspace, Viewer };

} // namespace rs::app::rmlui
