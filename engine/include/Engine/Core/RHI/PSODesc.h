#pragma once

/**
 * @file PSODesc.h
 * @brief PSO (Pipeline State Object) 描述符 — 打包渲染管线状态为不可变对象
 *
 * 设计理念：
 *   - 所有渲染状态打包到一个结构体中，零散状态不复存在
 *   - 通过 GetHash() 生成确定性哈希值，用于 PSOCache 的去重查找
 *   - 与具体 API 解耦（GraphicsPSODesc 无 OpenGL/Vulkan 依赖）
 */

#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/StringID.h"
#include <vector>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Engine {
namespace RHI {

    // ============================================================
    // 混合状态
    // ============================================================
    struct BlendDesc {
        bool        enable      = false;
        BlendFactor srcColor    = BlendFactor::SrcAlpha;
        BlendFactor dstColor    = BlendFactor::InvSrcAlpha;
        BlendOp     colorOp     = BlendOp::Add;
        BlendFactor srcAlpha    = BlendFactor::One;
        BlendFactor dstAlpha    = BlendFactor::One;
        BlendOp     alphaOp     = BlendOp::Add;
        uint8       writeMask   = 0xF;  // RGBA 通道写入掩码

        bool operator==(const BlendDesc& o) const {
            return enable == o.enable && srcColor == o.srcColor
                && dstColor == o.dstColor && colorOp == o.colorOp
                && srcAlpha == o.srcAlpha && dstAlpha == o.dstAlpha
                && alphaOp == o.alphaOp && writeMask == o.writeMask;
        }

        uint64_t Hash() const noexcept {
            if (!enable) return 0;
            uint64_t h = static_cast<uint64_t>(srcColor);
            h = h * 31 + static_cast<uint64_t>(dstColor);
            h = h * 31 + static_cast<uint64_t>(colorOp);
            h = h * 31 + static_cast<uint64_t>(srcAlpha);
            h = h * 31 + static_cast<uint64_t>(dstAlpha);
            h = h * 31 + static_cast<uint64_t>(alphaOp);
            h = h * 31 + writeMask;
            return h;
        }
    };

    // ============================================================
    // 深度/模板状态
    // ============================================================
    struct DepthStencilDesc {
        bool        depthTest   = true;
        bool        depthWrite  = true;
        CompareFunc depthFunc   = CompareFunc::LessEqual;
        bool        stencilTest = false;
        uint8       stencilReadMask  = 0xFF;
        uint8       stencilWriteMask = 0xFF;
        StencilOp   frontOp     = StencilOp::Keep;
        StencilOp   backOp      = StencilOp::Keep;

        bool operator==(const DepthStencilDesc& o) const {
            return depthTest == o.depthTest && depthWrite == o.depthWrite
                && depthFunc == o.depthFunc && stencilTest == o.stencilTest
                && stencilReadMask == o.stencilReadMask
                && stencilWriteMask == o.stencilWriteMask
                && frontOp == o.frontOp && backOp == o.backOp;
        }

        uint64_t Hash() const noexcept {
            if (!depthTest && !depthWrite) return 0;
            uint64_t h = depthTest ? 1 : 0;
            h = h * 31 + (depthWrite ? 1 : 0);
            h = h * 31 + static_cast<uint64_t>(depthFunc);
            return h;
        }
    };

    // ============================================================
    // 光栅化器状态
    // ============================================================
    struct RasterizerDesc {
        CullMode    cullMode    = CullMode::Back;
        FillMode    fillMode    = FillMode::Solid;
        bool        frontCCW    = true;          ///< 正面 = 逆时针
        int32       depthBias   = 0;
        float       slopeScaledDepthBias = 0.0f;
        bool        scissorTest = false;

        bool operator==(const RasterizerDesc& o) const {
            return cullMode == o.cullMode && fillMode == o.fillMode
                && frontCCW == o.frontCCW && depthBias == o.depthBias
                && slopeScaledDepthBias == o.slopeScaledDepthBias
                && scissorTest == o.scissorTest;
        }

