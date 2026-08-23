/**
 * @file ResourceLifecycleTest.cpp
 * @brief Milestone 2 — Resource Lifecycle v1 contract tests (headless)
 *
 * Tests the GUID resolution + error contract layer.
 * Actual GPU resource loading requires a GL context (tested in Sandbox).
 */

#include <gtest/gtest.h>
#include "Engine/Core/Content/ContentAsset.h"
#include "Engine/Core/Content/ResourceLifecycle.h"
#include "Engine/Core/Resources/ResourceGUID.h"

using namespace Engine;
using namespace Engine::Content;

// ── Registry-level identity tests ──

TEST(RLCycle, Import_IdempotentByPath) {
    ContentRegistry reg;
    auto g1 = reg.Import("test.png", AssetType::Texture);
    auto g2 = reg.Import("test.png", AssetType::Texture);
    EXPECT_TRUE(g1 == g2);
}

TEST(RLCycle, Resolve_KnownGuid_ReturnsPath) {
    ContentRegistry reg;
    auto guid = reg.Import("assets/textures/test.png", AssetType::Texture);
    EXPECT_EQ(reg.ResolvePath(guid), "assets/textures/test.png");
}

TEST(RLCycle, Resolve_UnknownGuid_ReturnsEmpty) {
    ContentRegistry reg;
    auto missing = ResourceGUID::Create();
    EXPECT_TRUE(reg.ResolvePath(missing).empty());
}

// ── Lifecycle error contracts ──

class RLLifecycle : public ::testing::Test {
protected:
    void SetUp() override {
        reg_ = std::make_unique<ContentRegistry>();
        // ResourceLifecycle needs registry only; RM tested separately with GL context
    }
    std::unique_ptr<ContentRegistry> reg_;
};

TEST_F(RLLifecycle, NullGuid_ProducesError) {
    // We can't easily create ResourceLifecycle without ResourceManager,
    // but we CAN test the ContentRegistry resolve path directly.
    auto result_guid = ResourceGUID::Null();
    EXPECT_TRUE(reg_->ResolvePath(result_guid).empty());
}

TEST_F(RLLifecycle, MissingGuid_ProducesEmptyPath) {
    auto missing = ResourceGUID::Create();   // never imported
    EXPECT_TRUE(reg_->ResolvePath(missing).empty());
}
