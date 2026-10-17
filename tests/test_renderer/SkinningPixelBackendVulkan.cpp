/**
 * @file SkinningPixelBackendVulkan.cpp
 * @brief GPU 蒙皮像素验证的 Vulkan 后端（headless，CI 上跑 lavapipe）
 *
 * 为什么需要它
 * ------------
 * 引擎要求 GL 4.6，而 hosted runner 给不出：Linux 的 llvmpipe 最高 4.5 core，
 * Windows 上连不带版本提示的默认上下文都建不出来。结果是四个蒙皮像素用例在
 * **每一个** CI runner 上都只能跳过 —— 像素验证等于长期缺席。
 * Vulkan 那条路已经在 lavapipe 上拿到过真实像素证据（见
 * docs/Handoff-Vulkan-Headless-GPU-Verification.md），因此像素验证落到它身上。
 *
 * 用的是 IRHI 那条路（VulkanCommandList 的渲染作用域 + 描述符 + 顶点输入 +
 * ReadbackTexture），不是裸 Vulkan 命令：离屏设施 VulkanOffscreenTarget 只用来
 * 分配一次性命令缓冲并提交等待（它已经过 CI 验证），绘制本身走接口。
 *
 * 与 GL 后端的差异，都是必须显式处理的地方
 * --------------------------------------
 *   * **着色器**：GL 那份是 GLSL 330 + 松散 uniform；Vulkan 需要 SPIR-V，
 *     uniform 必须放进 UBO。因此这里自带一份 GLSL 450 着色器（源码内联，
 *     经 shaderc 编译后注册进 ShaderRegistry），不共用产品那份 .vert/.frag。
 *   * **属性**：共享同一块顶点内存，但只声明用得到的 3 个属性
 *     （position / 骨骼索引 / 骨骼权重），location 在着色器里重新编号。
 *     骨骼索引是 ivec4，需要 Format::RGBA32_SInt —— 该映射此前缺失，
 *     会落到 VK_FORMAT_UNDEFINED 并建出非法管线。
 *   * **朝向**：Vulkan 回读是自上而下，GL 是自下而上；这里翻行使两个后端
 *     交给共享断言的是同一种朝向。
 *   * **剔除**：VulkanPipelineState 目前**硬编码** cullMode=BACK 且
 *     frontFace=CLOCKWISE（desc.rasterizer.cullMode 未被读取），所以四边形
 *     的绕序是有约束的。这里沿用与离屏三角形同向的绕序（已实测可光栅化），
 *     并且断言本身会在绕序不对时直接变红，不会静默通过。
 */
#include "SkinningPixelRig.h"

#if defined(ENGINE_HAS_VULKAN) && defined(ENGINE_HAS_SHADERC)

#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/RHIBackend.h"
#include "Engine/Core/RHI/RHIDeviceFactory.h"
#include "Engine/Core/RHI/ShaderReflection.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Vulkan/ShaderRegistry.h"
#include "Engine/Vulkan/VulkanOffscreen.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Engine {
namespace Test {
namespace {

constexpr int    kWidth        = 64;
constexpr int    kHeight       = 64;
constexpr uint32 kUboSize      = 192;   // mat4 viewProjection + mat4 bones[2]，std140
constexpr int    kBoneCount    = 2;

// GLSL 450：uniform 必须放进 UBO。骨骼索引是 ivec4，与共享顶点布局一致。
const char* const kSkinVertexGlsl = R"(
#version 450
layout(location = 0) in vec3  a_Position;
layout(location = 1) in ivec4 a_BoneIndices;
layout(location = 2) in vec4  a_BoneWeights;
layout(set = 0, binding = 0, std140) uniform SkinParams {
    mat4 u_ViewProjection;
    mat4 u_BoneMatrices[2];
} params;
void main() {
    mat4 skin = params.u_BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x
              + params.u_BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y
              + params.u_BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z
              + params.u_BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;
    gl_Position = params.u_ViewProjection * (skin * vec4(a_Position, 1.0));
}
)";

// 固定输出一个明显亮于清除色的颜色：清除色是 (0,0,0,1)，共享断言的口径是
// 「任一通道 > 8 即为亮」，因此几何与背景在字节层面必然可区分。
const char* const kSkinFragmentGlsl = R"(
#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(0.25, 0.55, 0.85, 1.0); }
)";