        uint64_t Hash() const noexcept {
            uint64_t h = static_cast<uint64_t>(cullMode);
            h = h * 31 + static_cast<uint64_t>(fillMode);
            h = h * 31 + (frontCCW ? 1 : 0);
            return h;
        }
    };

    // ============================================================
    // 顶点输入契约
    // ============================================================
    //
    // 此前 GraphicsPSODesc 完全没有顶点输入的描述能力：没有 attribute、没有
    // stride、没有 binding。结果是带顶点缓冲的网格在 Vulkan 上根本无法绘制，
    // 离屏三角形只能靠 gl_VertexIndex 绕过整个缺口。
    //
    // 这里是**增量**字段，默认空。对 GL / D3D12 而言不填即维持原有行为，
    // 因此不构成对其它后端的强制改动。

    /** 顶点/实例步进率。 */
    enum class VertexInputRate : uint8 {
        Vertex   = 0,
        Instance = 1,
    };

    /** 一个顶点缓冲槽位。slot 对应 IRHICommandList::SetVertexBuffer 的槽位号。 */
    struct VertexBufferBinding {
        uint32          slot   = 0;   ///< 缓冲槽位（着色器外的绑定点）
        uint32          stride = 0;   ///< 每个顶点的字节步长
        VertexInputRate rate   = VertexInputRate::Vertex;

        bool operator==(const VertexBufferBinding& o) const noexcept {
            return slot == o.slot && stride == o.stride && rate == o.rate;
        }
    };

    /** 一个顶点属性。location 对应着色器里的 layout(location = N)。 */
    struct VertexInputAttribute {
        uint32  location = 0;   ///< 着色器中的 location
        uint32  binding  = 0;   ///< 对应 vertexBuffers 的下标
        Format  format   = Format::Unknown;
        uint32  offset   = 0;   ///< 在单个顶点内的字节偏移

        bool operator==(const VertexInputAttribute& o) const noexcept {
            return location == o.location && binding == o.binding
                && format == o.format && offset == o.offset;
        }
    };

    /** 顶点输入契约。 */
    struct VertexInputDesc {
        std::vector<VertexBufferBinding> buffers;
        std::vector<VertexInputAttribute>     attributes;

        bool operator==(const VertexInputDesc& o) const noexcept {
            return buffers == o.buffers && attributes == o.attributes;
        }

        uint64_t Hash() const noexcept {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&h](uint64_t v) {
                h ^= v;
                h *= 1099511628211ull;
            };
            mix(buffers.size());
            for (const auto& b : buffers) {
                mix(b.slot); mix(b.stride); mix(static_cast<uint64_t>(b.rate));
            }
            mix(attributes.size());
            for (const auto& a : attributes) {
                mix(a.location); mix(a.binding);
                mix(static_cast<uint64_t>(a.format)); mix(a.offset);
            }
            return h;
        }
    };

    // ============================================================
    // Graphics PSO 描述符（渲染管线）
    // ============================================================
    /**
 * @brief 描述符绑定类型。
 *
 * 描述符系统此前**整条链都不存在**：GraphicsPSODesc 没有绑定声明，
 * VulkanPipelineState 建的 pipeline layout 里 setLayoutCount 为 0，
 * 而 SetConstantBuffer / SetShaderResource 只是重绑一个恒为空的
 * currentDescriptorSet，从不写入描述符。
 */
enum class DescriptorKind : uint8 {
    UniformBuffer = 0,
    StorageBuffer,
    SampledTexture,
    Sampler,
    CombinedImageSampler,
};

struct DescriptorBindingDesc {
    uint32       set{0};
    uint32       binding{0};
    DescriptorKind kind{DescriptorKind::UniformBuffer};
    uint32       count{1};

    bool operator==(const DescriptorBindingDesc& o) const noexcept {
        return set == o.set && binding == o.binding && kind == o.kind && count == o.count;
    }
};

