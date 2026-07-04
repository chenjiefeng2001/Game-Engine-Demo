/**
 * @file MasterMaterial.cpp
 * @brief Master Material — 变体编译 + 全局注册表实现
 */

#include "Engine/Rendering/MasterMaterial.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/ShaderStage.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/Log.h"
#include <sstream>

namespace {
    Engine::Logger s_Log("MasterMaterial");
}

namespace Engine {
namespace Rendering {

    // ============================================================
    // Macro 映射表：VariantKey 位 → 宏名称
    // ============================================================
    static const char* k_MacroNames[] = {
        "USE_NORMALMAP",
        "USE_EMISSIVE",
        "USE_AO",
        "ALPHA_TEST",
        "USE_PARALLAX",
        "SKINNED",
    };

    // ============================================================
    // GetMacros — 将变体键转换为宏定义列表
    // ============================================================
    std::vector<MacroDefinition> MasterMaterial::GetMacros(const VariantKey& key) const {
        std::vector<MacroDefinition> macros;
        for (int i = 0; i < 6; ++i) {
            uint32_t bit = 1u << i;
            if (key.Has(bit)) {
                macros.push_back({k_MacroNames[i], "1"});
            }
        }
        return macros;
    }

    // ============================================================
    // GenerateShaderPreamble — 生成 GLSL #define 前缀
    // ============================================================
    std::string MasterMaterial::GenerateShaderPreamble(const VariantKey& key) const {
        std::ostringstream oss;
        oss << "// --- Auto-generated preamble for variant " << key.flags << " ---\n";
        auto macros = GetMacros(key);
        for (const auto& m : macros) {
            oss << "#define " << m.name << " " << m.value << "\n";
        }
        oss << "// --- End preamble ---\n";
        return oss.str();
    }

    // ============================================================
    // GetOrCompileShader — 按需编译变体
    // ============================================================
    std::shared_ptr<Shader> MasterMaterial::GetOrCompileShader(
        const VariantKey& key, IGraphicsFactory& factory)
    {
        // 快速路径：已缓存
        {
            std::lock_guard<std::mutex> lock(m_VariantMutex);
            auto it = m_VariantCache.find(key.flags);
            if (it != m_VariantCache.end()) {
                return it->second;
            }
        }

        // 编译新变体
        std::string preamble = GenerateShaderPreamble(key);

        // 拼接完整源码
        std::string fullVertex   = preamble + m_VertexSrc;
        std::string fullFragment = preamble + m_FragmentSrc;

        // 目前：通过 ShaderStage 编译（OpenGL 后端）
        // 未来：通过 SpirVCompiler::CompileGLSL() → PSOCache 后获取
        ShaderStage vsStage{ShaderStageType::Vertex,   ShaderSourceType::GLSL, fullVertex};
        ShaderStage fsStage{ShaderStageType::Fragment, ShaderSourceType::GLSL, fullFragment};

        // 使用工厂的多阶段加载
        auto shader = factory.CreateShaderFromStages({vsStage, fsStage});

        if (!shader) {
            s_Log.Error("Failed to compile variant {} for material '{}'",
                        key.flags, m_Name);
            return nullptr;
        }

        // 缓存
        {
            std::lock_guard<std::mutex> lock(m_VariantMutex);
            m_VariantCache[key.flags] = shader;
        }

        s_Log.Info("Compiled variant {} ({} macros) for '{}'",
                   key.flags, GetMacros(key).size(), m_Name);
        return shader;
    }

    // ============================================================
    // Registry — 全局注册表
    // ============================================================
    void MasterMaterial::Registry::Register(std::shared_ptr<MasterMaterial> mm) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Materials[mm->GetID().Value()] = std::move(mm);
    }

    MasterMaterial* MasterMaterial::Registry::Find(StringID id) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Materials.find(id.Value());
        return (it != m_Materials.end()) ? it->second.get() : nullptr;
    }

} // namespace Rendering
} // namespace Engine