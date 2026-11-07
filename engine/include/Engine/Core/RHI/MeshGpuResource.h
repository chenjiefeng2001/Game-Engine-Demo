#pragma once

#include "Engine/Core/Renderer/Mesh.h"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Engine {

class IGraphicsFactory;
class VertexArray;
class VertexBuffer;
class IndexBuffer;

// P6-1B B5-G2: GL Mesh resource + identity-based cache.
//
// Identity: 64-bit stable asset identity (GUID fold; 0 = anonymous/legacy)
//         + 64-bit GPU payload digest over the exact GPU-consumed payload
//           (vertex bytes, index bytes, span ranges, vertex flags).
// The CPU object address is NEVER used as cache identity.
//
// Owner-thread contract: creation/invalidation/clear must run while the GL
// context is current on the cache owner thread (the EngineHost runtime
// thread in production). Violations fail closed (Acquire returns nullptr /
// Clear returns false) instead of calling GL.

struct MeshGpuSpan
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0xFFFFFFFFu;
};

struct MeshGpuResourceKey
{
    uint64_t identity = 0;
    uint64_t payloadDigest = 0;

    bool operator==(const MeshGpuResourceKey& o) const {
        return identity == o.identity && payloadDigest == o.payloadDigest;
    }
};

struct MeshGpuResourceKeyHash
{
    size_t operator()(const MeshGpuResourceKey& k) const noexcept;
};

// Internal GPU budget caps (NOT the CPU import caps; see B5-G2 record).
inline constexpr uint64_t kMaxGpuMeshVertexBytes = 256ull * 1024 * 1024;
inline constexpr uint64_t kMaxGpuMeshIndexBytes = 256ull * 1024 * 1024;
inline constexpr uint32_t kMaxGpuMeshDrawCount = 0x7FFFFFFFu; // GLsizei

// A fully-initialized GPU mesh. Constructed only by MeshGpuCache; holds the
// GL wrapper objects (VAO/VBO/IBO) whose lifetimes are tied to this resource.
class MeshGpuResource
{
public:
    MeshGpuResource() = delete;

    const std::shared_ptr<VertexArray>& GetVertexArray() const { return m_Vao; }
    const std::shared_ptr<VertexBuffer>& VertexBufferObject() const { return m_Vbo; }
    const std::shared_ptr<IndexBuffer>& IndexBufferObject() const { return m_Ibo; }

    uint32_t VertexCount() const { return m_VertexCount; }
    uint32_t IndexCount() const { return m_IndexCount; }
    uint64_t VertexBytes() const { return m_VertexBytes; }
    uint64_t IndexBytes() const { return m_IndexBytes; }
    const std::vector<MeshGpuSpan>& Spans() const { return m_Spans; }
    const std::vector<uint8_t>& VertexFlags() const { return m_VertexFlags; }
    const MeshGpuResourceKey& Key() const { return m_Key; }
    bool IsValid() const { return m_Valid; }

private:
    friend class MeshGpuCache;
    MeshGpuResource(std::shared_ptr<VertexArray> vao,
                    std::shared_ptr<VertexBuffer> vbo,
                    std::shared_ptr<IndexBuffer> ibo, MeshGpuResourceKey key,
                    uint32_t vertexCount, uint32_t indexCount,
                    uint64_t vertexBytes, uint64_t indexBytes,
                    std::vector<MeshGpuSpan> spans,
                    std::vector<uint8_t> vertexFlags);

    std::shared_ptr<VertexArray> m_Vao;
    std::shared_ptr<VertexBuffer> m_Vbo;
    std::shared_ptr<IndexBuffer> m_Ibo;
    MeshGpuResourceKey m_Key;
    uint32_t m_VertexCount = 0;
    uint32_t m_IndexCount = 0;
    uint64_t m_VertexBytes = 0;
    uint64_t m_IndexBytes = 0;
    std::vector<MeshGpuSpan> m_Spans;
    std::vector<uint8_t> m_VertexFlags;
    bool m_Valid = false;
};

// Test-only failure injection points (production default None).
enum class MeshGpuUploadStage : uint8_t {
    None = 0,
    AfterVertexArray,
    AfterVertexBuffer,
    AfterIndexBuffer,
    AfterAttributeConfig,
};

// Cache API. Acquire: validate → (hit? return existing) → temporary create
// → configure → validate → publish. Any failure releases temporaries and
// leaves the previous entry untouched.
class MeshGpuCache
{
public:
    MeshGpuCache() = default;
    MeshGpuCache(const MeshGpuCache&) = delete;
    MeshGpuCache& operator=(const MeshGpuCache&) = delete;
    ~MeshGpuCache();

    std::shared_ptr<const MeshGpuResource> Acquire(
        const MeshGpuResourceKey& key,
        const std::vector<Vertex3D>& vertices,
        const std::vector<uint32_t>& indices,
        const std::vector<MeshGpuSpan>& spans,
        const std::vector<uint8_t>& vertexFlags,
        IGraphicsFactory& factory);

    bool Invalidate(const MeshGpuResourceKey& key);
    bool InvalidateIdentity(uint64_t identity);
    // Requires a current GL context on the owner thread. Returns false and
    // keeps resources intact when that precondition is not met.
    bool Clear();
    size_t Size() const { return m_Resources.size(); }
    bool Empty() const { return m_Resources.empty(); }

    // FNV-1a-64 over exactly the GPU-consumed payload (+flags). Exposed so
    // import-side adapters can compute the digest once; MeshGpuCache does not
    // trust the address of the CPU object.
    static uint64_t ComputePayloadDigest(const std::vector<Vertex3D>& vertices,
                                         const std::vector<uint32_t>& indices,
                                         const std::vector<MeshGpuSpan>& spans,
                                         const std::vector<uint8_t>& vertexFlags);

    static void SetUploadFailureStageForTesting(MeshGpuUploadStage stage);
    // True when it is safe to call GL creation/teardown right now.
    static bool HasCurrentGlContext();

    // CPU-side payload validation used by Acquire (also directly testable
    // without a GL context): counts, flags size, index range, spans, size caps.
    static bool ValidatePayload(const std::vector<Vertex3D>& vertices,
                                const std::vector<uint32_t>& indices,
                                const std::vector<MeshGpuSpan>& spans,
                                const std::vector<uint8_t>& vertexFlags);

private:
    std::unordered_map<MeshGpuResourceKey, std::shared_ptr<const MeshGpuResource>,
                       MeshGpuResourceKeyHash>
        m_Resources;
    std::thread::id m_OwnerThread; // set on first successful GL acquire
    bool m_OwnerKnown = false;
};

} // namespace Engine