class VulkanSkinningPixelBackend final : public ISkinningPixelBackend {
public:
    ~VulkanSkinningPixelBackend() override
    {
        // 销毁顺序与建立顺序相反：命令缓冲先于池、设备先于 surface。
        target_.reset();
        pso_.reset();
        ubo_.reset();
        ibuf_.reset();
        vbuf_.reset();
        depth_.reset();
        colour_.reset();
        device_.reset();
    }

    static std::unique_ptr<VulkanSkinningPixelBackend> Create(std::string* why);

    const char* Name() const override { return name_.c_str(); }
    SkinCapture DrawRig(float boneX) override;
    bool VerifySkinnedProgram(std::string* detail) override;

private:
    bool WriteUbo(float boneX);

    std::unique_ptr<RHI::IRHIDevice>    device_;
    RHI::VulkanDevice*                  vk_ = nullptr;
    std::unique_ptr<RHI::VulkanOffscreenTarget> target_;
    std::shared_ptr<RHI::IRHITexture>   colour_;
    std::shared_ptr<RHI::IRHITexture>   depth_;
    std::shared_ptr<RHI::IRHIBuffer>    vbuf_;
    std::shared_ptr<RHI::IRHIBuffer>    ibuf_;
    std::shared_ptr<RHI::IRHIBuffer>    ubo_;
    std::shared_ptr<RHI::IRHIPipelineState> pso_;
    std::string name_ = "Vulkan";
};

std::unique_ptr<VulkanSkinningPixelBackend> VulkanSkinningPixelBackend::Create(std::string* why)
{
    using namespace Engine::RHI;

    auto backend = std::unique_ptr<VulkanSkinningPixelBackend>(new VulkanSkinningPixelBackend());

    backend->device_ = CreateRHIDevice(Backend::Vulkan, nullptr, kWidth, kHeight);
    if (!backend->device_) {
        if (why) *why = "CreateRHIDevice(Vulkan) returned null (no Vulkan loader or no usable "
                        "physical device; on CI this means lavapipe is not installed)";
        return nullptr;
    }
    backend->vk_ = dynamic_cast<VulkanDevice*>(backend->device_.get());
    if (!backend->vk_) {
        if (why) *why = "the created device is not a VulkanDevice";
        return nullptr;
    }
    backend->name_ = std::string("Vulkan (") + backend->vk_->GetDeviceName() + ")";

    if (!ShaderRegistry::Get().RegisterSource("skinpix_vs", ShaderStageType::Vertex, kSkinVertexGlsl) ||
        !ShaderRegistry::Get().RegisterSource("skinpix_fs", ShaderStageType::Fragment, kSkinFragmentGlsl)) {
        if (why) *why = "skinned GLSL failed to compile to SPIR-V (shaderc); the register "
                        "call returns false and prints the compiler log";
        return nullptr;
    }

    TextureDesc colourDesc{};
    colourDesc.width  = kWidth;
    colourDesc.height = kHeight;
    colourDesc.format = Format::RGBA8_UNorm;
    backend->colour_ = backend->device_->CreateTexture(colourDesc);

    TextureDesc depthDesc{};
    depthDesc.width  = kWidth;
    depthDesc.height = kHeight;
    depthDesc.format = Format::D32_Float;
    backend->depth_ = backend->device_->CreateTexture(depthDesc);
    if (!backend->colour_ || !backend->depth_) {
        if (why) *why = "CreateTexture failed for the colour or depth attachment";
        return nullptr;
    }

    const std::vector<SkinVertex> verts = SkinBuildQuadVertices();
    uint32_t indexCount = 0;
    const uint32_t* idx = SkinQuadIndices(&indexCount);

    RHIBufferDesc vbDesc{};
    vbDesc.size        = verts.size() * sizeof(SkinVertex);
    vbDesc.memoryUsage = MemoryUsage::CPU_To_GPU;
    vbDesc.initialData = verts.data();
    backend->vbuf_ = backend->device_->CreateBuffer(vbDesc);

    RHIBufferDesc ibDesc{};
    ibDesc.size        = indexCount * sizeof(uint32_t);
    ibDesc.memoryUsage = MemoryUsage::CPU_To_GPU;
    ibDesc.initialData = idx;
    backend->ibuf_ = backend->device_->CreateBuffer(ibDesc);

    RHIBufferDesc uboDesc{};
    uboDesc.size        = kUboSize;
    uboDesc.memoryUsage = MemoryUsage::CPU_To_GPU;
    backend->ubo_ = backend->device_->CreateBuffer(uboDesc);

    if (!backend->vbuf_ || !backend->ibuf_ || !backend->ubo_ || !backend->ubo_->GetMappedPtr()) {
        if (why) *why = "CreateBuffer failed, or the uniform buffer is not host-mapped";
        return nullptr;
    }

    // 顶点上传必须真的到位：只声明容量却没拷进去，会画出一个退化三角形，
    // 表现为 0 像素 —— 那看起来像引擎坏了，其实是数据没到。
    {
        const SkinVertex* seen = static_cast<const SkinVertex*>(backend->vbuf_->GetMappedPtr());
        const SkinVertex& want = verts.front();
        if (seen->position[0] != want.position[0] || seen->boneWeights[0] != want.boneWeights[0]) {
            if (why) *why = "vertex data did not reach the mapped buffer";
            return nullptr;
        }
    }

    GraphicsPSODesc pso;
    pso.vertexShader = StringID::Runtime("skinpix_vs");
    pso.pixelShader  = StringID::Runtime("skinpix_fs");
    pso.rtvCount = 1;
    pso.rtvFormats[0] = Format::RGBA8_UNorm;
    pso.dsvFormat = Format::D32_Float;
    pso.depthStencil.depthTest  = true;
    pso.depthStencil.depthWrite = true;
    pso.depthStencil.depthFunc  = CompareFunc::LessEqual;
    pso.vertexInput.buffers.push_back(
        { 0u, static_cast<uint32>(sizeof(SkinVertex)), VertexInputRate::Vertex });
    pso.vertexInput.attributes.push_back({ 0u, 0u, Format::RGB32_Float,
                                           static_cast<uint32>(offsetof(SkinVertex, position)) });
    pso.vertexInput.attributes.push_back({ 1u, 0u, Format::RGBA32_SInt,
                                           static_cast<uint32>(offsetof(SkinVertex, boneIndices)) });
    pso.vertexInput.attributes.push_back({ 2u, 0u, Format::RGBA32_Float,
                                           static_cast<uint32>(offsetof(SkinVertex, boneWeights)) });
    pso.bindings.push_back({ 0u, 0u, DescriptorKind::UniformBuffer, 1u });

    backend->pso_ = backend->device_->CreateGraphicsPSO(pso);
    if (!backend->pso_) {
        if (why) *why = "CreateGraphicsPSO returned null";
        return nullptr;
    }

    backend->target_ = VulkanOffscreenTarget::Create(*backend->vk_, kWidth, kHeight);
    if (!backend->target_) {
        if (why) *why = "VulkanOffscreenTarget::Create failed";
        return nullptr;
    }
    return backend;
}

