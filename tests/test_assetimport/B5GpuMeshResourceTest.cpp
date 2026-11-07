// P6-1B B5-G2: CPU adapter + real-GL GPU mesh resource/cache tests.
#include "../test_debugdraw/DebugDrawGLFixture.h"

#include "TestSupport.h"

#include "AssetImport/MeshAssetReader.h"
#include "AssetImport/MeshAssetWriter.h"
#include "AssetImport/MeshUploadAdapter.h"
#include "AssetImport/ObjParser.h"
#include "Engine/Core/RHI/MeshGpuResource.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "OpenGL/Resources/OpenGLIndexBuffer.h"
#include "OpenGL/Resources/OpenGLVertexArray.h"
#include "OpenGL/Resources/OpenGLVertexBuffer.h"

#include <gtest/gtest.h>

#include <glad/gl.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <thread>
#include <vector>

namespace {

using namespace AssetImportTest;
using namespace Engine;
using namespace Engine::AssetImport;

// ── helpers ─────────────────────────────────────────────────────────────

MeshAsset MakeTriangleAsset() {
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                "vt 0 0\nvt 1 0\nvt 1 1\nvn 0 0 1\n"
                "f 1/1/1 2/2/1 3/3/1\n"),
        "");
    EXPECT_TRUE(parsed.ok);
    const std::string bytes = SerializeMeshAssetJson(parsed.mesh);
    const MeshAssetReadResult read = ReadMeshAssetJson(ToBytes(bytes));
    EXPECT_TRUE(read.ok) << read.error;
    return read.mesh;
}

std::shared_ptr<const MeshGpuResource> AcquirePrepared(
    MeshGpuCache& cache, IGraphicsFactory& factory, const MeshAsset& asset,
    uint64_t identity = 42) {
    const MeshUploadInputResult prepared = BuildMeshUploadInput(asset);
    EXPECT_TRUE(prepared.ok) << prepared.error;
    MeshGpuResourceKey key;
    key.identity = identity;
    key.payloadDigest = prepared.input.payloadDigest;
    return cache.Acquire(key, prepared.input.vertices, prepared.input.indices,
                         prepared.input.spans, prepared.input.vertexFlags,
                         factory);
}

int64_t LiveTotal() {
    return OpenGLVertexArray::LiveCount() + OpenGLVertexBuffer::LiveCount() +
           OpenGLIndexBuffer::LiveCount();
}

// ── CPU adapter tests ───────────────────────────────────────────────────

TEST(MeshGpuAdapterTest, MapsVertexFieldsAndFlags) {
    const MeshAsset asset = MakeTriangleAsset();
    const MeshUploadInputResult r = BuildMeshUploadInput(asset);
    ASSERT_TRUE(r.ok) << r.error;
    ASSERT_EQ(r.input.vertices.size(), asset.vertices.size());
    for (size_t i = 0; i < asset.vertices.size(); ++i) {
        const MeshAssetVertex& src = asset.vertices[i];
        const Vertex3D& dst = r.input.vertices[i];
        EXPECT_FLOAT_EQ(dst.position.x, src.position[0]);
        EXPECT_FLOAT_EQ(dst.normal.y, src.normal[1]);
        EXPECT_FLOAT_EQ(dst.texCoord.x, src.uv0[0]);
        EXPECT_FLOAT_EQ(dst.tangent.z, src.tangent[2]);
        EXPECT_EQ(r.input.vertexFlags[i], src.flags);
        // Vertex3D is 44B; flags are kept OUT of the GPU vertex layout.
        EXPECT_EQ(sizeof(Vertex3D), 44u);
    }
    EXPECT_TRUE(r.input.indices == asset.indices);
    ASSERT_EQ(r.input.spans.size(), asset.primitives.size());
    EXPECT_EQ(r.input.spans[0].firstIndex, asset.primitives[0].firstIndex);
    EXPECT_EQ(r.input.spans[0].indexCount, asset.primitives[0].indexCount);
    EXPECT_NE(r.input.payloadDigest, 0u);
}

