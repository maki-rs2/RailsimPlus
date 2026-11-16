#include "LayoutDocumentState.h"

namespace rs::app {

void LayoutDocumentState::MarkEdited() {
    ++markedEdits;
}

bool LayoutDocumentState::IsDirty(uint64_t editorRevision, uint64_t sceneRegistryRevision) const {
    return baseline.editorRevision != editorRevision ||
           baseline.sceneRegistryRevision != sceneRegistryRevision ||
           baseline.markedEdits != markedEdits;
}

void LayoutDocumentState::Rebase(uint64_t editorRevision, uint64_t sceneRegistryRevision) {
    baseline.editorRevision = editorRevision;
    baseline.sceneRegistryRevision = sceneRegistryRevision;
    baseline.markedEdits = markedEdits;
}

}  // namespace rs::app
