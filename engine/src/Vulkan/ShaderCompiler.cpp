/**
 * @file ShaderCompiler.cpp
 * @brief 基于 Shaderc 的着色器编译器 — GLSL/HLSL → SPIR-V
 *
 * 编译链：
 *   输入: GLSL / HLSL 源码 (字符串)
 *   中间: Shaderc 编译 → SPIR-V 二进制
 *   输出: SPIR-V 二进制 + SPIRV-Cross 反射 (UBO/Sampler 布局)
 *
 * 使用方式：
 * @code
 *   ShaderCompiler compiler;
 *   SpirVBinary spirv = compiler.Compile(source, ShaderStage::Vertex, "main");
 *   // 或从文件编译：
 *   SpirVBinary spirv = compiler.CompileFromFile("shaders/triangle.vert.glsl");
 * @endcode
 */

#include "Engine/Vulkan/VulkanCommon.h"
#include "Engine/Core/RHI/PSODesc.h"

#ifdef ENGINE_HAS_SHADERC
#include <shaderc/shaderc.hpp>
#endif

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// SPIR-V 二进制结果
// ════════════════════════════════════════════════════════════

struct SpirVBinary {
    std::vector<uint32_t> data;   ///< SPIR-V 32-bit 字
    std::string errorMessage;     ///< 编译错误信息
    bool success{false};          ///< 是否编译成功

    const uint32_t* Data() const noexcept { return data.data(); }
    size_t Size() const noexcept { return data.size() * sizeof(uint32_t); }
    size_t WordCount() const noexcept { return data.size(); }
};

// ════════════════════════════════════════════════════════════
// 着色器阶段枚举
// ════════════════════════════════════════════════════════════

enum class ShaderStage : uint8_t {
    Vertex   = 0,
    Fragment = 1,
    Compute  = 2,
    Geometry = 3,
    TessControl  = 4,
    TessEval     = 5,
};

// ════════════════════════════════════════════════════════════
// ShaderCompiler 类
// ════════════════════════════════════════════════════════════

class ShaderCompiler {
public:
    ShaderCompiler() noexcept {
#ifdef ENGINE_HAS_SHADERC
        try {
            m_Compiler = std::make_unique<shaderc::Compiler>();
        } catch (...) {
            std::fprintf(stderr, "[ShaderCompiler] Failed to create shaderc compiler\n");
        }
#endif
    }

    ~ShaderCompiler() = default;

    /// 是否可用（Shaderc 已编译）
    bool IsAvailable() const noexcept {
#ifdef ENGINE_HAS_SHADERC
        return m_Compiler != nullptr;
#else
        return false;
#endif
    }

    /// 编译源码到 SPIR-V
    SpirVBinary Compile(const std::string& source, ShaderStage stage,
                         const std::string& entryPoint = "main") {
        SpirVBinary result;

#ifdef ENGINE_HAS_SHADERC
        if (!m_Compiler) {
            result.errorMessage = "Shaderc compiler not initialized";
            return result;
        }

        shaderc_shader_kind kind = ToShaderKind(stage);
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                     shaderc_env_version_vulkan_1_3);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        options.SetSourceLanguage(shaderc_source_language_glsl);

        auto compilationResult = m_Compiler->CompileGlslToSpv(
            source.data(), source.size(), kind, "shader", entryPoint.c_str(), options);

        if (compilationResult.GetCompilationStatus() !=
            shaderc_compilation_status_success) {
            result.errorMessage = compilationResult.GetErrorMessage();
            std::fprintf(stderr, "[ShaderCompiler] Compilation error: %s\n",
                         result.errorMessage.c_str());
            return result;
        }

        // 复制 SPIR-V 二进制
        result.data.assign(compilationResult.begin(), compilationResult.end());
        result.success = true;
#else
        (void)source;
        (void)stage;
        (void)entryPoint;
        result.errorMessage = "Shaderc not available (ENGINE_HAS_SHADERC not defined)";
#endif
        return result;
    }

    /// 从文件编译
    SpirVBinary CompileFromFile(const std::string& filePath) {
        std::ifstream file(filePath, std::ios::in);
        if (!file.is_open()) {
            SpirVBinary result;
            result.errorMessage = "Cannot open file: " + filePath;
            return result;
        }

        std::stringstream ss;
        ss << file.rdbuf();
        std::string source = ss.str();

        // 从扩展名推断 shader stage
        ShaderStage stage = ShaderStage::Fragment;
        if (filePath.find(".vert") != std::string::npos) stage = ShaderStage::Vertex;
        else if (filePath.find(".frag") != std::string::npos) stage = ShaderStage::Fragment;
        else if (filePath.find(".comp") != std::string::npos) stage = ShaderStage::Compute;
        else if (filePath.find(".geom") != std::string::npos) stage = ShaderStage::Geometry;
        else if (filePath.find(".tesc") != std::string::npos) stage = ShaderStage::TessControl;
        else if (filePath.find(".tese") != std::string::npos) stage = ShaderStage::TessEval;

        return Compile(source, stage);
    }

    /// 从 StringID 获取着色器源码并编译
    SpirVBinary CompileFromRegistry(const std::string& shaderName) {
        // 在资源注册表中查找着色器
        // 简化：从 assets/shaders/ 目录加载
        std::vector<std::string> paths = {
            "assets/shaders/" + shaderName + ".vert.glsl",
            "assets/shaders/" + shaderName + ".frag.glsl",
            "assets/shaders/" + shaderName + ".comp.glsl",
            "assets/shaders/" + shaderName + ".glsl",
        };

        for (auto& path : paths) {
            SpirVBinary result = CompileFromFile(path);
            if (result.success) return result;
        }

        SpirVBinary result;
        result.errorMessage = "Shader not found: " + shaderName;
        return result;
    }

private:
#ifdef ENGINE_HAS_SHADERC
    std::unique_ptr<shaderc::Compiler> m_Compiler;

    static shaderc_shader_kind ToShaderKind(ShaderStage stage) {
        switch (stage) {
            case ShaderStage::Vertex:     return shaderc_glsl_vertex_shader;
            case ShaderStage::Fragment:   return shaderc_glsl_fragment_shader;
            case ShaderStage::Compute:    return shaderc_glsl_compute_shader;
            case ShaderStage::Geometry:   return shaderc_glsl_geometry_shader;
            case ShaderStage::TessControl: return shaderc_glsl_tess_control_shader;
            case ShaderStage::TessEval:   return shaderc_glsl_tess_evaluation_shader;
            default: return shaderc_glsl_infer_from_source;
        }
    }
#else
    void* m_Compiler{nullptr};
#endif
};

} // namespace RHI
} // namespace Engine