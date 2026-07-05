/**
 * @file MaterialInstance.cpp
 * @brief 材质实例实现 — 反射驱动的参数布局 + Dynamic UBO 提交
 */

#include "Engine/Rendering/MaterialInstance.h"
#include "Engine/Core/Log.h"
#include <cstring>
#include <algorithm>

namespace {
    Engine::Logger s_Log("MaterialInst");
}

namespace Engine {
namespace Rendering {

    // ════════════════════════════════════════════════════════
    // 构造 — 从 Master Material 的反射数据构建参数布局
    // ════════════════════════════════════════════════════════

    MaterialInstance::MaterialInstance(std::shared_ptr<MasterMaterial> master,
                                        std::string name)
        : m_Master(std::move(master))
        , m_Name(std::move(name))
    {
        if (!m_Master) {
            s_Log.Error("MaterialInstance constructed with null MasterMaterial");
            return;
        }

        // 构建参数布局
        BuildParameterLayout();

        s_Log.Info("Created MaterialInstance '{}' from master '{}' ({} params, {} data bytes)",
                   m_Name.c_str(), m_Master->GetName().c_str(),
                   m_ParamLayout.size(), m_ParameterData.size());
    }

    // ════════════════════════════════════════════════════════
    // BuildParameterLayout — 从 SPIR-V 反射构建参数映射
    // ════════════════════════════════════════════════════════

    // 硬编码 PBR 回退布局（当着色器反射不可用时使用）
    namespace {
        struct PBRParamEntry {
            const char*    name;
            MaterialParamType type;
            uint32_t       offset;
            uint32_t       size;
        };

        struct TexSlotEntry {
            const char* name;
            uint32_t    set;
            uint32_t    binding;
        };

        const PBRParamEntry k_BuiltinParams[] = {
            {"BaseColor",       MaterialParamType::Float4, 0,  16},
            {"Metallic",        MaterialParamType::Float,  16, 4},
            {"Roughness",       MaterialParamType::Float,  20, 4},
            {"AO",              MaterialParamType::Float,  24, 4},
            {"Emissive",        MaterialParamType::Float4, 32, 16},
            {"NormalStrength",  MaterialParamType::Float,  48, 4},
            {"ParallaxScale",   MaterialParamType::Float,  52, 4},
            {"AlphaCutoff",     MaterialParamType::Float,  56, 4},
            {"ClearCoat",       MaterialParamType::Float,  64, 4},
            {"ClearCoatRoughness", MaterialParamType::Float, 68, 4},
        };

        const size_t k_BuiltinParamCount = sizeof(k_BuiltinParams) / sizeof(k_BuiltinParams[0]);
        const uint32_t k_PBRTotalSize = 80;

        const TexSlotEntry k_TextureSlots[] = {
            {"AlbedoMap",   1, 0},
            {"NormalMap",   1, 1},
            {"MetallicMap", 1, 2},
            {"RoughnessMap",1, 3},
            {"AOMap",       1, 4},
            {"EmissiveMap", 1, 5},
            {"HeightMap",   1, 6},
        };

        const size_t k_TextureSlotCount = sizeof(k_TextureSlots) / sizeof(k_TextureSlots[0]);
    }

    void MaterialInstance::BuildParameterLayout() {
        m_ParamLayout.clear();
        m_ParameterData.clear();
        m_Textures.clear();

        // 尝试从 Master Material 获取着色器反射数据
        const RHI::ShaderReflectionData* reflection = nullptr;
        if (m_Master) {
            reflection = m_Master->GetReflection();
        }

        if (reflection && !reflection->resources.empty()) {
            // ── 数据驱动路径：从 SPIR-V 反射构建参数布局 ──
            // 计算总 UBO 大小（所有 UniformBuffer 的 size 之和）
            uint32_t totalUBOSize = 0;
            for (const auto& res : reflection->resources) {
                if (res.type == RHI::ShaderResourceType::UniformBuffer) {
                    totalUBOSize += res.size;
                }
            }

            // 分配 CPU 镜像缓冲区
            m_ParameterData.resize(totalUBOSize > 0 ? totalUBOSize : 1, 0);

            // 填充 UniformBuffer 参数
            uint32_t uboOffset = 0;
            for (const auto& res : reflection->resources) {
                if (res.type == RHI::ShaderResourceType::UniformBuffer) {
                    MaterialParamMeta meta;
                    meta.name   = StringID::Runtime(res.name.c_str());
                    meta.type   = MaterialParamType::Float4; // 通用类型
                    meta.offset = uboOffset;
                    meta.size   = res.size;
                    m_ParamLayout[meta.name.Value()] = meta;
                    uboOffset += res.size;
                }
            }

            // 填充 SampledImage 纹理槽
            for (const auto& res : reflection->resources) {
                if (res.type == RHI::ShaderResourceType::SampledImage) {
                    MaterialParamMeta meta;
                    meta.name    = StringID::Runtime(res.name.c_str());
                    meta.type    = MaterialParamType::Texture;
                    meta.set     = res.set;
                    meta.binding = res.binding;
                    m_ParamLayout[meta.name.Value()] = meta;
                }
            }

            s_Log.Info("Built parameter layout from shader reflection ({})",
                       static_cast<uint64_t>(reflection->resources.size()));
        } else {
            // ── 回退路径：硬编码 PBR 参数布局 ──
            m_ParameterData.resize(k_PBRTotalSize, 0);

            for (size_t i = 0; i < k_BuiltinParamCount; ++i) {
                const auto& bp = k_BuiltinParams[i];
                MaterialParamMeta meta;
                meta.name   = StringID::Runtime(bp.name);
                meta.type   = bp.type;
                meta.offset = bp.offset;
                meta.size   = bp.size;
                m_ParamLayout[meta.name.Value()] = meta;
            }

            for (size_t i = 0; i < k_TextureSlotCount; ++i) {
                const auto& slot = k_TextureSlots[i];
                MaterialParamMeta meta;
                meta.name    = StringID::Runtime(slot.name);
                meta.type    = MaterialParamType::Texture;
                meta.binding = slot.binding;
                meta.set     = slot.set;
                m_ParamLayout[meta.name.Value()] = meta;
            }
        }

        m_Textures.reserve(16);
    }

