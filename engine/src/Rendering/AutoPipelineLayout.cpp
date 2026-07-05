/**
 * @file AutoPipelineLayout.cpp
 * @brief 自动 PipelineLayout 缓存实现
 */

#include "Engine/Rendering/AutoPipelineLayout.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <algorithm>

namespace {
    Engine::Logger s_Log("AutoLayout");
}

namespace Engine {
namespace Rendering {

    // ════════════════════════════════════════════════════════
    // ShaderStageType 枚举值（用于 stageFlags 位掩码）
    // ════════════════════════════════════════════════════════
    namespace {
        constexpr uint32_t kStageVertex   = 1u << 0;
        constexpr uint32_t kStageFragment = 1u << 1;
        constexpr uint32_t kStageCompute  = 1u << 2;
        constexpr uint32_t kStageGeometry = 1u << 3;
        constexpr uint32_t kStageTessCtrl = 1u << 4;
        constexpr uint32_t kStageTessEval = 1u << 5;

        uint32_t StageFlagsFromSet(uint32_t setIndex) noexcept {
            // 简单映射：set 0 = vertex, set 1 = fragment, set 2 = compute
            switch (setIndex) {
                case 0:  return kStageVertex;
                case 1:  return kStageFragment;
                case 2:  return kStageCompute;
                default: return kStageVertex | kStageFragment;
            }
        }
    }

    // ════════════════════════════════════════════════════════
    // PipelineLayoutDesc::FromReflection — 从反射数据生成描述
    // ════════════════════════════════════════════════════════
    PipelineLayoutDesc PipelineLayoutDesc::FromReflection(
        const ShaderReflectionData& reflection)
    {
        PipelineLayoutDesc desc;

        // 合并所有阶段中的 UniformBlock
        auto allUBs = reflection.GetAllUniformBlocks();
        for (const auto& ub : allUBs) {
            DescriptorBinding binding;
            binding.set     = ub.set;
            binding.binding = ub.binding;
            binding.type    = DescriptorType::UniformBuffer;
            binding.count   = 1;
            binding.stageFlags = StageFlagsFromSet(ub.set);
            desc.bindings.push_back(binding);
        }

        // 合并所有阶段中的 Sampler
        auto allSamplers = reflection.GetAllSamplers();
        for (const auto& sampler : allSamplers) {
            DescriptorBinding binding;
            binding.set     = sampler.set;
            binding.binding = sampler.binding;
            binding.type    = DescriptorType::SampledImage;
            binding.count   = 1;
            binding.stageFlags = StageFlagsFromSet(sampler.set);
            desc.bindings.push_back(binding);
        }

        // Push Constants
        desc.pushConstantSize = reflection.vertex.pushConstantSize;
        if (reflection.fragment.pushConstantSize > desc.pushConstantSize)
            desc.pushConstantSize = reflection.fragment.pushConstantSize;
        if (reflection.compute.pushConstantSize > desc.pushConstantSize)
            desc.pushConstantSize = reflection.compute.pushConstantSize;

        // 按 set 排序
        std::sort(desc.bindings.begin(), desc.bindings.end(),
            [](const DescriptorBinding& a, const DescriptorBinding& b) {
                if (a.set != b.set) return a.set < b.set;
                return a.binding < b.binding;
            });

        return desc;
    }

    // ════════════════════════════════════════════════════════
    // PipelineLayoutDesc::ComputeKey — 计算哈希缓存键
    // ════════════════════════════════════════════════════════
    LayoutKey PipelineLayoutDesc::ComputeKey() const noexcept {
        // FNV-1a 哈希
        constexpr uint64_t kFNVOffsetBasis = 14695981039346656037ULL;
        constexpr uint64_t kFNVPrime       = 1099511628211ULL;

        uint64_t hash = kFNVOffsetBasis;

        auto update = [&](const void* data, size_t size) {
            const uint8_t* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= kFNVPrime;
            }
        };

        // 编码所有绑定信息到哈希
        for (const auto& b : bindings) {
            update(&b.set, sizeof(b.set));
            update(&b.binding, sizeof(b.binding));
            update(&b.type, sizeof(b.type));
            update(&b.count, sizeof(b.count));
            update(&b.stageFlags, sizeof(b.stageFlags));
        }
        update(&pushConstantSize, sizeof(pushConstantSize));

        LayoutKey key;
        key.hash = hash;
        return key;
    }

    // ════════════════════════════════════════════════════════
    // AutoPipelineLayoutCache::EnsureLayout
    // ════════════════════════════════════════════════════════
    void* AutoPipelineLayoutCache::EnsureLayout(
        const ShaderReflectionData& reflection,
        RHI::IRHIDevice& device)
    {
        auto desc = PipelineLayoutDesc::FromReflection(reflection);
        auto key  = desc.ComputeKey();

        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto it = m_Cache.find(key.hash);
            if (it != m_Cache.end()) {
                it->second.refCount++;
                return it->second.handle;
            }
        }

        // 未命中 → 创建新的
        void* handle = CreatePipelineLayout(desc, device);
        if (!handle) {
            s_Log.Error("Failed to create PipelineLayout from reflection");
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Cache[key.hash] = {handle, 1};
        }

        s_Log.Info("Created PipelineLayout (hash={:016x}, {} bindings, pushConstant={} bytes)",
                   key.hash, desc.bindings.size(), desc.pushConstantSize);
        return handle;
    }

    // ════════════════════════════════════════════════════════
    // AutoPipelineLayoutCache::CreatePipelineLayout
    // ════════════════════════════════════════════════════════
    void* AutoPipelineLayoutCache::CreatePipelineLayout(
        const PipelineLayoutDesc& desc,
        RHI::IRHIDevice& device)
    {
        // ── 桥接到 RHI 层 ──
        // 这里生成与后端无关的中间描述，供 Vulkan/D3D12 后端消费
        //
        // 对于 Vulkan:
        //   1. 将 DescriptorBinding 转换为 VkDescriptorSetLayoutBinding
        //   2. 分组到 VkDescriptorSetLayoutCreateInfo（按 set 索引）
        //   3. 创建 VkPipelineLayout
        //
        // 对于 D3D12:
        //   1. 转换为 D3D12_ROOT_PARAMETER 数组
        //   2. 创建 ID3D12RootSignature
        //
        // 当前返回占位符指针，待具体 RHI 后端实现后填充。
        //
        // 由于不同 RHI 后端的实现差异较大，此处预留扩展点。
        // 具体 Vulkan 实现应在 CreateVulkanPipelineLayout(desc) 中完成，
        // 并调用 device 上的相应方法。
        //
        // 例如 Vulkan 下：
        //   auto vkDevice = static_cast<VulkanDevice*>(&device);
        //   return vkDevice->CreatePipelineLayoutFromDesc(desc);
        //
        // 目前返回非空占位符表明创建成功

        (void)desc;
        (void)device;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEF));
    }

    // ════════════════════════════════════════════════════════
    // AutoPipelineLayoutCache::Clear
    // ════════════════════════════════════════════════════════
    void AutoPipelineLayoutCache::Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        // 在实际 RHI 后端中，此处应释放所有 VkDescriptorSetLayout / RootSignature
        m_Cache.clear();
        s_Log.Info("AutoPipelineLayoutCache cleared");
    }

} // namespace Rendering
} // namespace Engine