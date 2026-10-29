/**
 * @file GpuTimestampProfilerTest.cpp
 * @brief T-4 GPU timestamp profiler tests (backend-neutral + Vulkan).
 */

#include <gtest/gtest.h>

#include "Engine/Core/RHI/GpuTimestampProfiler.h"
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/RHIBackend.h"
#include "Engine/Core/RHI/RHIDeviceFactory.h"

using namespace Engine;
using namespace Engine::RHI;

TEST(GpuTimestampProfilerTest, NullDeviceIsExplicitlyUnavailable) {
    auto profiler = CreateGpuTimestampProfiler();
    ASSERT_NE(profiler, nullptr);
    EXPECT_FALSE(profiler->Initialize(nullptr, 4));
    EXPECT_FALSE(profiler->IsAvailable());
    EXPECT_TRUE(profiler->Resolve().empty());
    profiler->Shutdown();
}

TEST(GpuTimestampProfilerTest, VulkanTimestampIsRealOrExplicitlyUnavailable) {
    auto device = CreateRHIDevice(Backend::Vulkan, nullptr, 16, 16);
    if (!device)
        GTEST_SKIP() << "Vulkan device unavailable on this runner";
    if (!device->SupportsGpuTimestamps())
        GTEST_SKIP() << "Vulkan queue family does not expose timestampValidBits";

    auto profiler = CreateGpuTimestampProfiler();
    ASSERT_NE(profiler, nullptr);
    ASSERT_TRUE(profiler->Initialize(device.get(), 4));
    EXPECT_TRUE(profiler->IsAvailable());

    auto cmd = device->CreateCommandList(CommandListType::Direct);
    ASSERT_NE(cmd, nullptr);

    profiler->BeginFrame();
    cmd->Begin();
    const uint32_t segment = profiler->BeginSegment(*cmd, "vk_empty_pass");
    ASSERT_NE(segment, IRHICommandList::kInvalidTimestamp);
    profiler->EndSegment(*cmd, segment);
    cmd->End();
    cmd->Submit();
    device->WaitIdle();

    const auto& results = profiler->Resolve();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].valid);
    EXPECT_GE(results[0].gpuMs, 0.0);
    EXPECT_NE(results[0].gpuMs, 0.5); // old stub's placeholder must not return
}

TEST(GpuTimestampProfilerTest, MaxSegmentsRejectsExtraSegment) {
    auto device = CreateRHIDevice(Backend::Vulkan, nullptr, 16, 16);
    if (!device || !device->SupportsGpuTimestamps())
        GTEST_SKIP() << "Vulkan timestamps unavailable on this runner";

    auto profiler = CreateGpuTimestampProfiler();
    ASSERT_NE(profiler, nullptr);
    ASSERT_TRUE(profiler->Initialize(device.get(), 1));

    auto cmd = device->CreateCommandList(CommandListType::Direct);
    ASSERT_NE(cmd, nullptr);
    profiler->BeginFrame();
    cmd->Begin();
    const uint32_t first = profiler->BeginSegment(*cmd, "one");
    ASSERT_NE(first, IRHICommandList::kInvalidTimestamp);
    EXPECT_EQ(profiler->BeginSegment(*cmd, "two"), UINT32_MAX);
    profiler->EndSegment(*cmd, first);
    cmd->End();
    cmd->Submit();
    device->WaitIdle();
    const auto& results = profiler->Resolve();
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].name, "one");
    EXPECT_TRUE(results[0].valid);
}