bool VulkanSkinningPixelBackend::WriteUbo(float boneX)
{
    float* const dst = static_cast<float*>(ubo_->GetMappedPtr());
    if (!dst) return false;
    std::memset(dst, 0, kUboSize);
    // viewProjection：单位阵（与 GL rig 相同，四边形直接落在 NDC 里）
    SkinMakeTranslate(0.0f, 0.0f, 0.0f, dst);
    // 两根骨骼：0 号承载几何，1 号保持单位阵
    SkinMakeTranslate(boneX, 0.0f, 0.0f, dst + 16);
    SkinMakeTranslate(0.0f, 0.0f, 0.0f, dst + 32);
    return true;
}

SkinCapture VulkanSkinningPixelBackend::DrawRig(float boneX)
{
    using namespace Engine::RHI;

    SkinCapture cap;
    if (!WriteUbo(boneX)) {
        std::printf("  [%s] uniform buffer is not host-mapped\n", name_.c_str());
        return cap;
    }

    VkCommandBuffer cmd = target_->BeginOneShot();
    if (cmd == VK_NULL_HANDLE) {
        std::printf("  [%s] BeginOneShot failed\n", name_.c_str());
        return cap;
    }
    {
        auto cl = device_->CreateCommandList(CommandListType::Direct);
        auto* vcl = dynamic_cast<VulkanCommandList*>(cl.get());
        if (!vcl) {
            std::printf("  [%s] CreateCommandList did not produce a Vulkan command list\n",
                        name_.c_str());
            return cap;
        }
        vcl->SetDevice(vk_);
        vcl->AttachToRecording(cmd);

        RenderTargetDesc rt{};
        rt.color[0] = colour_.get();
        rt.colorCount = 1;
        rt.depth = depth_.get();
        rt.clear = true;
        rt.clearColor[0] = 0.0f; rt.clearColor[1] = 0.0f; rt.clearColor[2] = 0.0f;
        rt.clearColor[3] = 1.0f;
        rt.depthClearValue = 1.0f;      // Vulkan 约定：1.0 是最远
        vcl->BeginRendering(rt);
        vcl->SetPipelineState(pso_.get());
        vcl->SetVertexBuffer(0, vbuf_.get(), sizeof(SkinVertex), 0);
        vcl->SetIndexBuffer(ibuf_.get(), 0);
        vcl->SetConstantBuffer(0, 0, ubo_.get(), 0, kUboSize);
        vcl->DrawIndexed(6, 0, 0);
        vcl->EndRendering();
    }
    if (!target_->EndSubmitAndWait(cmd)) {
        std::printf("  [%s] submit/wait failed\n", name_.c_str());
        return cap;
    }

    std::vector<uint8_t> raw;
    if (!vk_->ReadbackTexture(colour_.get(), raw)) {
        std::printf("  [%s] ReadbackTexture failed\n", name_.c_str());
        return cap;
    }

    // Vulkan 自上而下、GL 自下而上；翻行使两个后端交给共享断言的是同一种朝向。
    const size_t rowBytes = static_cast<size_t>(kWidth) * 4;
    if (raw.size() < rowBytes * static_cast<size_t>(kHeight)) {
        std::printf("  [%s] readback returned %zu bytes, expected %zu\n",
                    name_.c_str(), raw.size(), rowBytes * static_cast<size_t>(kHeight));
        return cap;
    }
    cap.w = kWidth;
    cap.h = kHeight;
    cap.px.resize(rowBytes * static_cast<size_t>(kHeight));
    for (int y = 0; y < kHeight; ++y) {
        std::memcpy(&cap.px[static_cast<size_t>(y) * rowBytes],
                    &raw[static_cast<size_t>(kHeight - 1 - y) * rowBytes], rowBytes);
    }
    return cap;
}

