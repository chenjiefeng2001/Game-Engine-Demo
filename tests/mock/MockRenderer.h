/**
 * @file MockRenderer.h
 * @brief Google Mock — 渲染器接口 Mock
 *
 * 用于 Gameplay 测试时注入替代真实渲染器
 */
#include <gmock/gmock.h>
#include "Engine/Renderer/IRenderer.h" // NOLINT — 接口可能存在

// 如果 IRenderer 尚未定义，此文件提供独立的 Mock 定义
// 这样即使渲染系统未完成也可以写 Gameplay 测试

namespace Engine { namespace Mock {

class MockRenderer {
public:
    MOCK_METHOD(bool, Initialize, (const void* config));
    MOCK_METHOD(void, Shutdown, ());
    MOCK_METHOD(void, BeginFrame, ());
    MOCK_METHOD(void, EndFrame, ());
    MOCK_METHOD(void, DrawMesh, (void* mesh, const float transform[16]));
    MOCK_METHOD(uint32_t, GetDrawCallCount, (), (const));
};

}} // namespace Engine::Mock