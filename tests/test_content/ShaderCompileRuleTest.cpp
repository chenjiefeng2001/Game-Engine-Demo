/**
 * @file ShaderCompileRuleTest.cpp
 * @brief ShaderCompileRule 必须真的产出 .spv，且产物必须能被 Vulkan 消费
 *
 * 此前这条规则只做两件事：把一个 `.spv` **路径**推进 `generatedFiles`，以及把 GLSL
 * 源码登记进运行时注册表。文件本身从未被写出。也就是说 `PipelineResult::outputFiles`
 * 会列出一个不存在的文件，而报告仍然是 success=true。
 *
 * 更糟的是三个失败都会静默：
 *   1. 编译失败只打印一行，然后照样 `return true`；
 *   2. 扩展名不认识时，在登记产物**之后**就 return true；
 *   3. `NeedsRebuild` 对缺失的源文件返回 false，被 Process 当作「跳过这条规则」。
 *
 * 这三个都是「失败表现为成功」，因此本文件全部按失败方向断言：产物必须存在、
 * 长度必须是整数个 32 位字、魔数必须对、坏 GLSL 必须让 Process 失败、坏文件必须被
 * 加载入口拒绝。最后再用一次真实的 `CreateGraphicsPSO` 证明写出的字节确实是 Vulkan
 * 接受的 SPIR-V ——「文件存在」不是「文件有效」。
 *
 * 依赖 Vulkan 后端与 shaderc：SPIR-V 编译器目前住在 engine/src/Vulkan 里
 * （ShaderCompiler 是文件私有类），所以「有 shaderc 但无 Vulkan」的构建里规则
 * 无法产出 .spv。此时整组用例显式跳过并写明原因，而不是伪装通过。
 */
#include <gtest/gtest.h>

#include "Engine/Core/FileSystem.h"
#include "Engine/Core/Resources/AssetPipeline.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#if defined(ENGINE_HAS_VULKAN) && defined(ENGINE_HAS_SHADERC)

#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/RHIBackend.h"
#include "Engine/Core/RHI/RHIDeviceFactory.h"
#include "Engine/Core/RHI/VulkanIRHIDevice.h"
#include "Engine/Core/StringID.h"
#include "Engine/Vulkan/ShaderRegistry.h"

namespace {

using namespace Engine;

const char* const kGoodGlsl = R"(#version 450
layout(set = 0, binding = 0, std140) uniform Push { mat4 mvp; } push;
layout(location = 0) in vec3 inPos;
layout(location = 0) out vec4 outColor;
void main() {
    gl_Position = push.mvp * vec4(inPos, 1.0);
    outColor = vec4(1.0, 0.5, 0.25, 1.0);
}
)";

// 片元着色器单独一份：**不能**拿顶点阶段的 SPIR-V 去当片元模块。SPIR-V 模块的
// 入口点带阶段，用错阶段是规范违规（validation 会报），而 CreateGraphicsPSO 在
// 查不到片元模块时只打印一行就继续 —— 那会造出一条「看起来建成了」的非法管线，
// 恰好是本用例要排除的东西。
const char* const kGoodFragGlsl = R"(#version 450
layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;
void main() { outColor = inColor; }
)";

// 故意写错：`this is not glsl`。存在的意义是证明编译失败会**失败**。
const char* const kBadGlsl = R"(#version 450
void main() { this is not glsl at all; }
)";

struct Scratch {
    std::filesystem::path root;

    explicit Scratch(const char* tag)
    {
        root = std::filesystem::temp_directory_path() /
               (std::string("engine_shader_rule_") + tag);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root / "src", ec);
    }
    ~Scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::string writeSource(const char* name, const char* text) const
    {
        const std::filesystem::path p = root / "src" / name;
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return {};
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
        return p.string();
    }

    std::string outDir() const { return (root / "out").string(); }
    std::string spvPath(const char* stem) const { return (root / "out" / (std::string(stem) + ".spv")).string(); }
};

// 用管线真正驱动规则，而不是直接调 Execute：走 Process 才能同时覆盖
// sourcePath 解析、规则匹配、NeedsRebuild 与产物登记这几段。
PipelineResult RunRule(const Scratch& scratch, const std::string& sourceFile, const char* entryName)
{
    AssetEntry entry;
    entry.path = sourceFile;
    entry.sourcePath = sourceFile;
    entry.fileName = entryName;
    entry.extension = "vert";
    entry.resourceType = ResourceType::Shader;
    // GetMatchingRules 匹配的是 **type** 而不是 resourceType（两者都在 AssetEntry
    // 里，pipeline 只读前者）。只设 resourceType 会得到「没有规则匹配」，
    // 而 Process 对此返回 success=true —— 一次看起来成功的空跑。
    entry.type = ResourceType::Shader;
    entry.isValid = true;

    AssetPipeline pipeline;
    pipeline.RegisterRule(std::make_shared<ShaderCompileRule>());
    return pipeline.Process(entry, scratch.outDir());
}

} // namespace