bool VulkanSkinningPixelBackend::VerifySkinnedProgram(std::string* detail)
{
    using namespace Engine::RHI;

    const std::vector<uint32_t>* spirv =
        ShaderRegistry::Get().Find(StringID::Runtime("skinpix_vs"), ShaderStageType::Vertex);
    if (!spirv || spirv->empty()) {
        if (detail) *detail = "the skinned vertex shader is not in the registry";
        return false;
    }
    const ShaderReflectionData refl = ReflectSPIRV(spirv->data(), spirv->size());
    const ShaderResourceBinding* ubo = refl.Find(0u, 0u);
    if (!ubo || ubo->type != ShaderResourceType::UniformBuffer) {
        if (detail) *detail = "the vertex shader declares no uniform buffer at set 0 / binding 0, "
                              "so the bone matrices the PSO binds could not be read";
        return false;
    }
    if (ubo->size < kUboSize) {
        if (detail) *detail = "the reflected UBO is smaller than the " + std::to_string(kUboSize) +
                              " bytes the draw writes";
        return false;
    }
    if (!pso_) {
        if (detail) *detail = "no graphics pipeline state was created";
        return false;
    }
    if (detail) {
        *detail = "vertex SPIR-V reflects a " + std::to_string(ubo->size) +
                  " byte uniform buffer at set 0 / binding 0, and the pipeline exists";
    }
    return true;
}

} // namespace

std::unique_ptr<ISkinningPixelBackend> TryCreateVulkanSkinningBackend(std::string* why)
{
    return VulkanSkinningPixelBackend::Create(why);
}

} // namespace Test
} // namespace Engine

#else  // 未编译 Vulkan 后端或 shaderc

#include <string>

namespace Engine {
namespace Test {

std::unique_ptr<ISkinningPixelBackend> TryCreateVulkanSkinningBackend(std::string* why)
{
    if (why) {
#if defined(ENGINE_HAS_VULKAN)
        *why = "the Vulkan backend is compiled but shaderc is not, so the skinned shader cannot "
               "be compiled to SPIR-V at run time";
#else
        *why = "this build has no Vulkan backend (find_package(Vulkan) failed at configure time)";
#endif
    }
    return nullptr;
}

} // namespace Test
} // namespace Engine

#endif