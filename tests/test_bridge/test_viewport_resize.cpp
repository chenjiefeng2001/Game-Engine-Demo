#include <gtest/gtest.h>
#include "editor_bridge/capi.h"
#include "EngineHost.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace {

static void SetFailureFlag(const char* name, bool enabled) {
#ifdef _WIN32
    _putenv_s(name, enabled ? "1" : "");
#else
    if (enabled) setenv(name, "1", 1);
    else unsetenv(name);
#endif
}

static editor_bridge::EngineHost* AsHost(EngineHostHandle handle) {
    return static_cast<editor_bridge::EngineHost*>(handle);
}

} // namespace

TEST(EngineHostViewportResizeTest, InvalidDimensionsAreRejectedWithoutChangingTarget) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    auto result = RuntimeResult{};
    EngineHostHandle handle = EngineHost_Create(&result);
    ASSERT_NE(handle, nullptr);
    auto* host = AsHost(handle);

    EXPECT_EQ(host->PresentationTargetWidth(), ENGINE_HOST_PRESENTATION_DEFAULT_WIDTH);
    EXPECT_EQ(host->PresentationTargetHeight(), ENGINE_HOST_PRESENTATION_DEFAULT_HEIGHT);

    struct Case { float w; float h; };
    const Case cases[] = {
        {0.0f, 600.0f},
        {800.0f, 0.0f},
        {-1.0f, 600.0f},
        {600.0f, -1.0f},
        {static_cast<float>(ENGINE_HOST_PRESENTATION_MAX_WIDTH) + 1.0f, 600.0f},
        {600.0f, static_cast<float>(ENGINE_HOST_PRESENTATION_MAX_HEIGHT) + 1.0f},
        {0.4f, 600.0f},
        {600.0f, 0.4f},
        {std::numeric_limits<float>::quiet_NaN(), 600.0f},
        {600.0f, std::numeric_limits<float>::infinity()},
    };

    for (const Case& c : cases) {
        float arg[2] = {c.w, c.h};
        auto r = RuntimeResult{};
        EXPECT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                     nullptr, nullptr, nullptr, arg, &r), -1)
            << "invalid size " << c.w << "x" << c.h << " must be rejected";
        EXPECT_EQ(r.status, ENGINE_HOST_STATUS_INVALID_STATE)
            << "invalid size must report INVALID_STATE";
        EXPECT_EQ(host->PresentationTargetWidth(), ENGINE_HOST_PRESENTATION_DEFAULT_WIDTH);
        EXPECT_EQ(host->PresentationTargetHeight(), ENGINE_HOST_PRESENTATION_DEFAULT_HEIGHT);
    }

    {
        auto r = RuntimeResult{};
        EXPECT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                     nullptr, nullptr, nullptr, nullptr, &r), -1);
        EXPECT_EQ(r.status, ENGINE_HOST_STATUS_INVALID_STATE);
    }

    EngineHost_Destroy(handle);
}

TEST(EngineHostViewportResizeTest, QueuedResizeRequestsResolveToTheLastValidRequest) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    auto result = RuntimeResult{};
    EngineHostHandle handle = EngineHost_Create(&result);
    ASSERT_NE(handle, nullptr);
    auto* host = AsHost(handle);

    float first[2] = {640.0f, 480.0f};
    float second[2] = {900.0f, 700.0f};
    auto r1 = RuntimeResult{};
    auto r2 = RuntimeResult{};
    ASSERT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                 nullptr, nullptr, nullptr, first, &r1), 0);
    ASSERT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                 nullptr, nullptr, nullptr, second, &r2), 0);

    auto frameResult = RuntimeResult{};
    ASSERT_EQ(EngineHost_PumpOneFrame(handle, 0.016f, &frameResult), 0);

    EXPECT_EQ(host->PresentationTargetWidth(), 900);
    EXPECT_EQ(host->PresentationTargetHeight(), 700);

    std::vector<uint8_t> packet(ENGINE_HOST_FRAME_PACKET_BYTES, 0xCD);
    auto fetchResult = RuntimeResult{};
    const int32_t rc = EngineHost_Fetch(handle, ENGINE_HOST_OP_GET_PRESENTATION_FRAME,
                                        0, nullptr, nullptr, nullptr,
                                        packet.data(),
                                        static_cast<int32_t>(packet.size()), &fetchResult);
    ASSERT_EQ(rc, ENGINE_HOST_FRAME_PACKET_BYTES_FOR(900, 700));
    ASSERT_EQ(fetchResult.status, ENGINE_HOST_STATUS_SUCCESS);
    int32_t width = 0, height = 0;
    std::memcpy(&width, packet.data() + 0, sizeof(int32_t));
    std::memcpy(&height, packet.data() + 4, sizeof(int32_t));
    EXPECT_EQ(width, 900);
    EXPECT_EQ(height, 700);

    EngineHost_Destroy(handle);
}

