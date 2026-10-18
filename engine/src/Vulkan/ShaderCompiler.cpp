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
#include "Engine/Vulkan/ShaderRegistry.h"
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


// ════════════════════════════════════════════════════════════
// ShaderRegistry 实现
//
// 放在本文件是因为 ShaderCompiler 是文件内私有类（无法从其它 TU 引用）。
// 本文件在 src/Vulkan/ 下，engine/CMakeLists.txt 的 GLOB 会自动纳入。
// ════════════════════════════════════════════════════════════

namespace {
// RHI::ShaderStageType → 本文件私有的 RHI::ShaderStage 枚举。
ShaderStage ToLocalStage(::Engine::ShaderStageType stage)
{
    switch (stage) {
    case ::Engine::ShaderStageType::Vertex:   return ShaderStage::Vertex;
    case ::Engine::ShaderStageType::Fragment: return ShaderStage::Fragment;
    case ::Engine::ShaderStageType::Compute:  return ShaderStage::Compute;
    case ::Engine::ShaderStageType::Geometry: return ShaderStage::Geometry;
    case ::Engine::ShaderStageType::TessControl:    return ShaderStage::TessControl;
    case ::Engine::ShaderStageType::TessEvaluation: return ShaderStage::TessEval;
    default: return ShaderStage::Fragment;
    }
}
} // namespace

ShaderRegistry& ShaderRegistry::Get()
{
    static ShaderRegistry instance;
    return instance;
}

bool ShaderRegistry::RegisterCompiled(const std::string& name, ShaderStageType stage,
                                      std::vector<uint32_t>&& words)
{
    if (words.empty()) return false;
    StringID id = StringID::Runtime(name);
    Key key{ id.Value(), static_cast<uint8_t>(stage) };
    m_Shaders[std::move(key)] = std::move(words);
    return true;
}

bool ShaderRegistry::RegisterSource(const std::string& name, ShaderStageType stage,
                                    const std::string& glslSource)
{
#ifdef ENGINE_HAS_SHADERC
    ShaderCompiler compiler;
    // 本文件在 Engine::RHI 内定义了自己的 ShaderStage 枚举（且 IGraphicsFactory.h
    // 在同一命名空间还有一个不透明前向声明），因此 ShaderStage 与
    // ShaderStageType::Vertex 都不能直接对应，必须显式映射。
    SpirVBinary result = compiler.Compile(glslSource, ToLocalStage(stage));
    if (!result.success) {
        std::fprintf(stderr, "[ShaderRegistry] compile failed for '%s': %s\n",
                     name.c_str(), result.errorMessage.c_str());
        return false;
    }
    return RegisterCompiled(name, stage, std::move(result.data));
#else
    (void)name; (void)stage; (void)glslSource;
    std::fprintf(stderr, "[ShaderRegistry] shaderc unavailable (ENGINE_HAS_SHADERC undefined)\n");
    return false;
#endif
}

bool ShaderRegistry::RegisterFile(const std::string& name, ShaderStageType stage,
                                  const std::string& glslPath)
{
    std::ifstream in(glslPath, std::ios::binary);
    if (!in.is_open()) {
        std::fprintf(stderr, "[ShaderRegistry] cannot open '%s'\n", glslPath.c_str());
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return RegisterSource(name, stage, ss.str());
}

bool ShaderRegistry::RegisterSpirv(const std::string& name, ShaderStageType stage,
                                   const uint32_t* data, size_t wordCount)
{
    if (!data || wordCount == 0) return false;
    return RegisterCompiled(name, stage, std::vector<uint32_t>(data, data + wordCount));
}

bool ShaderRegistry::RegisterSpirvFile(const std::string& name, ShaderStageType stage,
                                       const std::string& spvPath)
{
    std::ifstream in(spvPath, std::ios::binary);
    if (!in.is_open()) {
        std::fprintf(stderr, "[ShaderRegistry] cannot open SPIR-V '%s'\n", spvPath.c_str());
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string bytes = ss.str();

    // 长度不是 4 的整数倍说明这文件不是 SPIR-V 字流。直接按字读会读出半个字，
    // 而 vkCreateShaderModule 随后会以难以定位的方式失败 —— 在这里就报出来。
    if (bytes.empty() || (bytes.size() % 4u) != 0u) {
        std::fprintf(stderr, "[ShaderRegistry] '%s' is %zu bytes: not a whole number of "
                             "32-bit SPIR-V words\n", spvPath.c_str(), bytes.size());
        return false;
    }
    // SPIR-V 魔数 0x07230203。文件存在不等于内容是 SPIR-V：这里顺手验一道，
    // 免得一个被截断或被别的格式覆盖的 .spv 安静地变成"注册成功"。
    const uint32_t magic = static_cast<uint32_t>(
        static_cast<uint8_t>(bytes[0])       | (static_cast<uint8_t>(bytes[1]) << 8) |
        (static_cast<uint8_t>(bytes[2]) << 16) | (static_cast<uint8_t>(bytes[3]) << 24));
    if (magic != 0x07230203u) {
        std::fprintf(stderr, "[ShaderRegistry] '%s' does not start with the SPIR-V magic "
                             "(got 0x%08x, want 0x07230203)\n", spvPath.c_str(), magic);
        return false;
    }

    const size_t wordCount = bytes.size() / 4u;
    std::vector<uint32_t> words(wordCount);
    for (size_t i = 0; i < wordCount; ++i) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data()) + i * 4u;
        words[i] = static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                   (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    return RegisterCompiled(name, stage, std::move(words));
}

const std::vector<uint32_t>* ShaderRegistry::Find(StringID id, ShaderStageType stage) const
{
    Key key{ id.Value(), static_cast<uint8_t>(stage) };
    auto it = m_Shaders.find(key);
    return (it == m_Shaders.end()) ? nullptr : &it->second;
}

size_t ShaderRegistry::Count() const noexcept { return m_Shaders.size(); }
} // namespace RHI
} // namespace Engine
