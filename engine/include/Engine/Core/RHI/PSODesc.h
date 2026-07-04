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
#include <cstdint>
#include <cstring>

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
    // Graphics PSO 描述符（渲染管线）
    // ============================================================
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