TEST(MeshGpuAdapterTest, MissingNormalIsNotSynthesized) {
    // OBJ without normals -> IR normal flags absent and zeros.
    const ImportResult parsed =
        ObjParser::ParseObj(ToBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"), "");
    ASSERT_TRUE(parsed.ok);
    const MeshAssetReadResult read =
        ReadMeshAssetJson(ToBytes(SerializeMeshAssetJson(parsed.mesh)));
    ASSERT_TRUE(read.ok);
    const MeshUploadInputResult r = BuildMeshUploadInput(read.mesh);
    ASSERT_TRUE(r.ok);
    for (size_t i = 0; i < r.input.vertices.size(); ++i) {
        EXPECT_EQ(r.input.vertices[i].normal.x, 0.0f);
        EXPECT_EQ(r.input.vertices[i].normal.y, 0.0f);
        EXPECT_EQ(r.input.vertices[i].normal.z, 0.0f);
        EXPECT_FALSE(r.input.vertexFlags[i] & kVertexFlagNormalValid);
    }
}

TEST(MeshGpuAdapterTest, MultiSpanPreserved) {
    const ImportResult parsed = ObjParser::ParseObj(
        ToBytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"
                "usemtl a\nf 1 2 3\nusemtl b\nf 1 2 4\n"),
        "");
    ASSERT_TRUE(parsed.ok);
    const MeshAssetReadResult read =
        ReadMeshAssetJson(ToBytes(SerializeMeshAssetJson(parsed.mesh)));
    ASSERT_TRUE(read.ok);
    const MeshUploadInputResult r = BuildMeshUploadInput(read.mesh);
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.input.spans.size(), 2u);
    EXPECT_EQ(r.input.spans[0].firstIndex, 0u);
    EXPECT_EQ(r.input.spans[1].firstIndex, 3u);
}

TEST(MeshGpuAdapterTest, ValidatePayloadRejectsIllegalData) {
    MeshUploadInputResult good = BuildMeshUploadInput(MakeTriangleAsset());
    ASSERT_TRUE(good.ok);
    const auto& in = good.input;

    EXPECT_TRUE(MeshGpuCache::ValidatePayload(in.vertices, in.indices,
                                              in.spans, in.vertexFlags));

    // index out of range
    {
        auto indices = in.indices;
        indices[0] = 99;
        EXPECT_FALSE(MeshGpuCache::ValidatePayload(in.vertices, indices,
                                                   in.spans,
                                                   in.vertexFlags));
    }
    // not triangle multiple
    {
        auto indices = in.indices;
        indices.pop_back();
        EXPECT_FALSE(MeshGpuCache::ValidatePayload(in.vertices, indices,
                                                   in.spans,
                                                   in.vertexFlags));
    }
    // empty
    EXPECT_FALSE(MeshGpuCache::ValidatePayload({}, in.indices, in.spans,
                                               in.vertexFlags));
    // span zero count
    {
        auto spans = in.spans;
        spans[0].indexCount = 0;
        EXPECT_FALSE(MeshGpuCache::ValidatePayload(in.vertices, in.indices,
                                                   spans, in.vertexFlags));
    }
    // span exceeds range
    {
        auto spans = in.spans;
        spans[0].indexCount = 99;
        EXPECT_FALSE(MeshGpuCache::ValidatePayload(in.vertices, in.indices,
                                                   spans, in.vertexFlags));
    }
    // flags size mismatch
    {
        auto flags = in.vertexFlags;
        flags.pop_back();
        EXPECT_FALSE(MeshGpuCache::ValidatePayload(in.vertices, in.indices,
                                                   in.spans, flags));
    }
}

