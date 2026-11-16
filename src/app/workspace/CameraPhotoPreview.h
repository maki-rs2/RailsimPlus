#pragma once

#include <string>

namespace rs::app::workspace {
enum class CameraPhotoPhase { Hidden, Started, Flash, Flying, Holding, Fading };

struct CameraPhotoTiming {
    double flash = 0.0;
    double flight = 0.0;
    double hold = 0.0;
    double fade = 0.0;
};

class CameraPhotoPreview {
public:
    void Start(std::string source, CameraPhotoTiming timing);
    void Advance(double deltaSeconds);
    void Clear();
    CameraPhotoPhase Phase() const { return phase_; }
    const std::string& Source() const { return source_; }
private:
    CameraPhotoPhase phase_ = CameraPhotoPhase::Hidden;
    CameraPhotoTiming timing_;
    double elapsed_ = 0.0;
    std::string source_;
};
}
