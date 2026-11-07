#include "Engine/Core/RHI/MeshGpuResource.h"

#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"

#include <GLFW/glfw3.h>

#include <atomic>
#include <cstring>

namespace Engine {

namespace {

static_assert(sizeof(Vertex3D) == 44, "Vertex3D ABI is frozen at 44 bytes");
static_assert(offsetof(Vertex3D, position) == 0, "Vertex3D position offset");
static_assert(offsetof(Vertex3D, normal) == 12, "Vertex3D normal offset");
static_assert(offsetof(Vertex3D, texCoord) == 24, "Vertex3D texCoord offset");
static_assert(offsetof(Vertex3D, tangent) == 32, "Vertex3D tangent offset");

std::atomic<int> g_FailStage{static_cast<int>(MeshGpuUploadStage::None)};

uint64_t Fnv1aUpdate(uint64_t h, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t Fnv1aStart() { return 0xcbf29ce484222325ull; }

bool Injected(MeshGpuUploadStage stage) {
    return g_FailStage.load(std::memory_order_relaxed) ==
           static_cast<int>(stage);
}

bool SpansValid(const std::vector<MeshGpuSpan>& spans, uint32_t indexCount) {
    if (spans.empty()) return false;
    uint64_t prevEnd = 0;
    for (const MeshGpuSpan& s : spans) {
        if (s.indexCount == 0) return false;
        const uint64_t end = static_cast<uint64_t>(s.firstIndex) + s.indexCount;
        if (end > indexCount) return false;
        if (static_cast<uint64_t>(s.firstIndex) < prevEnd) return false;
        prevEnd = end;
    }
    return true;
}

} // namespace

size_t MeshGpuResourceKeyHash::operator()(const MeshGpuResourceKey& k) const
    noexcept {
    // Mix both fields; FNV-style combine.
    uint64_t h = 0xcbf29ce484222325ull;
    h ^= k.identity;
    h *= 0x100000001b3ull;
    h ^= k.payloadDigest;
    h *= 0x100000001b3ull;
    return static_cast<size_t>(h);
}

MeshGpuResource::MeshGpuResource(
    std::shared_ptr<VertexArray> vao, std::shared_ptr<VertexBuffer> vbo,
    std::shared_ptr<IndexBuffer> ibo, MeshGpuResourceKey key,
    uint32_t vertexCount, uint32_t indexCount, uint64_t vertexBytes,
    uint64_t indexBytes, std::vector<MeshGpuSpan> spans,
    std::vector<uint8_t> vertexFlags)
    : m_Vao(std::move(vao))
    , m_Vbo(std::move(vbo))
    , m_Ibo(std::move(ibo))
    , m_Key(key)
    , m_VertexCount(vertexCount)
    , m_IndexCount(indexCount)
    , m_VertexBytes(vertexBytes)
    , m_IndexBytes(indexBytes)
    , m_Spans(std::move(spans))
    , m_VertexFlags(std::move(vertexFlags))
    , m_Valid(m_Vao && m_Vbo && m_Ibo) {}

MeshGpuCache::~MeshGpuCache() {
    // Best effort: only touch GL when the context is current; the required
    // shutdown path calls Clear() explicitly while the context is current.
    if (!m_Resources.empty() && HasCurrentGlContext()) {
        (void)Clear();
    }
}

bool MeshGpuCache::HasCurrentGlContext() {
    return glfwGetCurrentContext() != nullptr;
}

uint64_t MeshGpuCache::ComputePayloadDigest(
    const std::vector<Vertex3D>& vertices, const std::vector<uint32_t>& indices,
    const std::vector<MeshGpuSpan>& spans,
    const std::vector<uint8_t>& vertexFlags) {
    uint64_t h = Fnv1aStart();
    const uint64_t vCount = vertices.size();
    const uint64_t iCount = indices.size();
    const uint64_t sCount = spans.size();
    const uint64_t fCount = vertexFlags.size();
    h = Fnv1aUpdate(h, &vCount, sizeof(vCount));
    if (!vertices.empty())
        h = Fnv1aUpdate(h, vertices.data(),
                        vertices.size() * sizeof(Vertex3D));
    h = Fnv1aUpdate(h, &iCount, sizeof(iCount));
    if (!indices.empty())
        h = Fnv1aUpdate(h, indices.data(), indices.size() * sizeof(uint32_t));
    h = Fnv1aUpdate(h, &sCount, sizeof(sCount));
    for (const MeshGpuSpan& s : spans) {
        h = Fnv1aUpdate(h, &s.firstIndex, sizeof(s.firstIndex));
        h = Fnv1aUpdate(h, &s.indexCount, sizeof(s.indexCount));
        h = Fnv1aUpdate(h, &s.materialIndex, sizeof(s.materialIndex));
    }
    h = Fnv1aUpdate(h, &fCount, sizeof(fCount));
    if (!vertexFlags.empty())
        h = Fnv1aUpdate(h, vertexFlags.data(), vertexFlags.size());
    return h;
}

void MeshGpuCache::SetUploadFailureStageForTesting(MeshGpuUploadStage stage) {
    g_FailStage.store(static_cast<int>(stage), std::memory_order_relaxed);
}

bool MeshGpuCache::ValidatePayload(const std::vector<Vertex3D>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   const std::vector<MeshGpuSpan>& spans,
                                   const std::vector<uint8_t>& vertexFlags) {
    if (vertices.empty() || indices.empty()) return false;
    if (vertexFlags.size() != vertices.size()) return false;
    const uint64_t vertexBytes =
        static_cast<uint64_t>(vertices.size()) * sizeof(Vertex3D);
    const uint64_t indexBytes =
        static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
    if (vertexBytes > kMaxGpuMeshVertexBytes || vertexBytes > 0xFFFFFFFFull)
        return false;
    if (indexBytes > kMaxGpuMeshIndexBytes || indexBytes > 0xFFFFFFFFull)
        return false;
    if (indices.size() > kMaxGpuMeshDrawCount) return false;
    if (indices.size() % 3 != 0) return false;
    for (uint32_t index : indices) {
        if (index >= vertices.size()) return false;
    }
    if (!SpansValid(spans, static_cast<uint32_t>(indices.size())))
        return false;
    return true;
}

std::shared_ptr<const MeshGpuResource> MeshGpuCache::Acquire(
    const MeshGpuResourceKey& key, const std::vector<Vertex3D>& vertices,
    const std::vector<uint32_t>& indices, const std::vector<MeshGpuSpan>& spans,
    const std::vector<uint8_t>& vertexFlags, IGraphicsFactory& factory) {
    if (!HasCurrentGlContext()) return nullptr;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return nullptr;
    if (!m_OwnerKnown) {
        m_OwnerThread = std::this_thread::get_id();
        m_OwnerKnown = true;
    }

    if (!ValidatePayload(vertices, indices, spans, vertexFlags))
        return nullptr;
    const uint64_t vertexBytes =
        static_cast<uint64_t>(vertices.size()) * sizeof(Vertex3D);
    const uint64_t indexBytes =
        static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);