TEST(MeshContentHashTest, ContentAddressedNotPointer) {
    Mesh a({{Vec3(0, 0, 0), Vec3(0, 0, 1), Vec2(0, 0), Vec3(1, 0, 0)},
            {Vec3(1, 0, 0), Vec3(0, 0, 1), Vec2(1, 0), Vec3(1, 0, 0)},
            {Vec3(0, 1, 0), Vec3(0, 0, 1), Vec2(0, 1), Vec3(1, 0, 0)}},
           {0, 1, 2});
    Mesh b = a;
    EXPECT_EQ(a.GetContentHash(), b.GetContentHash());
    b.SetIndices({0, 2, 1});
    EXPECT_NE(a.GetContentHash(), b.GetContentHash());
    b.SetIndices({0, 1, 2});
    EXPECT_EQ(a.GetContentHash(), b.GetContentHash());
}

// ── real GL tests ──────────────────────────────────────────────────────

TEST(MeshGpuResourceGLTest, UploadsBuffersAndAttributeLayout) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    auto& gl = fixture.GL();
    auto& factory = fixture.Factory();

    const MeshAsset asset = MakeTriangleAsset();
    const MeshUploadInputResult prepared = BuildMeshUploadInput(asset);
    ASSERT_TRUE(prepared.ok);

    MeshGpuCache cache;
    const auto resource = AcquirePrepared(cache, factory, asset);
    ASSERT_NE(resource, nullptr);
    EXPECT_TRUE(resource->IsValid());
    EXPECT_EQ(resource->VertexCount(), prepared.input.vertices.size());
    EXPECT_EQ(resource->IndexCount(), prepared.input.indices.size());
    EXPECT_EQ(resource->VertexBytes(),
              prepared.input.vertices.size() * sizeof(Vertex3D));
    EXPECT_EQ(resource->IndexBytes(),
              prepared.input.indices.size() * sizeof(uint32_t));
    ASSERT_EQ(resource->Spans().size(), prepared.input.spans.size());

    // Buffer sizes
    resource->VertexBufferObject()->Bind();
    GLint vboSize = 0;
    gl.GetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);
    EXPECT_EQ(static_cast<uint32>(vboSize),
              static_cast<uint32>(resource->VertexBytes()));

    // VBO content matches CPU bytes exactly.
    std::vector<uint8_t> vboData(resource->VertexBytes());
    gl.GetBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(vboData.size()),
                        vboData.data());
    EXPECT_EQ(std::memcmp(vboData.data(), prepared.input.vertices.data(),
                          vboData.size()),
              0);

    // Index count from VAO + attribute layout from bound VAO.
    resource->GetVertexArray()->Bind();
    ASSERT_NE(resource->GetVertexArray()->GetIndexBuffer(), nullptr);
    EXPECT_EQ(resource->GetVertexArray()->GetIndexBuffer()->GetCount(),
              prepared.input.indices.size());

    auto checkAttrib = [&](GLuint index, GLint size, GLsizei stride,
                           GLintptr offset) {
        GLint enabled = 0, type = 0, count = 0, Stride = 0;
        gl.GetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        gl.GetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
        gl.GetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &count);
        gl.GetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &Stride);
        void* ptr = nullptr;
        gl.GetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, &ptr);
        const GLintptr Offset = reinterpret_cast<GLintptr>(ptr);
        EXPECT_EQ(enabled, GL_TRUE);
        EXPECT_EQ(type, GL_FLOAT);
        EXPECT_EQ(count, size);
        EXPECT_EQ(Stride, stride);
        EXPECT_EQ(static_cast<GLintptr>(Offset), offset);
    };
    checkAttrib(0, 3, sizeof(Vertex3D), offsetof(Vertex3D, position));
    checkAttrib(1, 3, sizeof(Vertex3D), offsetof(Vertex3D, normal));
    checkAttrib(2, 2, sizeof(Vertex3D), offsetof(Vertex3D, texCoord));
    checkAttrib(3, 3, sizeof(Vertex3D), offsetof(Vertex3D, tangent));

    // Index buffer bytes.
    resource->IndexBufferObject()->Bind();
    std::vector<uint32_t> iboData(prepared.input.indices.size());
    gl.GetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(resource->IndexBytes()),
                        iboData.data());
    EXPECT_EQ(std::memcmp(iboData.data(), prepared.input.indices.data(),
                          resource->IndexBytes()),
              0);
}

