#pragma once

/**
 * @file RenderPacket.h
 * @brief 渲染数据包 — 从 ECS 中提取出的不可变渲染快照
 *
 * RenderPacket 是纯 POD 结构，可在线程间安全 memcpy。
 * SceneExtractor 负责将 ECS/GameObject 的数据提取为 RenderPacket。
 * RenderPacket 不再持有 shared_ptr，全部使用 Handle<T>。
 *
 * v2 RenderCommand 扩展：添加工厂函数从旧版 RenderCommand + Handle 创建。
 */

#include "Engine/Core/RHI/Handle.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include <cstdint>
#include <algorithm>
#include <vector>

namespace Engine {
namespace RHI {

// ════════════════════════════════════════════════════════════
// 64-bit Sort Key（排序键）
// ════════════════════════════════════════════════════════════
//
// 布局（从高位到低位）：
//   [1-bit Transparent] [31-bit PSO_ID] [32-bit Depth]
//
// 排序规则：
//   1. 不透明排在透明前面（0 < 1）
//   2. 同 PSO ID 的排在一起（减少状态切换）
//   3. 同 PSO 内按深度从前到后（减少 Overdraw）
//
struct SortKey {
    uint64_t key;

    static SortKey Make(bool transparent, uint32_t psoID, float depth) noexcept {
        SortKey sk;
        uint32_t depthInt;
        // float → uint32 单调映射（保留深度顺序）
        // 使用 reinterpret_cast 位模式，但 float 不保证顺序
        // 更稳健：用 uint32 max - uint32(depth * SCALE) 来反转
        constexpr float SCALE = 16777216.0f; // 2^24
        depthInt = static_cast<uint32_t>(depth * SCALE);
        if (depthInt > 0xFFFFFF) depthInt = 0xFFFFFF;

        sk.key = (static_cast<uint64_t>(transparent ? 1 : 0) << 63) |
                 (static_cast<uint64_t>(psoID & 0x7FFFFFFF) << 32) |
                 depthInt;
        return sk;
    }
};

// ════════════════════════════════════════════════════════════
// RenderPacket — 渲染数据包（POD，可 memcpy）
// ════════════════════════════════════════════════════════════

struct RenderPacket {
    // 变换矩阵（列主序，与 RenderCommand 兼容）
    float worldMatrix[16];

    // 资源句柄
    MeshHandle    meshHandle;
    TextureHandle textureHandle;
    ShaderHandle   shaderHandle;

    // 材质数据（从 MaterialInstance 提取）
    float baseColor[4];
    float uvOffset[4];     // [offsetU, offsetV, tilingU, tilingV]
    float metallic;
    float roughness;
    float ao;

    // 排序信息
    SortKey sortKey;
    uint32_t layerMask;    // 可见性遮罩

    /// 从旧版 RenderCommand 转换（填充适量默认值）
    static RenderPacket FromRenderCommand(const RenderCommand& cmd,
                                          MeshHandle mesh,
                                          TextureHandle tex,
                                          ShaderHandle shader)
    {
        RenderPacket pkt;
        // 复制矩阵
        for (int i = 0; i < 16; ++i) pkt.worldMatrix[i] = cmd.worldMatrix[i];
        // 资源句柄
        pkt.meshHandle    = mesh;
        pkt.textureHandle = tex;
        pkt.shaderHandle  = shader;
        // 材质默认值
        pkt.baseColor[0]  = cmd.color[0];
        pkt.baseColor[1]  = cmd.color[1];
        pkt.baseColor[2]  = cmd.color[2];
        pkt.baseColor[3]  = cmd.color[3];
        pkt.uvOffset[0]   = cmd.uv[0];
        pkt.uvOffset[1]   = cmd.uv[1];
        pkt.uvOffset[2]   = cmd.uv[2];
        pkt.uvOffset[3]   = cmd.uv[3];
        pkt.metallic      = 0.0f;
        pkt.roughness     = 0.5f;
        pkt.ao            = 1.0f;
        // 排序键（默认深度 0，后续可按需调整）
        pkt.sortKey       = SortKey::Make(false, 0, 0.0f);
        pkt.layerMask     = cmd.layerMask;
        return pkt;
    }
};

// ════════════════════════════════════════════════════════════
// SceneExtraction — 场景提取结果（每帧构建）
// ════════════════════════════════════════════════════════════

struct SceneExtraction {
    /// 所有提取的渲染包
    std::vector<RenderPacket> packets;

    /// 使用的线性分配器（临时内存，每帧重置）
    /// 使用 vector 的 push_back，后续可替换为 LinearAllocator

    void Clear() { packets.clear(); }

    // 排序：按 SortKey 升序
    void Sort() {
        std::sort(packets.begin(), packets.end(),
            [](const RenderPacket& a, const RenderPacket& b) {
                return a.sortKey.key < b.sortKey.key;
            });
    }
};

} // namespace RHI
} // namespace Engine