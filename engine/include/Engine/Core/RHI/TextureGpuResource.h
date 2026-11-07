#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Engine {

class IGraphicsFactory;
class Texture;

// P6-1B B5-G3: GPU texture resource + identity-based cache.
//
// Key = stable texture asset identity (path fold; GUID wiring arrives with the
// asset resolver gate) + encoded-content digest + upload semantics digest
// (colorspace/role/rule version). CPU image object addresses are never used.
// Uploads go through the existing OpenGLGraphicsFactory path (B4 sidecar picks
// the sRGB/Linear internal format); this cache adds lifecycle + identity only.

struct TextureGpuResourceKey
{
    uint64_t identity = 0;
    uint64_t contentDigest = 0;
    uint64_t uploadSemantics = 0;

    bool operator==(const TextureGpuResourceKey& o) const {
        return identity == o.identity && contentDigest == o.contentDigest &&
               uploadSemantics == o.uploadSemantics;
    }
};

struct TextureGpuResourceKeyHash
{
    size_t operator()(const TextureGpuResourceKey& k) const noexcept;
};

class TextureGpuResource
{
public:
    TextureGpuResource() = delete;

    const std::shared_ptr<Texture>& GetTexture() const { return m_Texture; }
    const TextureGpuResourceKey& Key() const { return m_Key; }
    uint32_t Width() const { return m_Width; }
    uint32_t Height() const { return m_Height; }
    bool IsSrgb() const { return m_Srgb; }
    bool IsValid() const { return m_Valid; }

private:
    friend class TextureGpuCache;
    TextureGpuResource(std::shared_ptr<Texture> texture,
                       TextureGpuResourceKey key, uint32_t width,
                       uint32_t height, bool srgb);

    std::shared_ptr<Texture> m_Texture;
    TextureGpuResourceKey m_Key;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    bool m_Srgb = false;
    bool m_Valid = false;
};

class TextureGpuCache
{
public:
    TextureGpuCache() = default;
    TextureGpuCache(const TextureGpuCache&) = delete;
    TextureGpuCache& operator=(const TextureGpuCache&) = delete;
    ~TextureGpuCache();

    // absolutePath must resolve to the committed .tex.png plus its sidecar.
    // expectedWidth/Height are the validated CPU dimensions (collision guard).
    std::shared_ptr<const TextureGpuResource> Acquire(
        const TextureGpuResourceKey& key, const std::string& absolutePath,
        uint32_t expectedWidth, uint32_t expectedHeight, bool srgb,
        IGraphicsFactory& factory);

    bool Invalidate(const TextureGpuResourceKey& key);
    bool Clear();
    size_t Size() const { return m_Resources.size(); }

    static bool HasCurrentGlContext();

    // FNV-1a-64 helpers used by the binding layer for digest construction.
    static uint64_t HashBytes(const void* data, size_t size);
    static uint64_t HashCombine(uint64_t h, uint64_t value);

private:
    std::unordered_map<TextureGpuResourceKey,
                       std::shared_ptr<const TextureGpuResource>,
                       TextureGpuResourceKeyHash>
        m_Resources;
    std::thread::id m_OwnerThread;
    bool m_OwnerKnown = false;
};

} // namespace Engine