TEST(MeshGpuResourceGLTest, CacheReuseAndPayloadIdentity) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    MeshGpuCache cache;
    const MeshAsset asset = MakeTriangleAsset();
    const auto a = AcquirePrepared(cache, fixture.Factory(), asset, 7);
    ASSERT_NE(a, nullptr);
    const auto b = AcquirePrepared(cache, fixture.Factory(), asset, 7);
    EXPECT_EQ(a.get(), b.get());
    EXPECT_EQ(cache.Size(), 1u);

    // Same payload under a different identity -> separate entry.
    const auto c = AcquirePrepared(cache, fixture.Factory(), asset, 8);
    ASSERT_NE(c, nullptr);
    EXPECT_NE(a.get(), c.get());
    EXPECT_EQ(cache.Size(), 2u);

    // Changed payload under the same identity -> new resource; old remains.
    MeshAsset changed = asset;
    changed.vertices[0].position[0] = 5.0f;
    const auto d = AcquirePrepared(cache, fixture.Factory(), changed, 7);
    ASSERT_NE(d, nullptr);
    EXPECT_NE(a.get(), d.get());
    EXPECT_EQ(cache.Size(), 3u);
    EXPECT_TRUE(a->IsValid());
}

TEST(MeshGpuResourceGLTest, InvalidateAndClearReleaseHandles) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    MeshGpuCache cache;
    const MeshAsset asset = MakeTriangleAsset();
    const int64_t baseline = LiveTotal();

    auto a = AcquirePrepared(cache, fixture.Factory(), asset, 1);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(LiveTotal(), baseline + 3); // VAO + VBO + IBO

    EXPECT_TRUE(cache.Invalidate(a->Key()));
    EXPECT_EQ(cache.Size(), 0u);
    a.reset();
    EXPECT_EQ(LiveTotal(), baseline);

    auto b = AcquirePrepared(cache, fixture.Factory(), asset, 2);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_TRUE(cache.Clear());
    EXPECT_EQ(cache.Size(), 0u);
    b.reset();
    EXPECT_EQ(LiveTotal(), baseline);
    EXPECT_TRUE(cache.Clear()); // idempotent
}

TEST(MeshGpuResourceGLTest, UploadFailureRollsBackCompletely) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    MeshGpuCache cache;
    const int64_t baseline = LiveTotal();

    const MeshAsset asset = MakeTriangleAsset();
    const MeshUploadInputResult prepared = BuildMeshUploadInput(asset);
    ASSERT_TRUE(prepared.ok);
    MeshGpuResourceKey key;
    key.identity = 1;
    key.payloadDigest = prepared.input.payloadDigest;

    const MeshGpuUploadStage stages[] = {
        MeshGpuUploadStage::AfterVertexArray,
        MeshGpuUploadStage::AfterVertexBuffer,
        MeshGpuUploadStage::AfterIndexBuffer,
        MeshGpuUploadStage::AfterAttributeConfig,
    };
    for (const auto stage : stages) {
        MeshGpuCache::SetUploadFailureStageForTesting(stage);
        const auto failed =
            cache.Acquire(key, prepared.input.vertices, prepared.input.indices,
                          prepared.input.spans, prepared.input.vertexFlags,
                          fixture.Factory());
        EXPECT_EQ(failed, nullptr);
        EXPECT_EQ(cache.Size(), 0u);
        EXPECT_EQ(LiveTotal(), baseline) << "leak at stage "
                                         << static_cast<int>(stage);
    }
    MeshGpuCache::SetUploadFailureStageForTesting(MeshGpuUploadStage::None);
    const auto ok = cache.Acquire(key, prepared.input.vertices,
                                  prepared.input.indices, prepared.input.spans,
                                  prepared.input.vertexFlags,
                                  fixture.Factory());
    ASSERT_NE(ok, nullptr);
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_TRUE(cache.Clear());
}

