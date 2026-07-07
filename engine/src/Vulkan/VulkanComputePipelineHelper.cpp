/**
 * @file VulkanComputePipelineHelper.cpp
 * @brief Vulkan 计算管线辅助 — 真正的 CreateComputePSO 实现（含着色器字节码）
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/Log.h"
#include <vector>

namespace Engine { namespace RHI {

static Logger s_Log("VulkanCompute");

/**
 * @brief 使用 SPIR-V 字节码创建 Vulkan 计算管线
 * 
 * @param device        VkDevice
 * @param pipelineCache VkPipelineCache (VK_NULL_HANDLE 也可)
 * @param shaderSPIRV   SPIR-V 字节码
 * @param entryPoint    入口函数名（通常 "main"）
 * @return VkPipeline   或 VK_NULL_HANDLE
 */
VkPipeline CreateVulkanComputePipeline(
    VkDevice device,
    VkPipelineCache pipelineCache,
    const std::vector<uint32_t>& shaderSPIRV,
    const char* entryPoint)
{
    if (!device || shaderSPIRV.empty()) return VK_NULL_HANDLE;

    // 创建着色器模块
    VkShaderModuleCreateInfo smCI{};
    smCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smCI.codeSize = shaderSPIRV.size() * sizeof(uint32_t);
    smCI.pCode = shaderSPIRV.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smCI, nullptr, &shaderModule) != VK_SUCCESS) {
        s_Log.Error("Failed to create compute shader module");
        return VK_NULL_HANDLE;
    }

    // PipelineLayout（简化：无 push constant 和 descriptor set，由外部绑定）
    VkPipelineLayoutCreateInfo plCI{};
    plCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plCI.setLayoutCount = 0;
    plCI.pSetLayouts = nullptr;
    plCI.pushConstantRangeCount = 0;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &plCI, nullptr, &pipelineLayout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, shaderModule, nullptr);
        s_Log.Error("Failed to create compute pipeline layout");
        return VK_NULL_HANDLE;
    }

    // Compute Pipeline 创建
    VkComputePipelineCreateInfo cpCI{};
    cpCI.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpCI.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpCI.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpCI.stage.module = shaderModule;
    cpCI.stage.pName = entryPoint ? entryPoint : "main";
    cpCI.layout = pipelineLayout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult result = vkCreateComputePipelines(device, pipelineCache, 1, &cpCI, nullptr, &pipeline);

    // 清理中间对象
    vkDestroyShaderModule(device, shaderModule, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

    if (result != VK_SUCCESS) {
        s_Log.Error("vkCreateComputePipelines failed: {}", (int)result);
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

}} // namespace Engine::RHI