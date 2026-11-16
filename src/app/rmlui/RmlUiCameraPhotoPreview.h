#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include "../workspace/CameraFinder.h"
#include "../workspace/CameraPhotoPreview.h"

namespace Rml { class Context; }
namespace rs::app::rmlui {
struct CameraPhotoPreviewData {
    std::string source_;
    int phase_ = 0;
    workspace::CameraFinderRect origin_;
    workspace::CameraFinderRect target_;
};

class RmlUiCameraPhotoPreview {
public:
    ~RmlUiCameraPhotoPreview();
    void RegisterModel(Rml::DataModelConstructor& constructor, Rml::Context& context);
    void Show(std::string path, const workspace::CameraFinderRect& finder);
    void Refresh(Rml::Context& context, double deltaSeconds);
    void Clear();
private:
    workspace::CameraPhotoPreview preview_;
    CameraPhotoPreviewData data_;
    Rml::DataModelHandle modelHandle_;
    Rml::Context* context_ = nullptr;
    std::string pendingPath_;
    workspace::CameraFinderRect origin_;
    workspace::CameraFinderRect target_;
};
}