TEST(MeshGpuResourceGLTest, FailedNewVersionPreservesOldEntry) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    MeshGpuCache cache;
    const int64_t baseline = LiveTotal();

    const MeshAsset asset = MakeTriangleAsset();
    auto old = AcquirePrepared(cache, fixture.Factory(), asset, 3);
    ASSERT_NE(old, nullptr);
    const int64_t withOld = LiveTotal();
    EXPECT_EQ(withOld, baseline + 3);

    MeshAsset changed = asset;
    changed.vertices[1].position[1] = 9.0f;
    const MeshUploadInputResult prepared = BuildMeshUploadInput(changed);
    ASSERT_TRUE(prepared.ok);
    MeshGpuResourceKey key;
    key.identity = 3;
    key.payloadDigest = prepared.input.payloadDigest;

    MeshGpuCache::SetUploadFailureStageForTesting(
        MeshGpuUploadStage::AfterAttributeConfig);
    const auto failed = cache.Acquire(key, prepared.input.vertices,
                                      prepared.input.indices,
                                      prepared.input.spans,
                                      prepared.input.vertexFlags,
                                      fixture.Factory());
    EXPECT_EQ(failed, nullptr);
    EXPECT_EQ(cache.Size(), 1u); // old entry intact
    EXPECT_TRUE(old->IsValid());
    EXPECT_EQ(LiveTotal(), withOld); // no leaked temporaries
    MeshGpuCache::SetUploadFailureStageForTesting(MeshGpuUploadStage::None);
    EXPECT_TRUE(cache.Clear());
    old.reset();
    EXPECT_EQ(LiveTotal(), baseline);
}

TEST(MeshGpuResourceGLTest, NonOwnerThreadIsRejectedWithoutGL) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    MeshGpuCache cache;
    const int64_t baseline = LiveTotal();

    const MeshAsset asset = MakeTriangleAsset();
    const MeshUploadInputResult prepared = BuildMeshUploadInput(asset);
    ASSERT_TRUE(prepared.ok);
    MeshGpuResourceKey key;
    key.identity = 9;
    key.payloadDigest = prepared.input.payloadDigest;

    std::shared_ptr<const MeshGpuResource> fromThread;
    std::thread worker([&] {
        fromThread = cache.Acquire(key, prepared.input.vertices,
                                   prepared.input.indices,
                                   prepared.input.spans,
                                   prepared.input.vertexFlags,
                                   fixture.Factory());
    });
    worker.join();
    EXPECT_EQ(fromThread, nullptr);
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(LiveTotal(), baseline);
}

TEST(MeshGpuResourceGLTest, InvalidPayloadRejectedBeforeGL) {
    Engine::Rendering::DebugDraw::Test::DebugDrawGLFixture fixture;
    ASSERT_TRUE(fixture.Setup()) << fixture.FailStep();
    MeshGpuCache cache;
    const int64_t baseline = LiveTotal();
    const MeshAsset asset = MakeTriangleAsset();
    MeshUploadInputResult prepared = BuildMeshUploadInput(asset);
    ASSERT_TRUE(prepared.ok);
    MeshGpuResourceKey key;
    key.identity = 4;
    key.payloadDigest = prepared.input.payloadDigest;

    auto badIndices = prepared.input.indices;
    badIndices[0] = 999;
    EXPECT_EQ(cache.Acquire(key, prepared.input.vertices, badIndices,
                            prepared.input.spans, prepared.input.vertexFlags,
                            fixture.Factory()),
              nullptr);
    auto badFlags = prepared.input.vertexFlags;
    badFlags.pop_back();
    EXPECT_EQ(cache.Acquire(key, prepared.input.vertices,
                            prepared.input.indices, prepared.input.spans,
                            badFlags, fixture.Factory()),
              nullptr);
    EXPECT_EQ(cache.Size(), 0u);
    EXPECT_EQ(LiveTotal(), baseline);
}

} // namespace
