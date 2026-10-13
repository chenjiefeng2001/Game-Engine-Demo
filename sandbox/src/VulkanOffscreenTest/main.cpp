/**
 * @file main.cpp
 * @brief Headless Vulkan 离屏三角形 —— 验证引擎后端能在无 GPU 环境真实出像素
 *
 * 为什么需要这个：Linux CI 的 llvmpipe 最高只有 OpenGL 4.5，而引擎把 4.6 作为
 * 产品承诺，因此 GPU 像素用例在 CI 上长期显式 SKIP。Vulkan + lavapipe 是唯一
 * 已验证可行（且不妥协产品承诺）的路线。
 *
 * 它同时是引擎 Vulkan 后端的第一份**运行时**证据。此前该后端从未被任何 CI
 * 编译过，更没运行过。
 *
 * 验证方式不是「退出码为 0」，而是**像素断言**：渲染目标 clear 为黑，片元
 * 着色器固定输出 vec4(0.25, 0.5, 0.75, 1.0) = (64,128,191,255)。中心像素若
 * 等于该值，说明管线创建、渲染作用域、光栅化与回读都真实发生；若为黑，说明
 * 只有 clear 生效，绘制没跑起来。
 *
 * 三角形不用顶点缓冲，靠 gl_VertexIndex 生成，因此不需要顶点输入契约。
 */
#include <cstdio>
#include <vector>

#ifdef ENGINE_HAS_VULKAN
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/StringID.h"
#include "Engine/Vulkan/ShaderRegistry.h"
#include "Engine/Vulkan/VulkanOffscreen.h"

namespace {

const char* kVertexGlsl = R"(#version 450
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kFragmentGlsl = R"(#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(0.25, 0.5, 0.75, 1.0); }
)";

} // namespace
#endif // ENGINE_HAS_VULKAN

