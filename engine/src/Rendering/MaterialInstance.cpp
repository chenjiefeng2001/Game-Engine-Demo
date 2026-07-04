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
    // 构造 — 从 Master Material 的反射数据参数布局
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
    // BuildParameterLayout — 从反射构建参数映射
    // ════════════════════════════════════════════════════════

    // 用于参数布局构建的结构体定义（函数外，消除 MSVC 匿名 struct 问题）
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

        // 标准 PBR 参数布局（与 PBRMaterialParams std140 对齐，80 bytes）
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

        // 分配 CPU 镜像缓冲区（80 字节，与 PBRMaterialParams 一致）
        m_ParameterData.resize(k_PBRTotalSize, 0);

        // 填充 PBR 参数布局映射
        for (size_t i = 0; i < k_BuiltinParamCount; ++i) {
            const auto& bp = k_BuiltinParams[i];
            MaterialParamMeta meta;
            meta.name   = StringID::Runtime(bp.name);
            meta.type   = bp.type;
            meta.offset = bp.offset;
            meta.size   = bp.size;
            m_ParamLayout[meta.name.Value()] = meta;
        }

        // 纹理槽位
        for (size_t i = 0; i < k_TextureSlotCount; ++i) {
            const auto& slot = k_TextureSlots[i];
            MaterialParamMeta meta;
            meta.name    = StringID::Runtime(slot.name);
            meta.type    = MaterialParamType::Texture;
            meta.binding = slot.binding;
            meta.set     = slot.set;
            m_ParamLayout[meta.name.Value()] = meta;
        }

        m_Textures.reserve(k_TextureSlotCount);
    }

    // ════════════════════════════════════════════════════════
    // FindParamMeta — 查找参数元数据
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
    // SetParameterRaw — 通用参数设置
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

        // 确保数据大小匹配
        uint32_t copySize = (size < meta->size) ? size : meta->size;

        // 写入 CPU 镜像
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
    // SetTexture — 绑定纹理
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

        // 在纹理数组中查找是否已有此槽位
        for (auto& binding : m_Textures) {
            if (binding.set == meta->set && binding.binding == meta->binding) {
                binding.texture = std::move(tex);
                m_IsDirty = true;
                return true;
            }
        }

        // 新槽位
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

        // ── 1. 从 DynamicUBO 分配空间 ──
        UBOAllocation uboAlloc = allocator.Allocate(m_ParameterData.size());
        if (!uboAlloc.valid) {
            s_Log.Error("Failed to allocate UBO space for material '{}' (size={})",
                        m_Name.c_str(), m_ParameterData.size());
            return false;
        }

        // ── 2. 上传参数数据 ──
        if (uboAlloc.cpuPtr && m_IsDirty) {
            std::memcpy(uboAlloc.cpuPtr, m_ParameterData.data(), m_ParameterData.size());
        } else if (m_IsDirty) {
            s_Log.Warn("DynamicUBO not mapped, skipping data upload (material '{}')", m_Name.c_str());
        }

        // ── 3. 绑定 Constant Buffer（使用动态偏移） ──
        auto* uboBuffer = allocator.GetBuffer();
        if (uboBuffer) {
            cmd.SetConstantBuffer(0, 0, uboBuffer, uboAlloc.gpuOffset, m_ParameterData.size());
        } else {
            s_Log.Error("DynamicUBO buffer is null");
            return false;
        }

        // ── 4. 绑定所有纹理 ──
        // 注意：完整桥接需要 Texture 类暴露 IRHITexture*
        // 当前跳过纹理绑定，仅记录偏移

        // ── 5. 记录 UBO 偏移 ──
        m_LastUBOOffset = uboAlloc.gpuOffset;
        m_IsDirty = false;

        return true;
    }

} // namespace Rendering
} // namespace Engine