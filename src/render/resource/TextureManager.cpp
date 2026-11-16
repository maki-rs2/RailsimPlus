#include "TextureManager.h"

#include <spdlog/spdlog.h>

#include "TextureLoader.h"
#include "TextureUpload.h"
#include "../pipeline/ShaderColor.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {
// 色はすべて0xAABBGGRR順。
constexpr uint32_t kPlaceholderRGBA = 0xFFFF00FFu; // マゼンタ(A=FF,B=FF,G=00,R=FF)
constexpr uint32_t kDefaultBaseRGBA = 0xFFFFFFFFu;   // 白
constexpr uint32_t kDefaultNormalRGBA = 0xFFFF8080u; // 平らな法線(0,0,1)

// base を baseRGBA の1色、normal/orm を既定値にした1x1の3枚組を作る。
std::shared_ptr<TextureResource> CreateSolidTextureSet(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                                       uint32_t baseRGBA, LoadState state) {
    std::shared_ptr<TextureResource> resource = std::make_shared<TextureResource>();
    ImageData none; // ok=false → fallback色が使われる
    resource->base = CreateGpuTexture(device, upload, none, baseRGBA, true,
                                      &resource->hasAlpha, &resource->hasZeroAlpha);
    resource->normal = CreateGpuTexture(device, upload, none, kDefaultNormalRGBA, false);
    resource->orm = CreateGpuTexture(device, upload, none, kDefaultOrmRGBA, false);
    resource->state = state;
    return resource;
}
} // namespace

std::shared_ptr<TextureResource> TextureManager::CreatePlaceholder(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
    if (!placeholder_) placeholder_ = CreateSolidTextureSet(device, upload, kPlaceholderRGBA, LoadState::kFailed);
    return placeholder_;
}

std::shared_ptr<TextureResource> TextureManager::AcquireDefaultTextures(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!defaults_ || !defaults_->base || !defaults_->normal || !defaults_->orm) {
        defaults_ = CreateSolidTextureSet(device, upload, kDefaultBaseRGBA, LoadState::kReady);
    }
    return defaults_;
}

std::shared_ptr<TextureResource> TextureManager::Acquire(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const std::string& basePath, const std::string& normalPath,
    const std::string& pbrPath) {
    std::lock_guard<std::mutex> lock(mutex_);

    const TextureCacheKey key(basePath, normalPath, pbrPath);
    std::shared_ptr<TextureResource> resident = cache_.Find(key);
    if (resident) return resident;

    if (failed_.find(basePath) != failed_.end()) {
        return CreatePlaceholder(device, upload);
    }

    ImageData baseImg = LoadImageRGBA(basePath);
    if (!baseImg.ok) {
        if (baseImg.notFound) {
            spdlog::warn("[texture] base image not found: {}", basePath);
        } else {
            spdlog::warn("[texture] base image decode failed (存在するが読込不可): {}", basePath);
        }
        failed_.insert(basePath);
        return CreatePlaceholder(device, upload);
    }

    std::shared_ptr<TextureResource> resource = std::make_shared<TextureResource>();
    resource->base = CreateGpuTexture(device, upload, baseImg, kDefaultBaseRGBA, true,
                                      &resource->hasAlpha, &resource->hasZeroAlpha, false,
                                      &resource->semiAlphaFraction);
    if (!resource->base) {
        spdlog::warn("[texture] GPU resource creation failed: {} ({}x{})", basePath,
                     baseImg.width, baseImg.height);
        failed_.insert(basePath);
        return CreatePlaceholder(device, upload);
    }

    ImageData nrm;
    if (!normalPath.empty()) {
        nrm = LoadImageRGBA(normalPath);
        resource->hasNormal = nrm.ok;
    }
    resource->normal = CreateGpuTexture(device, upload, nrm, kDefaultNormalRGBA, false,
                                        nullptr, nullptr, true);

    ImageData orm;
    if (!pbrPath.empty()) {
        orm = LoadImageRGBA(pbrPath);
        resource->hasOrm = orm.ok;
    }
    resource->orm = CreateGpuTexture(device, upload, orm, kDefaultOrmRGBA, false,
                                     nullptr, nullptr, true);

    resource->state = LoadState::kReady;
    cache_.Insert(key, resource);
    return resource;
}

bool TextureManager::IsKnownFailed(const std::string& basePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_.find(basePath) != failed_.end();
}

bool TextureManager::HasResident(const std::string& basePath, const std::string& normalPath,
                                 const std::string& pbrPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.Find(TextureCacheKey(basePath, normalPath, pbrPath)) != nullptr;
}

void TextureManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.Clear();
    placeholder_.reset();
    defaults_.reset();
}

TextureManager& GetTextureManager() {
    static TextureManager sManager;
    return sManager;
}

} // namespace rs::render
