#pragma once

#include "Engine/Core/RHI/RHIWindow.h"
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

    std::string m_GPUName;
    std::string m_BackendName;
    uint64 m_FrameCount = 0;

    bool InitOpenGL();
    bool InitVulkan();
    bool InitD3D12();
};

} // namespace Engine