int main()
{
#ifndef ENGINE_HAS_VULKAN
    std::printf("[VKTRI] ENGINE_HAS_VULKAN undefined; nothing to verify\n");
    return 0;
#else
    using namespace Engine;
    using namespace Engine::RHI;

    std::printf("[VKTRI] === headless Vulkan offscreen triangle ===\n");

    // ── 1. 着色器登记 ──
    if (!ShaderRegistry::Get().RegisterSource("vktri_vs", ShaderStageType::Vertex, kVertexGlsl)) {
        std::printf("[VKTRI] FAIL: vertex shader registration failed\n");
        return 1;
    }
    if (!ShaderRegistry::Get().RegisterSource("vktri_fs", ShaderStageType::Fragment, kFragmentGlsl)) {
        std::printf("[VKTRI] FAIL: fragment shader registration failed\n");
        return 1;
    }
    std::printf("[VKTRI] shaders registered (registry holds %zu)\n", ShaderRegistry::Get().Count());

    // ── 2. 设备 ──
    auto device = CreateVulkanDevice();
    if (!device) {
        std::printf("[VKTRI] FAIL: CreateVulkanDevice returned null\n");
        return 1;
    }
    auto* vkDevice = dynamic_cast<VulkanDevice*>(device.get());
    if (!vkDevice) {
        std::printf("[VKTRI] FAIL: device is not a VulkanDevice\n");
        return 1;
    }
    std::printf("[VKTRI] device created: %s\n", vkDevice->GetDeviceName() ? vkDevice->GetDeviceName() : "<null>");

    // ── 3. 管线（这条路径此前恒返回 nullptr）──
    GraphicsPSODesc desc;
    desc.vertexShader = StringID::Runtime("vktri_vs");
    desc.pixelShader  = StringID::Runtime("vktri_fs");
    desc.rtvCount = 1;
    desc.rtvFormats[0] = Format::RGBA8_UNorm;
    desc.depthStencil.depthTest = false;
    desc.depthStencil.depthWrite = false;

    auto* pso = vkDevice->CreateGraphicsPSO(desc);
    if (!pso) {
        std::printf("[VKTRI] FAIL: CreateGraphicsPSO returned null\n");
        return 1;
    }
    auto* vkPso = dynamic_cast<VulkanPipelineState*>(pso);
    if (!vkPso) {
        std::printf("[VKTRI] FAIL: PSO is not a VulkanPipelineState\n");
        return 1;
    }
    VkPipeline pipeline = vkPso->GetVkPipeline();
    std::printf("[VKTRI] graphics pipeline created\n");

    // ── 4. 离屏目标 ──
    constexpr uint32_t kW = 64, kH = 64;
    auto target = VulkanOffscreenTarget::Create(*vkDevice, kW, kH);
    if (!target) {
        std::printf("[VKTRI] FAIL: offscreen target creation failed\n");
        return 1;
    }

    // ── 5. 录制：清黑 → 绘制三角形 ──
    VkCommandBuffer cmd = target->BeginOneShot();
    if (cmd == VK_NULL_HANDLE) {
        std::printf("[VKTRI] FAIL: BeginOneShot returned null\n");
        return 1;
    }
    // 清除色刻意用 alpha = 0.25：片元输出与清除色的 alpha 都取 1.0 时，
    // 「clear 生效但 draw 没跑」和「clear 也没生效」读到的 alpha 都是 255，
    // 无法区分。给出可区分的哨兵值后，单看 alpha 即可定位是哪一层没工作：
    //   alpha   0  → clear 与 draw 都没生效（提交/回读/布局层面）
    //   alpha  64  → clear 生效、draw 没生效（视口、剔除、顶点等光栅层）
    //   64 128 191 255 → 端到端正常
    const float clearWithSentinel[4] = { 0.0f, 0.0f, 0.0f, 0.25f };
    target->BeginRendering(cmd, clearWithSentinel);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    target->EndRendering(cmd);

    if (!target->EndSubmitAndWait(cmd)) {
        std::printf("[VKTRI] FAIL: EndSubmitAndWait failed\n");
        return 1;
    }

    // ── 6. 回读并断言像素 ──
    std::vector<uint8_t> pixels;
    if (!target->Readback(pixels)) {
        std::printf("[VKTRI] FAIL: Readback failed\n");
        return 1;
    }

    const uint32_t pitch = target->RowPitch();
    const size_t off = (static_cast<size_t>(kH / 2) * kW + kW / 2) * 4;
    if (off + 4 > pixels.size()) {
        std::printf("[VKTRI] FAIL: readback buffer too small (%zu bytes)\n", pixels.size());
        return 1;
    }
    const uint8_t* c = pixels.data() + off;
    std::printf("[VKTRI] centre pixel = %u %u %u %u (pitch=%u)\n", c[0], c[1], c[2], c[3], pitch);

    // 全缓冲区统计：把「一个像素都没写进去」与「写进去了但值不对」区分开，
    // 避免只看中心点时把缓冲未初始化误读成光栅问题。
    size_t nonZeroBytes = 0;
    size_t distinctAlpha = 0;
    uint8_t seenAlpha[256] = { 0 };
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const uint8_t* px = pixels.data() + i;
        if (px[0] | px[1] | px[2] | px[3]) ++nonZeroBytes;
        if (!seenAlpha[px[3]]) { seenAlpha[px[3]] = 1; ++distinctAlpha; }
    }
    std::printf("[VKTRI] pixels with any non-zero channel = %zu / %zu, distinct alpha values = %zu\n",
                nonZeroBytes, pixels.size() / 4, distinctAlpha);

    const bool drew = (c[0] == 64 && c[1] == 128 && c[2] == 191 && c[3] == 255);
    if (drew) {
        std::printf("[VKTRI] PASS: fragment shader output reached the readback; "
                    "the engine Vulkan backend rasterises offscreen on this device\n");
    } else if (nonZeroBytes == 0) {
        std::printf("[VKTRI] FAIL: nothing reached the readback buffer at all. The clear alone "
                    "would have set alpha 64, so this is a submit/copy/layout problem upstream of "
                    "rasterisation, not a draw-state problem.\n");
    } else if (c[3] == 64) {
        std::printf("[VKTRI] FAIL: clear landed (alpha 64) but the shader colour did not. "
                    "Raster-stage problem: viewport, scissor, culling or vertex generation.\n");
    } else {
        std::printf("[VKTRI] FAIL: centre pixel is neither the clear sentinel nor the shader "
                    "colour; some pixels were written.\n");
    }
    return drew ? 0 : 1;
#endif // ENGINE_HAS_VULKAN
}