#pragma once
/**
 * @file ShaderRegistry.h
 * @brief 运行时着色器注册表 —— GraphicsPSODesc 的 StringID 到 SPIR-V 的解析
 *
 * 为什么需要它：GraphicsPSODesc 用 StringID 引用顶点/像素着色器，但 StringID 是
 * **单向哈希**（只有 Value() 返回 uint64_t，没有任何从 ID 反解回名字的接口）。
 * 因此不存在「拿着 ID 去找着色器」的路——必须有一个在着色器加载时填充、
 * 以 StringID 的哈希值为键、存放已编译 SPIR-V 的运行时注册表。
 *
 * 在此之前 VulkanDevice::CreateGraphicsPSO 向 CreateGraphicsPipeline 传的是两个
 * VK_NULL_HANDLE，而后者对空模块的处理是 stages.empty() → 返回 nullptr，
 * 于是图形管线**永远无法创建**。本注册表补上的正是这条缺失的解析。
 *
 * 与 ShaderCompiler 的关系：ShaderCompiler 是文件内私有类，负责 GLSL→SPIR-V；
 * 本注册表在它之上，按 StringID 缓存结果并对外提供查找。
 */
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RenderResources/ShaderStage.h"
#include "Engine/Core/StringID.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
namespace RHI {

/**
 * 着色器注册表。单例，按需填充。
 *
 * 典型用法（加载期）：
 * @code
 *   ShaderRegistry::Get().RegisterSource("vktri", ShaderStageType::Vertex,
 *                                        "assets/shaders/vktri.vert.glsl");
 *   // 然后 GraphicsPSODesc::vertexShader = StringID::Runtime("vktri");
 * @endcode
 *
 * 注意：源码与 SPIR-V 均不跨帧缓存失效；同名重复 Register 会覆盖。
 */
class ShaderRegistry {
public:
    static ShaderRegistry& Get();

    /** 以源码文本登记一个着色器（立即编译）。失败返回 false 并输出原因。 */
    bool RegisterSource(const std::string& name, ShaderStageType stage, const std::string& glslSource);

    /** 以 GLSL 文件路径登记。路径不存在或编译失败返回 false。 */
    bool RegisterFile(const std::string& name, ShaderStageType stage, const std::string& glslPath);

    /**
     * 以已编译的 SPIR-V 登记（供已有编译管线的接入方使用）。
     * data 必须是 32-bit 字数组，wordCount 为字数。
     */
    bool RegisterSpirv(const std::string& name, ShaderStageType stage,
                        const uint32_t* data, size_t wordCount);

    /**
     * 以磁盘上的 .spv 文件登记。这是 ShaderCompileRule 产出物的**消费入口**：
     * 在此之前，全仓库没有任何代码从磁盘读 .spv（RegisterFile 读的是 GLSL），
     * 于是规则写出的文件即使存在也没有读者，闭环无法验证。
     *
     * 文件按 4 字节小端字读入（与 SPIR-V 规范及本编译器输出一致）。
     * 文件不存在、长度为 0 或不是 4 的整数倍都返回 false 并说明原因 ——
     * 一个「能打开但内容不是 SPIR-V」的文件必须在这里就暴露。
     */
    bool RegisterSpirvFile(const std::string& name, ShaderStageType stage,
                           const std::string& spvPath);

    /**
     * 按 GraphicsPSODesc 中的 StringID 取 SPIR-V。
     * 返回 nullptr 表示该 ID 未登记。
     */
    const std::vector<uint32_t>* Find(StringID id, ShaderStageType stage) const;

    /** 已登记的着色器数量（两个 stage 分别计数）。 */
    size_t Count() const noexcept;

private:
    ShaderRegistry() = default;

    struct Key {
        uint64_t id;
        uint8_t  stage;
        bool operator==(const Key& o) const noexcept { return id == o.id && stage == o.stage; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            return std::hash<uint64_t>{}(k.id) ^ (static_cast<size_t>(k.stage) << 56);
        }
    };

    bool RegisterCompiled(const std::string& name, ShaderStageType stage,
                          std::vector<uint32_t>&& words);

    std::unordered_map<Key, std::vector<uint32_t>, KeyHash> m_Shaders;
};

} // namespace RHI
} // namespace Engine