    // ════════════════════════════════════════════════════════
    // FindParamMeta
    // ════════════════════════════════════════════════════════

    const MaterialParamMeta* MaterialInstance::FindParamMeta(StringID name) const noexcept {
        auto it = m_ParamLayout.find(name.Value());
        if (it != m_ParamLayout.end()) {
            return &it->second;
        }
        return nullptr;
    }

    // ════════════════════════════════════════════════════════
    // SetParameter — 各类型重载
    // ════════════════════════════════════════════════════════

    void MaterialInstance::SetParameter(StringID name, float value) {
        SetParameterRaw(name, &value, sizeof(value));
    }

    void MaterialInstance::SetParameter2f(StringID name, const float* values) {
        SetParameterRaw(name, values, sizeof(float) * 2);
    }

    void MaterialInstance::SetParameter3f(StringID name, const float* values) {
        SetParameterRaw(name, values, sizeof(float) * 3);
    }

    void MaterialInstance::SetParameter4f(StringID name, const float* values) {
        SetParameterRaw(name, values, sizeof(float) * 4);
    }

    void MaterialInstance::SetParameterInt(StringID name, int32_t value) {
        SetParameterRaw(name, &value, sizeof(value));
    }

    // ════════════════════════════════════════════════════════
    // SetParameterRaw
    // ════════════════════════════════════════════════════════

    bool MaterialInstance::SetParameterRaw(StringID name, const void* data, uint32_t size) {
        auto* meta = FindParamMeta(name);
        if (!meta) {
            s_Log.Warn("Parameter not found (id={}) in material '{}'",
                       name.Value(), m_Name.c_str());
            return false;
        }

        if (meta->type == MaterialParamType::Texture) {
            s_Log.Warn("Parameter (id={}) is a texture slot, use SetTexture() instead",
                       name.Value());
            return false;
        }

        uint32_t copySize = (size < meta->size) ? size : meta->size;

        if (meta->offset + copySize <= m_ParameterData.size()) {
            std::memcpy(&m_ParameterData[meta->offset], data, copySize);
            m_IsDirty = true;
            return true;
        }

        s_Log.Error("Parameter write out of bounds: offset={}, size={}, buffer={}",
                    meta->offset, copySize, m_ParameterData.size());
        return false;
    }

    // ════════════════════════════════════════════════════════
    // SetTexture
    // ════════════════════════════════════════════════════════

    bool MaterialInstance::SetTexture(StringID name, std::shared_ptr<Texture> tex) {
        auto* meta = FindParamMeta(name);
        if (!meta) {
            s_Log.Warn("Texture slot not found (id={}) in material '{}'",
                       name.Value(), m_Name.c_str());
            return false;
        }

        if (meta->type != MaterialParamType::Texture) {
            s_Log.Warn("Parameter (id={}) is not a texture slot in material '{}'",
                       name.Value(), m_Name.c_str());
            return false;
        }

        for (auto& binding : m_Textures) {
            if (binding.set == meta->set && binding.binding == meta->binding) {
                binding.texture = std::move(tex);
                m_IsDirty = true;
                return true;
            }
        }

        MaterialTextureBinding binding;
        binding.set     = meta->set;
        binding.binding = meta->binding;
        binding.texture = std::move(tex);
        m_Textures.push_back(std::move(binding));
        m_IsDirty = true;

        return true;
    }

    // ════════════════════════════════════════════════════════
    // Apply — 提交材质参数到 GPU
    // ════════════════════════════════════════════════════════

    bool MaterialInstance::Apply(RHI::IRHICommandList& cmd,
                                  DynamicUBOAllocator& allocator)
    {
        if (!m_Master) {
            s_Log.Error("MaterialInstance '{}' has no MasterMaterial", m_Name.c_str());
            return false;
        }

        if (m_ParameterData.empty()) {
            s_Log.Warn("MaterialInstance '{}' has empty parameter data", m_Name.c_str());
            return false;
        }

        // 1. 从 DynamicUBO 分配空间
        UBOAllocation uboAlloc = allocator.Allocate(m_ParameterData.size());
        if (!uboAlloc.valid) {
            s_Log.Error("Failed to allocate UBO space for material '{}' (size={})",
                        m_Name.c_str(), m_ParameterData.size());
            return false;
        }

        // 2. 上传参数数据
        if (uboAlloc.cpuPtr && m_IsDirty) {
            std::memcpy(uboAlloc.cpuPtr, m_ParameterData.data(), m_ParameterData.size());
        }

        // 3. 绑定 Constant Buffer（使用动态偏移）
        auto* uboBuffer = allocator.GetBuffer();
        if (uboBuffer) {
            cmd.SetConstantBuffer(0, 0, uboBuffer, uboAlloc.gpuOffset, m_ParameterData.size());
        } else {
            s_Log.Error("DynamicUBO buffer is null");
            return false;
        }

        // 4. 记录 UBO 偏移
        m_LastUBOOffset = uboAlloc.gpuOffset;
        m_IsDirty = false;

        return true;
    }

} // namespace Rendering
} // namespace Engine