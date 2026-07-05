/**
 * @file VulkanPipelineState.cpp
 * @brief Vulkan Pipeline State 实现 — 完整的 PSO 创建
 */

#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Vulkan/VulkanPipelineLayoutCache.h"
#include <cstdio>
#include <vector>

namespace Engine {
namespace RHI {

struct VulkanPipelineState::Impl {
    VkPipeline       pipeline{VK_NULL_HANDLE};
    VkPipelineLayout layout{VK_NULL_HANDLE};
    VkDevice         device{VK_NULL_HANDLE};
    bool             isCompute{false};

    ~Impl() {
        if (pipeline != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        // PipelineLayout 由 PipelineLayoutCache 管理，不在此销毁
    }
};

VulkanPipelineState::VulkanPipelineState() : m_Impl(std::make_unique<Impl>()) {}
VulkanPipelineState::~VulkanPipelineState() = default;

VkPipeline VulkanPipelineState::GetVkPipeline() const noexcept {
    return m_Impl->pipeline;
}

VkPipelineLayout VulkanPipelineState::GetVkPipelineLayout() const noexcept {
    return m_Impl->layout;
}

void VulkanPipelineState::SetNativeHandles(VkPipeline pipeline, VkPipelineLayout layout, VkDevice device) noexcept {
    m_Impl->pipeline = pipeline;
    m_Impl->layout = layout;
    m_Impl->device = device;
}

// ════════════════════════════════════════════════════════════
// 工厂：创建 Graphics PSO
// ════════════════════════════════════════════════════════════

IRHIPipelineState* CreateGraphicsPipeline(
    VkDevice device,
    VkPipelineLayout pipelineLayout,
    const GraphicsPSODesc& desc,
    VkShaderModule vsModule,
    VkShaderModule fsModule)
{
    auto pso = new VulkanPipelineState();
    pso->m_Impl->device = device;
    pso->m_Impl->layout = pipelineLayout;
    // layout 由 PipelineLayoutCache 管理，不由 PSO 销毁

    std::vector<VkPipelineShaderStageCreateInfo> stages;

    // 顶点着色器
    if (vsModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo vsCI{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        vsCI.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vsCI.module = vsModule;
        vsCI.pName = "main";
        stages.push_back(vsCI);
    }

    // 片元着色器
    if (fsModule != VK_NULL_HANDLE) {
        VkPipelineShaderStageCreateInfo fsCI{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        fsCI.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fsCI.module = fsModule;
        fsCI.pName = "main";
        stages.push_back(fsCI);
    }

    // 顶点输入
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    // 从 desc 解析 vertex buffer 布局
    // 简化：使用默认设置

    // 输入组装
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = TopologyToVk(desc.primitiveTopology);

    // 视口
    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 光栅化
    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    // 多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 深度模板
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depthStencil.depthTestEnable = desc.depthStencil.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    // 颜色混合
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = desc.blendState.blendEnable ? VK_TRUE : VK_FALSE;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    // 动态状态（视口+裁剪）
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Dynamic Rendering
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(desc.colorFormats.size());
    std::vector<VkFormat> colorFormats;
    for (auto fmt : desc.colorFormats) {
        colorFormats.push_back(FormatToVk(fmt));
    }
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = FormatToVk(desc.depthFormat);
    renderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

    // 创建 Graphics Pipeline
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.pNext = &renderingInfo;
    ci.stageCount = static_cast<uint32_t>(stages.size());
    ci.pStages = stages.data();
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState = &viewportState;
    ci.pRasterizationState = &rasterizer;
    ci.pMultisampleState = &multisampling;
    ci.pDepthStencilState = &depthStencil;
    ci.pColorBlendState = &colorBlend;
    ci.pDynamicState = &dynamicState;
    ci.layout = pipelineLayout;

    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pso->m_Impl->pipeline);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create graphics pipeline\n");
        delete pso;
        return nullptr;
    }

    return pso;
}

IRHIPipelineState* CreateComputePipeline(
    VkDevice device,
    VkPipelineLayout pipelineLayout,
    VkShaderModule csModule)
{
    auto pso = new VulkanPipelineState();
    pso->m_Impl->device = device;
    pso->m_Impl->layout = pipelineLayout;
    pso->m_Impl->isCompute = true;

    VkPipelineShaderStageCreateInfo csCI{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    csCI.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    csCI.module = csModule;
    csCI.pName = "main";

    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = csCI;
    ci.layout = pipelineLayout;

    VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pso->m_Impl->pipeline);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[Vulkan] Failed to create compute pipeline\n");
        delete pso;
        return nullptr;
    }

    return pso;
}

} // namespace RHI
} // namespace Engine