// ── 产物必须真的存在，且是整数个 32 位字 ──────────────────────────────────────
TEST(ShaderCompileRule, EmitsRealSpirvFile)
{
    Scratch scratch("emit");
    const std::string src = scratch.writeSource("rule_probe.vert", kGoodGlsl);
    ASSERT_FALSE(src.empty()) << "could not write the probe shader to " << scratch.root.string();

    const PipelineResult result = RunRule(scratch, src, "rule_probe.vert");
    ASSERT_TRUE(result.success) << "pipeline failed: " << result.errorMessage;

    // 报告里列出的每一个产物都必须真的在磁盘上。此前这里列的是一个从未被写出的路径。
    for (const std::string& f : result.outputFiles) {
        EXPECT_TRUE(FileSystem::Exists(FileSystem::ResolvePath(f)))
            << "pipeline reported a generated file that does not exist: " << f;
    }
    ASSERT_EQ(result.outputFiles.size(), 1u)
        << "a GLSL vertex shader must produce exactly one SPIR-V artefact";

    const std::string spv = result.outputFiles.front();
    const std::vector<uint8> bytes = FileSystem::ReadFile(spv);
    ASSERT_FALSE(bytes.empty()) << "SPIR-V file is empty: " << spv;
    EXPECT_EQ(bytes.size() % 4u, 0u)
        << "SPIR-V is a stream of 32-bit words; the file length must be a multiple of 4";

    // SPIR-V 魔数。与其相信"文件非空"，不如验证它自称是什么。
    ASSERT_GE(bytes.size(), 4u);
    const uint32 magic = static_cast<uint32>(bytes[0]) | (static_cast<uint32>(bytes[1]) << 8) |
                         (static_cast<uint32>(bytes[2]) << 16) | (static_cast<uint32>(bytes[3]) << 24);
    EXPECT_EQ(magic, 0x07230203u) << "written file does not carry the SPIR-V magic";

    // 注册表里也应有同一份，文件不是凭空出现的另一份数据。
    const std::vector<uint32_t>* inRegistry =
        RHI::ShaderRegistry::Get().Find(StringID::Runtime("rule_probe"), ShaderStageType::Vertex);
    ASSERT_NE(inRegistry, nullptr) << "the rule must also register the shader for Vulkan";
    EXPECT_EQ(inRegistry->size() * 4u, bytes.size())
        << "the registry words and the file must be the same compilation";
}

// ── 编译失败必须是失败，且原因要指向"编译" ─────────────────────────────────
TEST(ShaderCompileRule, BadGlslFailsAndAdvertisesNothing)
{
    Scratch scratch("bad");
    const std::string src = scratch.writeSource("rule_bad.vert", kBadGlsl);
    ASSERT_FALSE(src.empty());
    // 先证明源文件本身可读：否则「读不到源文件」也会让 Process 失败，本用例就会
    // 因为**错误的原因**通过 —— 那正是它要防的那类假绿。
    ASSERT_TRUE(FileSystem::Exists(src)) << "probe shader was not written: " << src;

    const PipelineResult result = RunRule(scratch, src, "rule_bad.vert");
    EXPECT_FALSE(result.success)
        << "a GLSL syntax error must fail the pipeline, not return success with a .spv entry";
    // 规则把失败原因写进 metadata["validation_error"]，RunRule 会抄进 errorMessage。
    // 断言它指向编译，否则「源文件读不到」与「GLSL 写错了」无法区分。
    EXPECT_NE(result.errorMessage.find("compile"), std::string::npos)
        << "the failure must be attributable to the compile step, got: " << result.errorMessage;
    for (const std::string& f : result.outputFiles) {
        EXPECT_FALSE(FileSystem::Exists(FileSystem::ResolvePath(f)))
            << "a failed compile must not leave an output file behind: " << f;
    }
}

// ── 缺失源文件不能被当作「不需要重建」而静默跳过 ─────────────────────────────
TEST(ShaderCompileRule, MissingSourceFailsInsteadOfSilentlySkipping)
{
    Scratch scratch("missing");
    AssetEntry entry;
    entry.path = (scratch.root / "src" / "does_not_exist.vert").string();
    entry.sourcePath = entry.path;
    entry.fileName = "does_not_exist.vert";
    entry.extension = "vert";
    entry.resourceType = ResourceType::Shader;
    entry.type = ResourceType::Shader;

    AssetPipeline pipeline;
    pipeline.RegisterRule(std::make_shared<ShaderCompileRule>());
    const PipelineResult result = pipeline.Process(entry, scratch.outDir());
    EXPECT_FALSE(result.success)
        << "a missing shader source must fail; previously NeedsRebuild returned false, which "
           "Process treats as 'skip this rule'";
}