    // ── cache hit (digest + size guard against hash collisions) ────────
    auto it = m_Resources.find(key);
    if (it != m_Resources.end()) {
        const MeshGpuResource& existing = *it->second;
        if (existing.VertexCount() == vertices.size() &&
            existing.IndexCount() == indices.size() &&
            existing.Spans().size() == spans.size())
            return it->second;
        // Digest collision with different payload sizes: fail closed.
        return nullptr;
    }

    // ── temporary creation (rolled back by scope exit on any failure) ──
    std::shared_ptr<VertexArray> vao = factory.CreateVertexArray();
    if (!vao || Injected(MeshGpuUploadStage::AfterVertexArray)) return nullptr;

    std::shared_ptr<VertexBuffer> vbo = factory.CreateVertexBuffer(
        reinterpret_cast<float*>(const_cast<Vertex3D*>(vertices.data())),
        static_cast<uint32_t>(vertexBytes));
    if (!vbo || Injected(MeshGpuUploadStage::AfterVertexBuffer)) return nullptr;

    std::shared_ptr<IndexBuffer> ibo = factory.CreateIndexBuffer(
        const_cast<uint32_t*>(indices.data()),
        static_cast<uint32_t>(indices.size()));
    if (!ibo || Injected(MeshGpuUploadStage::AfterIndexBuffer)) return nullptr;

    VertexAttribute attributes[4] = {
        {0, 3, sizeof(Vertex3D), offsetof(Vertex3D, position)},
        {1, 3, sizeof(Vertex3D), offsetof(Vertex3D, normal)},
        {2, 2, sizeof(Vertex3D), offsetof(Vertex3D, texCoord)},
        {3, 3, sizeof(Vertex3D), offsetof(Vertex3D, tangent)},
    };
    vao->AddVertexBuffer(vbo, attributes, 4);
    vao->SetIndexBuffer(ibo);
    if (Injected(MeshGpuUploadStage::AfterAttributeConfig)) return nullptr;

    // final validation before publish
    if (vao->GetIndexBuffer() != ibo || ibo->GetCount() != indices.size())
        return nullptr;

    auto resource = std::shared_ptr<const MeshGpuResource>(
        new MeshGpuResource(std::move(vao), std::move(vbo), std::move(ibo), key,
                            static_cast<uint32_t>(vertices.size()),
                            static_cast<uint32_t>(indices.size()), vertexBytes,
                            indexBytes, spans, vertexFlags));
    if (!resource->IsValid()) return nullptr;
    m_Resources.emplace(key, resource);
    return resource;
}

bool MeshGpuCache::Invalidate(const MeshGpuResourceKey& key) {
    if (m_Resources.find(key) == m_Resources.end()) return true;
    if (!HasCurrentGlContext()) return false;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return false;
    m_Resources.erase(key);
    return true;
}

bool MeshGpuCache::InvalidateIdentity(uint64_t identity) {
    if (m_Resources.empty()) return true;
    if (!HasCurrentGlContext()) return false;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return false;
    for (auto it = m_Resources.begin(); it != m_Resources.end();) {
        if (it->first.identity == identity)
            it = m_Resources.erase(it);
        else
            ++it;
    }
    return true;
}

bool MeshGpuCache::Clear() {
    if (m_Resources.empty()) return true;
    if (!HasCurrentGlContext()) return false;
    if (m_OwnerKnown && std::this_thread::get_id() != m_OwnerThread)
        return false;
    m_Resources.clear();
    return true;
}

} // namespace Engine