struct GraphicsPSODesc {
        StringID        vertexShader;       ///< 顶点着色器 ID（由 ShaderCompiler 注册）
        StringID        pixelShader;        ///< 像素着色器 ID
        BlendDesc       blend;
        DepthStencilDesc depthStencil;
        RasterizerDesc  rasterizer;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        Format          rtvFormats[8] = {}; ///< 最多 8 个渲染目标格式
        uint32          rtvCount      = 1;
        Format          dsvFormat     = Format::Unknown;
        uint32          sampleCount   = 1;
/** 顶点输入契约。留空表示该 PSO 不从顶点缓冲取数（如全屏三角形）。 */
          VertexInputDesc vertexInput;
          /**
           * 描述符绑定声明。留空表示该 PSO 不使用描述符（setLayoutCount = 0）。
           * 必须参与哈希与相等比较：布局不同的两个 PSO 若共用缓存条目，
           * pipeline layout 与着色器期望的 set 布局就会对不上。
           */
          std::vector<DescriptorBindingDesc> bindings;

        /**
         * @brief 计算确定性哈希值（用于 PSOCache 去重）
         *
         * 算法：组合所有子组件的哈希值并通过 FNV1a 风格混合。
         */
        uint64_t GetHash() const noexcept {
            uint64_t h = vertexShader.Value();
            h = h * 31 + pixelShader.Value();
            h = h * 31 + blend.Hash();
            h = h * 31 + depthStencil.Hash();
            h = h * 31 + rasterizer.Hash();
            h = h * 31 + static_cast<uint64_t>(topology);
            h = h * 31 + static_cast<uint64_t>(rtvFormats[0]);
            h = h * 31 + rtvCount;
            h = h * 31 + static_cast<uint64_t>(dsvFormat);
            h = h * 31 + sampleCount;
            // 顶点输入必须参与哈希：否则两个顶点布局不同、其余状态相同的 PSO
            // 会命中同一条缓存记录，拿到顶点绑定错误的管线。
h = h * 31 + vertexInput.Hash();
              for (const auto& b : bindings) {
                  h = h * 31 + b.set;
                  h = h * 31 + b.binding;
                  h = h * 31 + static_cast<uint32_t>(b.kind);
                  h = h * 31 + b.count;
              }
              return h;
          }

        bool operator==(const GraphicsPSODesc& o) const {
            return vertexShader == o.vertexShader
                && pixelShader == o.pixelShader
                && blend == o.blend
                && depthStencil == o.depthStencil
                && rasterizer == o.rasterizer
                && topology == o.topology
                && rtvCount == o.rtvCount
                && dsvFormat == o.dsvFormat
                && sampleCount == o.sampleCount
&& vertexInput == o.vertexInput
                  && bindings == o.bindings
                  && memcmp(rtvFormats, o.rtvFormats, sizeof(rtvFormats)) == 0;
        }

        /**
         * @brief 构建常用默认 PSO 描述符
         */
        static GraphicsPSODesc DefaultOpaque() noexcept {
            GraphicsPSODesc desc;
            desc.blend.enable = false;
            desc.depthStencil.depthTest = true;
            desc.depthStencil.depthWrite = true;
            desc.rasterizer.cullMode = CullMode::Back;
            return desc;
        }

        static GraphicsPSODesc DefaultTransparent() noexcept {
            GraphicsPSODesc desc;
            desc.blend.enable = true;
            desc.depthStencil.depthTest = true;
            desc.depthStencil.depthWrite = false;
            desc.rasterizer.cullMode = CullMode::None;
            return desc;
        }

        static GraphicsPSODesc DefaultUI() noexcept {
            GraphicsPSODesc desc;
            desc.blend.enable = true;
            desc.blend.srcColor = BlendFactor::SrcAlpha;
            desc.blend.dstColor = BlendFactor::InvSrcAlpha;
            desc.depthStencil.depthTest = false;
            desc.depthStencil.depthWrite = false;
            desc.rasterizer.cullMode = CullMode::None;
            desc.topology = PrimitiveTopology::TriangleList;
            return desc;
        }
    };

    // ============================================================
    // Compute PSO 描述符（计算管线）
    // ============================================================
    struct ComputePSODesc {
        StringID computeShader;

        uint64_t GetHash() const noexcept {
            return computeShader.Value();
        }

        bool operator==(const ComputePSODesc& o) const {
            return computeShader == o.computeShader;
        }
    };

} // namespace RHI
} // namespace Engine