// ── 产物必须能被消费：写出的字节要真的建得出管线 ──────────────────────────────
TEST(ShaderCompileRule, EmittedSpirvFeedsGraphicsPso)
{
    Scratch scratch("pso");
    const std::string vsSrc = scratch.writeSource("rule_vs.vert", kGoodGlsl);
    const std::string fsSrc = scratch.writeSource("rule_fs.frag", kGoodFragGlsl);
    ASSERT_FALSE(vsSrc.empty());
    ASSERT_FALSE(fsSrc.empty());

    const PipelineResult vs = RunRule(scratch, vsSrc, "rule_vs.vert");
    ASSERT_TRUE(vs.success) << vs.errorMessage;
    const PipelineResult fs = RunRule(scratch, fsSrc, "rule_fs.frag");
    ASSERT_TRUE(fs.success) << fs.errorMessage;

    // **只从磁盘读**，不从注册表拿：否则这个用例证明的还是内存路径，
    // 而磁盘产物的意义恰恰在于它是别的进程/别的时刻能读到的东西。
    ASSERT_TRUE(RHI::ShaderRegistry::Get().RegisterSpirvFile(
                    "rule_vs_from_disk", ShaderStageType::Vertex, vs.outputFiles.front()))
        << "the emitted vertex SPIR-V must load back through RegisterSpirvFile";
    ASSERT_TRUE(RHI::ShaderRegistry::Get().RegisterSpirvFile(
                    "rule_fs_from_disk", ShaderStageType::Fragment, fs.outputFiles.front()))
        << "the emitted fragment SPIR-V must load back through RegisterSpirvFile";

    std::unique_ptr<RHI::IRHIDevice> device =
        RHI::CreateRHIDevice(RHI::Backend::Vulkan, nullptr, 16, 16);
    if (!device) {
        GTEST_SKIP() << "no Vulkan device on this machine; the disk round trip is verified by "
                        "EmitsRealSpirvFile, but building a pipeline from it needs a device";
    }

    RHI::GraphicsPSODesc pso;
    pso.vertexShader = StringID::Runtime("rule_vs_from_disk");
    pso.pixelShader  = StringID::Runtime("rule_fs_from_disk");
    pso.rtvCount = 1;
    pso.rtvFormats[0] = RHI::Format::RGBA8_UNorm;
    pso.vertexInput.buffers.push_back({ 0u, 12u, RHI::VertexInputRate::Vertex });
    pso.vertexInput.attributes.push_back({ 0u, 0u, RHI::Format::RGB32_Float, 0u });
    // 顶点着色器声明了 set 0 / binding 0 的 UBO，pipeline layout 必须同样声明它：
    // layout 少了这个 set，vkCreateGraphicsPipelines 就是非法调用（validation 会报
    // "shader binding not declared in layout"），而不开 validation 时它只是静静地
    // 接受一个不合法的管线。
    pso.bindings.push_back({ 0u, 0u, RHI::DescriptorKind::UniformBuffer, 1u });
    const std::shared_ptr<RHI::IRHIPipelineState> pipeline = device->CreateGraphicsPSO(pso);
    EXPECT_NE(pipeline, nullptr)
        << "vkCreateGraphicsPipelines rejected the SPIR-V we just wrote to disk";
}

// ── 坏文件必须被加载入口拒绝，而不是「注册成功」 ─────────────────────────────
TEST(ShaderCompileRule, RegisterSpirvFileRejectsNonSpirv)
{
    Scratch scratch("corrupt");
    const std::string bad = scratch.writeSource("not_spirv.spv", "this is definitely not SPIR-V");
    ASSERT_FALSE(bad.empty());
    EXPECT_FALSE(RHI::ShaderRegistry::Get().RegisterSpirvFile(
                    "corrupt_probe", ShaderStageType::Vertex, bad))
        << "a file that does not carry the SPIR-V magic must be rejected, not registered";

    const std::string empty = scratch.writeSource("empty.spv", "");
    EXPECT_FALSE(RHI::ShaderRegistry::Get().RegisterSpirvFile(
                    "empty_probe", ShaderStageType::Vertex, empty))
        << "an empty file must be rejected";

    EXPECT_FALSE(RHI::ShaderRegistry::Get().RegisterSpirvFile(
                    "absent_probe", ShaderStageType::Vertex,
                    (scratch.root / "src" / "absent.spv").string()))
        << "a missing file must be rejected";

    EXPECT_EQ(RHI::ShaderRegistry::Get().Find(StringID::Runtime("corrupt_probe"),
                                              ShaderStageType::Vertex),
              nullptr)
        << "a rejected file must not leave anything in the registry";
}

#else  // 没有 Vulkan 后端或 shaderc

TEST(ShaderCompileRule, SkippedWithoutVulkanOrShaderc)
{
    GTEST_SKIP() << "ShaderCompileRule's SPIR-V output needs both the Vulkan backend (the "
                    "SPIR-V compiler lives in engine/src/Vulkan/ShaderCompiler.cpp) and shaderc; "
                    "neither is present in this build.";
}

#endif
