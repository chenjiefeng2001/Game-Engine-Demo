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
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef ENGINE_HAS_VULKAN
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/MemoryTypes.h"
#include "Engine/Core/RHI/PSODesc.h"
#include <memory>
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/StringID.h"
#include "Engine/Vulkan/ShaderRegistry.h"
#include "Engine/Vulkan/VulkanOffscreen.h"

namespace {

/** 顶点输入契约阶段的着色器：从顶点缓冲取位置与颜色，不依赖 gl_VertexIndex。 */
const char* kVertexInputGlsl = R"(#version 450
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 vColor;
void main() {
    vColor = inColor;
    gl_Position = vec4(inPosition, 0.0, 1.0);
}
)";

const char* kVertexInputFragGlsl = R"(#version 450
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(vColor, 1.0); }
)";

/** 32 字节对齐顶点：location 0 取 RG32F @0，location 1 取 RG32F @8（读 8..20）。 */
struct TriVertex { float x, y, z, w; float r, g, b, a; };

/**
 * 第二阶段：验证 GraphicsPSODesc 的顶点输入契约真的能驱动 Vulkan 的
 * vertex input state，并配合引擎顶点缓冲画出几何。
 *
 * 与第一阶段分离报告：gl_VertexIndex 通过**不代表**顶点输入可用，两者排查的
 * 失败面完全不同（binding / attribute / stride / 格式 / 上传）。
 */
