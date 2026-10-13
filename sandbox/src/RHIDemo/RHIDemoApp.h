#pragma once

#include "Engine/Core/RHI/RHIWindow.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include <memory>
#include <string>
#include <cstdint>

namespace Engine {

class RHIDemoApp {
public:
    RHIDemoApp(RHI::RHIBackend backend);
    ~RHIDemoApp();
    bool Initialize();
    void Run();

private:
    RHI::RHIBackend m_Backend;

    // Vulkan/D3D12 路径
    std::unique_ptr<RHI::RHIWindow> m_RHIWindow;
    // 设备必须由成员持有：此前 InitVulkan / InitD3D12 把设备建在局部变量里，
    // 函数一返回就析构，窗口随即失去设备，而日志仍打印「✅」。
    std::unique_ptr<RHI::IRHIDevice> m_Device;

    std::string m_GPUName;
    std::string m_BackendName;
    uint64 m_FrameCount = 0;

    bool InitOpenGL();
    bool InitVulkan();
    bool InitD3D12();
};

} // namespace Engine