#include "Engine/Core/RHI/TextureGpuResource.h"

#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/Resources/Resource.h"

#include <GLFW/glfw3.h>

namespace Engine {

namespace {

constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

} // namespace

size_t TextureGpuResourceKeyHash::operator()(
    const TextureGpuResourceKey& k) const noexcept {
    uint64_t h = kFnvOffset;
    h = (h ^ k.identity) * kFnvPrime;
    h = (h ^ k.contentDigest) * kFnvPrime;
    h = (h ^ k.uploadSemantics) * kFnvPrime;
    return static_cast<size_t>(h);
}

uint64_t TextureGpuCache::HashBytes(const void* data, size_t size) {
    uint64_t h = kFnvOffset;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}

uint64_t TextureGpuCache::HashCombine(uint64_t h, uint64_t value) {
    return (h ^ value) * kFnvPrime;
}

TextureGpuResource::TextureGpuResource(std::shared_ptr<Texture> texture,
                                       TextureGpuResourceKey key,
                                       uint32_t width, uint32_t height,
                                       bool srgb)
    : m_Texture(std::move(texture))
    , m_Key(key)
    , m_Width(width)
    , m_Height(height)
    , m_Srgb(srgb)
    , m_Valid(m_Texture && m_Texture->GetState() == ResourceState::Loaded &&
              m_Texture->GetWidth() == width &&
              m_Texture->GetHeight() == height) {}

TextureGpuCache::~TextureGpuCache() {
    if (!m_Resources.empty() && HasCurrentGlContext()) {
        (void)Clear();
    }
}

bool TextureGpuCache::HasCurrentGlContext() {
    return glfwGetCurrentContext() != nullptr;
}

std::shared_ptr<const TextureGpuResource> TextureGpuCache::Acquire(
    const TextureGpuResourceKey& key, const std::string& absolutePath,
    uint32_t expectedWidth, uint32_t expectedHeight, bool srgb,
    IGraphicsFactory& factory) {
    if (!HasCurrentGlContext()) return nullptr;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return nullptr;
    if (!m_OwnerKnown) {
        m_OwnerThread = std::this_thread::get_id();
        m_OwnerKnown = true;
    }
    if (absolutePath.empty() || expectedWidth == 0 || expectedHeight == 0)
        return nullptr;

    auto it = m_Resources.find(key);
    if (it != m_Resources.end()) {
        if (it->second->Width() == expectedWidth &&
            it->second->Height() == expectedHeight &&
            it->second->IsSrgb() == srgb)
            return it->second;
        return nullptr; // digest collision with different semantics
    }

    // The factory path is the B4-verified GL upload path: it reads the sidecar
    // and selects GL_SRGB8_ALPHA8 / GL_RGBA8 accordingly.
    std::shared_ptr<Texture> texture = factory.CreateTexture(absolutePath);
    if (!texture) return nullptr;

    auto resource = std::shared_ptr<const TextureGpuResource>(
        new TextureGpuResource(std::move(texture), key, expectedWidth,
                               expectedHeight, srgb));
    if (!resource->IsValid()) {
        // Dropped here while the context is current; no cache entry published.
        return nullptr;
    }
    m_Resources.emplace(key, resource);
    return resource;
}

bool TextureGpuCache::Invalidate(const TextureGpuResourceKey& key) {
    if (m_Resources.find(key) == m_Resources.end()) return true;
    if (!HasCurrentGlContext()) return false;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return false;
    m_Resources.erase(key);
    return true;
}

bool TextureGpuCache::Clear() {
    if (m_Resources.empty()) return true;
    if (!HasCurrentGlContext()) return false;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return false;
    m_Resources.clear();
    return true;
}

} // namespace Engine