/**
   * 第三阶段：描述符（uniform buffer）。
   *
   * 此前整条描述符链都不存在 —— GraphicsPSODesc 没有绑定声明、pipeline layout 的
   * setLayoutCount 恒为 0、描述符池从未创建、SetConstantBuffer 只重绑一个恒为空的
   * set 且从不写入描述符。着色器因此无法读取任何 uniform。
   *
   * 这里让着色器真的读一个 UBO，并改写 UBO 确认像素随之改变 —— 只有「值真的
   * 到达了着色器」才能通过，单看绘制成功是不够的。
   */
  int RunUniformBufferPhase(Engine::RHI::VulkanDevice& device) {
      using namespace Engine;
      using namespace Engine::RHI;
      std::printf("[VKTRI-UBO] --- uniform buffer phase ---\n");

      const char* kVert = R"(
      #version 450
      void main() {
          vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
          gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
      }
      )";
      const char* kFrag = R"(
      #version 450
      layout(set = 0, binding = 0) uniform Params { vec4 tint; } params;
      layout(location = 0) out vec4 outColor;
      void main() { outColor = params.tint; }
      )";
      if (!ShaderRegistry::Get().RegisterSource("vktribo_vs", ShaderStageType::Vertex, kVert) ||
          !ShaderRegistry::Get().RegisterSource("vktribo_fs", ShaderStageType::Fragment, kFrag)) {
          std::printf("[VKTRI-UBO] FAIL: shader compilation\n"); return 1;
      }

      GraphicsPSODesc desc;
      desc.vertexShader = StringID::Runtime("vktribo_vs");
      desc.pixelShader  = StringID::Runtime("vktribo_fs");
      desc.rtvCount = 1;
      desc.rtvFormats[0] = Format::RGBA8_UNorm;
      desc.depthStencil.depthTest = false;
      desc.depthStencil.depthWrite = false;
      // 声明绑定：此前 GraphicsPSODesc 根本没有这个字段。
      desc.bindings.push_back({ 0u, 0u, DescriptorKind::UniformBuffer, 1u });

      std::shared_ptr<IRHIPipelineState> psoOwner(device.CreateGraphicsPSO(desc));
      auto* pso = psoOwner.get();
      if (!pso) { std::printf("[VKTRI-UBO] FAIL: CreateGraphicsPSO with a binding\n"); return 1; }
      auto* vkPso = dynamic_cast<VulkanPipelineState*>(pso);
      if (!vkPso || vkPso->GetDescriptorSetLayout() == VK_NULL_HANDLE) {
          std::printf("[VKTRI-UBO] FAIL: pipeline has no descriptor set layout\n"); return 1;
      }
      std::printf("[VKTRI-UBO] pipeline created with a descriptor set layout\n");

      struct Params { float r, g, b, a; };
      RHIBufferDesc bd{};
      bd.size = sizeof(Params);
      bd.memoryUsage = MemoryUsage::CPU_To_GPU;
      const Params red{1.0f, 0.0f, 0.0f, 1.0f};
      bd.initialData = &red;
      auto ubo = device.CreateBuffer(bd);
      if (!ubo) { std::printf("[VKTRI-UBO] FAIL: CreateBuffer\n"); return 1; }
      if (!ubo->GetMappedPtr()) {
          std::printf("[VKTRI-UBO] FAIL: GetMappedPtr() is null\n"); return 1;
      }

      constexpr uint32_t kW = 64, kH = 64;
      auto target = VulkanOffscreenTarget::Create(device, kW, kH);
      if (!target) { std::printf("[VKTRI-UBO] FAIL: offscreen target\n"); return 1; }

      auto renderWith = [&](const Params& v, uint8_t* out) -> bool {
          std::memcpy(ubo->GetMappedPtr(), &v, sizeof(v));
          VkCommandBuffer cmd = target->BeginOneShot();
          if (cmd == VK_NULL_HANDLE) return false;
          target->BeginRendering(cmd, (const float[4]){0.0f, 0.0f, 0.0f, 1.0f});
          {
              auto cl = device.CreateCommandList(CommandListType::Direct);
              auto* vcl = dynamic_cast<VulkanCommandList*>(cl.get());
              vcl->SetDevice(&device);
              // BeginOneShot() 已开始录制，不能再 Begin()；结束由 EndSubmitAndWait() 负责。
              vcl->AttachToRecording(cmd);
              vcl->SetPipelineState(pso);
              vcl->SetConstantBuffer(0, 0, ubo.get(), 0, sizeof(Params));
              vcl->Draw(3, 0);
          }
          target->EndRendering(cmd);
          if (!target->EndSubmitAndWait(cmd)) return false;
          std::vector<uint8_t> px;
          if (!target->Readback(px)) return false;
          const uint8_t* mid = px.data() + (((kH / 2) * kW) + kW / 2) * 4;
          out[0] = mid[0]; out[1] = mid[1]; out[2] = mid[2]; out[3] = mid[3];
          return true;
      };

      uint8_t pxRed[4]{}, pxGreen[4]{};
      if (!renderWith(red, pxRed)) { std::printf("[VKTRI-UBO] FAIL: red draw\n"); return 1; }
      std::printf("[VKTRI-UBO] red   -> %u %u %u %u\n", pxRed[0], pxRed[1], pxRed[2], pxRed[3]);
      if (!renderWith(Params{0.0f, 1.0f, 0.0f, 1.0f}, pxGreen)) {
          std::printf("[VKTRI-UBO] FAIL: green draw\n"); return 1;
      }
      std::printf("[VKTRI-UBO] green -> %u %u %u %u\n", pxGreen[0], pxGreen[1], pxGreen[2], pxGreen[3]);

      const bool redOK = pxRed[0] > 200 && pxRed[1] < 60 && pxRed[2] < 60;
      const bool greenOK = pxGreen[0] < 60 && pxGreen[1] > 200 && pxGreen[2] < 60;
      if (!redOK || !greenOK) {
          std::printf("[VKTRI-UBO] FAIL: UBO contents never reached the shader\n"); return 1;
      }
      std::printf("[VKTRI-UBO] PASS: the shader read the UBO through a real descriptor set; "
                  "changing the UBO changed the pixels\n");
      return 0;
  }

  /**
   * 第四阶段：纹理描述符与采样器。
   *
   * SetShaderResource 此前与 SetConstantBuffer 一样是死代码（只重绑恒为空的 set、
   * 从不写描述符），引擎里也没有任何采样器，TextureDesc::initialData 被
   * CreateTexture 完全忽略。
   *
   * 关键在于 UBO 与纹理**放在同一个 set 内**：描述符集若每次调用都重新分配并绑定，
   * 第二次调用就会覆盖第一次的绑定，UBO 会被丢掉。
   */
  int RunTextureSamplerPhase(Engine::RHI::VulkanDevice& device) {
      using namespace Engine;
      using namespace Engine::RHI;
      std::printf("[VKTRI-TEX] --- texture descriptor phase ---\n");

      const char* kVert = R"(
      #version 450
      void main() {
          vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
          gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
      }
      )";
      const char* kFrag = R"(
      #version 450
      layout(set = 0, binding = 0) uniform Params { vec4 tint; } params;
      layout(set = 0, binding = 1) uniform sampler2D tex;
      layout(location = 0) out vec4 outColor;
      void main() { outColor = texture(tex, gl_FragCoord.xy / 64.0) * params.tint; }
      )";
      if (!ShaderRegistry::Get().RegisterSource("vktrix_vs", ShaderStageType::Vertex, kVert) ||
          !ShaderRegistry::Get().RegisterSource("vktrix_fs", ShaderStageType::Fragment, kFrag)) {
          std::printf("[VKTRI-TEX] FAIL: shader compilation\n"); return 1;
      }

      GraphicsPSODesc desc;
      desc.vertexShader = StringID::Runtime("vktrix_vs");
      desc.pixelShader  = StringID::Runtime("vktrix_fs");
      desc.rtvCount = 1;
      desc.rtvFormats[0] = Format::RGBA8_UNorm;
      desc.depthStencil.depthTest = false;
      desc.depthStencil.depthWrite = false;
      desc.bindings.push_back({ 0u, 0u, DescriptorKind::UniformBuffer, 1u });
      desc.bindings.push_back({ 0u, 1u, DescriptorKind::CombinedImageSampler, 1u });

      std::shared_ptr<IRHIPipelineState> psoOwner(device.CreateGraphicsPSO(desc));
      if (!psoOwner) { std::printf("[VKTRI-TEX] FAIL: CreateGraphicsPSO\n"); return 1; }

      constexpr uint32_t kN = 64;
      std::vector<uint8_t> pixels(kN * kN * 4);
      for (uint32_t i = 0; i < kN * kN; ++i) {
          pixels[i * 4 + 0] = 32; pixels[i * 4 + 1] = 96;
          pixels[i * 4 + 2] = 160; pixels[i * 4 + 3] = 255;
      }
      TextureDesc td{};
      td.width = kN; td.height = kN;
      td.format = Format::RGBA8_UNorm;
      td.memoryUsage = MemoryUsage::GPU_Only;
      td.initialData = pixels.data();
      auto texture = device.CreateTexture(td);
      if (!texture) { std::printf("[VKTRI-TEX] FAIL: CreateTexture\n"); return 1; }

      struct Params { float r, g, b, a; };
      RHIBufferDesc bd{};
      bd.size = sizeof(Params);
      bd.memoryUsage = MemoryUsage::CPU_To_GPU;
      const Params white{1.0f, 1.0f, 1.0f, 1.0f};
      bd.initialData = &white;
      auto ubo = device.CreateBuffer(bd);
      if (!ubo || !ubo->GetMappedPtr()) { std::printf("[VKTRI-TEX] FAIL: CreateBuffer\n"); return 1; }

      auto target = VulkanOffscreenTarget::Create(device, kN, kN);
      if (!target) { std::printf("[VKTRI-TEX] FAIL: offscreen target\n"); return 1; }

      auto renderWith = [&](const Params& tint, uint8_t* out) -> bool {
          std::memcpy(ubo->GetMappedPtr(), &tint, sizeof(tint));
          VkCommandBuffer cmd = target->BeginOneShot();
          if (cmd == VK_NULL_HANDLE) return false;
          target->BeginRendering(cmd, (const float[4]){0.0f, 0.0f, 0.0f, 1.0f});
          {
              auto cl = device.CreateCommandList(CommandListType::Direct);
              auto* vcl = dynamic_cast<VulkanCommandList*>(cl.get());
              vcl->SetDevice(&device);
              vcl->AttachToRecording(cmd);
              vcl->SetPipelineState(psoOwner.get());
              vcl->SetConstantBuffer(0, 0, ubo.get(), 0, sizeof(Params));
              vcl->SetShaderResource(0, 1, texture.get());
              vcl->Draw(3, 0);
          }
          target->EndRendering(cmd);
          if (!target->EndSubmitAndWait(cmd)) return false;
          std::vector<uint8_t> px;
          if (!target->Readback(px)) return false;
          const uint8_t* mid = px.data() + ((kN / 2 * kN) + kN / 2) * 4;
          for (int i = 0; i < 4; ++i) out[i] = mid[i];
          return true;
      };

      uint8_t px[4]{};
      if (!renderWith(white, px)) { std::printf("[VKTRI-TEX] FAIL: first draw\n"); return 1; }
      std::printf("[VKTRI-TEX] white UBO    -> %u %u %u %u (expect ~32 96 160 255)\n",
                  px[0], px[1], px[2], px[3]);
      if (!renderWith(Params{0.5f, 1.0f, 1.0f, 1.0f}, px)) {
          std::printf("[VKTRI-TEX] FAIL: second draw\n"); return 1;
      }
      std::printf("[VKTRI-TEX] half-red UBO -> %u %u %u %u (expect ~16 96 160 255)\n",
                  px[0], px[1], px[2], px[3]);

      const bool texOK = px[1] > 70 && px[1] < 125 && px[2] > 130 && px[2] < 190;
      const bool uboOK = px[0] >= 4 && px[0] <= 32;
      if (!texOK || !uboOK) {
          std::printf("[VKTRI-TEX] FAIL: texture or UBO did not reach the shader\n"); return 1;
      }
      std::printf("[VKTRI-TEX] PASS: a real texture reached the shader through a descriptor "
                  "and sampler, and coexisted with a UBO in the same set\n");
      return 0;
  }

  int RunVertexInputPhase(Engine::RHI::VulkanDevice& device) {
      using namespace Engine;
    std::printf("[VKTRI-VB] --- vertex input phase ---\n");

    if (!RHI::ShaderRegistry::Get().RegisterSource("vktrib_vs", ShaderStageType::Vertex, kVertexInputGlsl) ||
        !RHI::ShaderRegistry::Get().RegisterSource("vktrib_fs", ShaderStageType::Fragment, kVertexInputFragGlsl)) {
        std::printf("[VKTRI-VB] FAIL: shader registration failed\n");
        return 1;
    }

    RHI::GraphicsPSODesc desc;
    desc.vertexShader = StringID::Runtime("vktrib_vs");
    desc.pixelShader  = StringID::Runtime("vktrib_fs");
    desc.rtvCount = 1;
    desc.rtvFormats[0] = RHI::Format::RGBA8_UNorm;
    desc.depthStencil.depthTest  = false;
    desc.depthStencil.depthWrite = false;
    desc.vertexInput.buffers.push_back({ 0u, sizeof(TriVertex), RHI::VertexInputRate::Vertex });
    desc.vertexInput.attributes.push_back({ 0u, 0u, RHI::Format::RG32_Float, 0u });
    desc.vertexInput.attributes.push_back({ 1u, 0u, RHI::Format::RG32_Float, 8u });

    // 必须持有所有权：CreateGraphicsPSO 返回裸指针，不接管的话 PSO 永远不析构，
  // 其 VkPipeline / VkPipelineLayout 会一直活到 vkDestroyDevice，触发
  // VUID-vkDestroyDevice-device-05137。
  std::shared_ptr<RHI::IRHIPipelineState> psoOwner(device.CreateGraphicsPSO(desc));
  auto* pso = psoOwner.get();
    if (!pso) {
        std::printf("[VKTRI-VB] FAIL: CreateGraphicsPSO with vertex input returned null\n");
        return 1;
    }
    auto* vkPso = dynamic_cast<RHI::VulkanPipelineState*>(pso);
    if (!vkPso) { std::printf("[VKTRI-VB] FAIL: PSO is not VulkanPipelineState\n"); return 1; }
    std::printf("[VKTRI-VB] pipeline created with %zu binding(s), %zu attribute(s)\n",
                desc.vertexInput.buffers.size(), desc.vertexInput.attributes.size());

    const TriVertex tri[3] = {
        { -0.8f, -0.8f, 0.0f, 1.0f,  1.0f, 0.0f, 0.0f, 1.0f },
        {  0.8f, -0.8f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f },
        {  0.0f,  0.8f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f },
    };
    RHI::RHIBufferDesc bd;
    bd.size = sizeof(tri);
    bd.stride = sizeof(TriVertex);
    bd.memoryUsage = RHI::MemoryUsage::CPU_To_GPU;
    bd.initialData = tri;
    auto vbuf = device.CreateBuffer(bd);
    if (!vbuf) { std::printf("[VKTRI-VB] FAIL: CreateBuffer\n"); return 1; }
    auto* vkBuf = static_cast<RHI::VulkanBuffer*>(vbuf.get());

    constexpr uint32_t kW = 64, kH = 64;
    auto target = RHI::VulkanOffscreenTarget::Create(device, kW, kH);
    if (!target) { std::printf("[VKTRI-VB] FAIL: offscreen target\n"); return 1; }

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    VkCommandBuffer cmd = target->BeginOneShot();
    if (cmd == VK_NULL_HANDLE) { std::printf("[VKTRI-VB] FAIL: BeginOneShot\n"); return 1; }
    target->BeginRendering(cmd, clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPso->GetVkPipeline());
VkBuffer handle = vkBuf->GetVkBuffer();
      // 偏移量必须显式传 0：pOffsets 传 nullptr 会被 validation 判为
      // VUID-vkCmdBindVertexBuffers-pOffsets-parameter。
      VkDeviceSize vertexOffset = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &handle, &vertexOffset);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    target->EndRendering(cmd);
    if (!target->EndSubmitAndWait(cmd)) { std::printf("[VKTRI-VB] FAIL: EndSubmitAndWait\n"); return 1; }

    std::vector<uint8_t> px;
    if (!target->Readback(px)) { std::printf("[VKTRI-VB] FAIL: Readback\n"); return 1; }
    const uint8_t* mid = px.data() + ((static_cast<size_t>(kH / 2) * kW) + kW / 2) * 4;
    const uint8_t* corner = px.data();
    std::printf("[VKTRI-VB] centre = %u %u %u %u (expect non-black)\n", mid[0], mid[1], mid[2], mid[3]);
    std::printf("[VKTRI-VB] corner = %u %u %u %u (expect 0 0 0 255, clear only)\n",
                corner[0], corner[1], corner[2], corner[3]);

    const bool triDrawn = !(mid[0] == 0 && mid[1] == 0 && mid[2] == 0);
    const bool clearOk = (corner[0] == 0 && corner[1] == 0 && corner[2] == 0 && corner[3] == 255);
    if (triDrawn && clearOk) {
        std::printf("[VKTRI-VB] PASS: geometry rasterised from an engine vertex buffer; "
                    "the vertex input contract works\n");
        return 0;
    }
    if (!triDrawn) std::printf("[VKTRI-VB] FAIL: triangle did not rasterise (centre is still clear)\n");
    if (!clearOk)  std::printf("[VKTRI-VB] FAIL: clear value not applied\n");
    return 1;
}

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

    std::shared_ptr<RHI::IRHIPipelineState> psoOwner(vkDevice->CreateGraphicsPSO(desc));
  auto* pso = psoOwner.get();
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
    if (!drew) return 1;

    // ── 第二阶段：顶点输入契约 ──────────────────────────────────────
// 第一阶段用 gl_VertexIndex 生成三角形，绕开了顶点缓冲；它通过只证明
      // 光栅化链路可用，**不能**证明顶点输入契约工作。因此本阶段必须运行。
      // 顶点输入阶段曾经按 VKTRI_VERTEX_INPUT 门控，理由是「lavapipe 绑定顶点缓冲即段错误」。
      // 那个结论是错的：真正的两个原因是我们的 bug —— FormatToVk() 没有
      // Format::RG32_Float 分支（顶点格式变成 VK_FORMAT_UNDEFINED），以及本测试
      // 给 vkCmdBindVertexBuffers 传了 nullptr 偏移。两者修复后阶段稳定通过，
      // 因此取消门控，让它成为常规验证的一部分。
      if (RunVertexInputPhase(*vkDevice) != 0) return 1;
      // 描述符阶段：证明 uniform buffer 真的能到达着色器。
      if (RunUniformBufferPhase(*vkDevice) != 0) return 1;
      // 纹理阶段：证明纹理描述符与采样器可用，且与 UBO 共存于同一 set。
      return RunTextureSamplerPhase(*vkDevice);
#endif // ENGINE_HAS_VULKAN
}