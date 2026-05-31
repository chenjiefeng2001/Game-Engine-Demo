#pragma once

/**
 * @file ShaderStage.h
 * @brief 着色器阶段定义 — 描述一个可编译的着色器单元
 *
 * 支持多种着色器阶段类型和源码格式。
 * 是 ShaderProgram 的基础构建块。
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <cstdint>

namespace Engine {

    // ════════════════════════════════════════════════
    // 着色器阶段类型（对应 GPU 管线各个可编程阶段）
    // ════════════════════════════════════════════════
    enum class ShaderStageType : uint8 {
        Vertex          = 0,    // 顶点着色器
        Fragment        = 1,    // 片段（像素）着色器
        Geometry        = 2,    // 几何着色器
        TessControl     = 3,    // 细分控制着色器（ Hull Shader ）
        TessEvaluation  = 4,    // 细分评估着色器（ Domain Shader ）
        Compute         = 5,    // 计算着色器
        // 未来扩展：
        // RayGen, RayMiss, RayClosestHit 等（光线追踪）
    };

    // ════════════════════════════════════════════════
    // 源码 / 中间表示 格式
    // ════════════════════════════════════════════════
    enum class ShaderSourceType : uint8 {
        GLSL,           ///< OpenGL Shading Language 源码
        SPIRV,          ///< SPIR-V 二进制中间表示
        HLSL,           ///< DirectX High-Level Shading Language
        MSL,            ///< Metal Shading Language
    };

    // ════════════════════════════════════════════════
    // 着色器阶段描述
    // ════════════════════════════════════════════════
    struct ShaderStage {
        ShaderStageType type;          // 管线阶段
        ShaderSourceType sourceType;   // 源码格式
        std::string      source;       // 源码文本或 SPIR-V 文件路径

        // 对于 SPIR-V 二进制格式
        const uint32_t* spirvData   = nullptr;
        size_t          spirvSize   = 0;  // 字节数

        // 对于 HLSL 需要指定入口函数名称
        std::string entryPoint = "main";

        // 便捷构造
        static ShaderStage FromGLSL(ShaderStageType type, const std::string& source) {
            return {type, ShaderSourceType::GLSL, source, nullptr, 0, "main"};
        }

        static ShaderStage FromFile(ShaderStageType type, const std::string& filePath) {
            return {type, ShaderSourceType::GLSL, filePath, nullptr, 0, "main"};
        }

        static ShaderStage FromSPIRV(ShaderStageType type,
                                      const uint32_t* data, size_t byteSize) {
            return {type, ShaderSourceType::SPIRV, "", data, byteSize, "main"};
        }
    };

    // ════════════════════════════════════════════════
    // 着色器反射数据（编译后查询）
    // ════════════════════════════════════════════════
    struct UniformInfo {
        std::string name;
        int32       location = -1;
        int32       size     = 0;
        uint32      type     = 0;   // GLenum 或平台相关类型枚举
        int32       binding  = -1;  // 对于 UBO/SSBO
    };

    struct ShaderReflection {
        std::vector<UniformInfo> uniforms;
        std::vector<UniformInfo> uniformBlocks;  // UBOs
        std::vector<UniformInfo> storageBlocks;  // SSBOs
        int32 maxUniformBufferBindings = 0;
    };

} // namespace Engine