TEST(EngineHostViewportResizeTest, ResizeFailureIsFailClosedAndLaterRecovers) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    auto result = RuntimeResult{};
    EngineHostHandle handle = EngineHost_Create(&result);
    ASSERT_NE(handle, nullptr);
    auto* host = AsHost(handle);

    SetFailureFlag("EDITOR_BRIDGE_FAIL_VIEWPORT_RESIZE", true);
    float requested[2] = {640.0f, 480.0f};
    auto dispatchResult = RuntimeResult{};
    ASSERT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                 nullptr, nullptr, nullptr, requested, &dispatchResult), 0);

    auto frameResult = RuntimeResult{};
    ASSERT_EQ(EngineHost_PumpOneFrame(handle, 0.016f, &frameResult), 0);
    EXPECT_EQ(host->PresentationTargetWidth(), ENGINE_HOST_PRESENTATION_DEFAULT_WIDTH);
    EXPECT_EQ(host->PresentationTargetHeight(), ENGINE_HOST_PRESENTATION_DEFAULT_HEIGHT);

    std::vector<uint8_t> before(ENGINE_HOST_FRAME_PACKET_BYTES, 0xCD);
    auto fetchResult = RuntimeResult{};
    const int32_t rc = EngineHost_Fetch(handle, ENGINE_HOST_OP_GET_PRESENTATION_FRAME,
                                        0, nullptr, nullptr, nullptr,
                                        before.data(),
                                        static_cast<int32_t>(before.size()), &fetchResult);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(fetchResult.status, ENGINE_HOST_STATUS_NO_NEW_FRAME);
    for (size_t i = 0; i < before.size(); ++i) {
        ASSERT_EQ(before[i], 0xCD) << "failed resize must not publish or overwrite a frame";
    }

    SetFailureFlag("EDITOR_BRIDGE_FAIL_VIEWPORT_RESIZE", false);
    float recovered[2] = {640.0f, 480.0f};
    auto recoverDispatch = RuntimeResult{};
    ASSERT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                 nullptr, nullptr, nullptr, recovered, &recoverDispatch), 0);
    auto recoverFrame = RuntimeResult{};
    ASSERT_EQ(EngineHost_PumpOneFrame(handle, 0.016f, &recoverFrame), 0);
    EXPECT_EQ(host->PresentationTargetWidth(), 640);
    EXPECT_EQ(host->PresentationTargetHeight(), 480);

    EngineHost_Destroy(handle);
}

TEST(EngineHostViewportResizeTest, ResizeIsNotAppliedWhenPumpFailsBeforeContextWork) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    auto result = RuntimeResult{};
    EngineHostHandle handle = EngineHost_Create(&result);
    ASSERT_NE(handle, nullptr);
    auto* host = AsHost(handle);

    SetFailureFlag("EDITOR_BRIDGE_FAIL_RUNTIME_COMMAND", true);
    float requested[2] = {640.0f, 480.0f};
    auto dispatchResult = RuntimeResult{};
    ASSERT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                 nullptr, nullptr, nullptr, requested, &dispatchResult), 0);

    auto frameResult = RuntimeResult{};
    EXPECT_EQ(EngineHost_PumpOneFrame(handle, 0.016f, &frameResult), -1);
    EXPECT_EQ(host->PresentationTargetWidth(), ENGINE_HOST_PRESENTATION_DEFAULT_WIDTH);
    EXPECT_EQ(host->PresentationTargetHeight(), ENGINE_HOST_PRESENTATION_DEFAULT_HEIGHT);

    SetFailureFlag("EDITOR_BRIDGE_FAIL_RUNTIME_COMMAND", false);

    EngineHost_Destroy(handle);
}

TEST(EngineHostViewportResizeTest, PendingPickIsInvalidatedByResize) {
    ASSERT_EQ(EditorBridge_RegisterMainThread(), 1);
    auto result = RuntimeResult{};
    EngineHostHandle handle = EngineHost_Create(&result);
    ASSERT_NE(handle, nullptr);

    float pick[2] = {12.0f, 34.0f};
    auto pickDispatch = RuntimeResult{};
    ASSERT_GT(EngineHost_Dispatch(handle, ENGINE_HOST_OP_PICK, 0, 0,
                                 nullptr, nullptr, nullptr, pick, &pickDispatch), 0);
    ASSERT_EQ(pickDispatch.status, ENGINE_HOST_STATUS_EDIT_ACCEPTED);

    float resize[2] = {640.0f, 480.0f};
    auto resizeDispatch = RuntimeResult{};
    ASSERT_EQ(EngineHost_Dispatch(handle, ENGINE_HOST_OP_SET_VIEWPORT_SIZE, 0, 0,
                                 nullptr, nullptr, nullptr, resize, &resizeDispatch), 0);

    auto frameResult = RuntimeResult{};
    ASSERT_EQ(EngineHost_PumpOneFrame(handle, 0.016f, &frameResult), 0);

    std::vector<uint8_t> packet(ENGINE_HOST_PICK_RESULT_PACKET_BYTES, 0xCD);
    auto fetchResult = RuntimeResult{};
    const int32_t rc = EngineHost_Fetch(handle, ENGINE_HOST_OP_PICK, 0, nullptr, nullptr, nullptr,
                                        packet.data(), static_cast<int32_t>(packet.size()),
                                        &fetchResult);
    ASSERT_GE(rc, 0);
    auto* pickPacket = reinterpret_cast<PickResultPacket*>(packet.data());
    EXPECT_EQ(pickPacket->status, ENGINE_HOST_PICK_NOT_READY)
        << "a pick whose coordinates belong to the pre-resize geometry must not be serviced";

    EngineHost_Destroy(handle);
}
