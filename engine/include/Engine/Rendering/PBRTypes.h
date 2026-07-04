#pragma once

/**
 * @file PBRTypes.h
 * @brief PBR 材质参数结构体 — 标准 Cook-Torrance 物理渲染模型
 *
 * 遵循 std140 布局规则，与 GPU 端 Uniform Buffer 对齐，
 * 确保 CPU/GPU 间零拷贝 memcpy 传递。
 *
 * 使用方式：
 * @code
 *   PBRMaterialParams params;
 *   params.baseColor    = {1.0f, 0.8f, 0.6f, 1.0f};  // 金色
 *   params.metallic     = 1.0f;
 *   params.roughness    = 0.3f;
 *   m_UBO->Upload(&params, sizeof(params));           // 直接上传到 GPU
 * @endcode
 */

#include "Engine/Types.h"
#include <cstdint>

namespace Engine {
namespace Rendering {

    /**
     * @brief PBR 材质参数 — 一个材质实例的 GPU 绑定数据
     *
     * 内存布局严格遵守 std140：
     *   - float4 → 16 字节对齐
     *   - float  → 4 字节对齐
     *   - 成员间不需要手动插入 padding（编译器自动对齐）
     */
    struct alignas(16) PBRMaterialParams {
        // ── 基础色 (BaseColor / Albedo) ──
        float baseColor[4]  = {1.0f, 1.0f, 1.0f, 1.0f};  // RGBA，A = 归一化透明度
        float metallic      = 0.0f;                         // 金属度 [0, 1]
        float roughness     = 0.5f;                         // 粗糙度 [0, 1]
        float ao            = 1.0f;                         // 环境光遮蔽 [0, 1]
        float _pad1         = 0.0f;                         // std140 对齐填充

        // ── 自发光 (Emissive) ──
        float emissive[4]   = {0.0f, 0.0f, 0.0f, 0.0f};   // RGB=颜色, A=强度

        // ── 纹理缩放 ──
        float normalStrength = 1.0f;   ///< 法线强度 [0, 2]
        float parallaxScale  = 0.0f;   ///< 视差贴图缩放
        float alphaCutoff    = 0.5f;   ///< Alpha 裁剪阈值
        float _pad2          = 0.0f;

        // ── 清漆 (Clear Coat) — 预留 ──
        float clearCoat      = 0.0f;
        float clearCoatRoughness = 0.0f;
        float _pad3[2]       = {0.0f, 0.0f};

        /// 默认构造（非 PBR 材质 = 纯白色 Lambertian）
        PBRMaterialParams() = default;

        /// 快速构造常用材质
        static PBRMaterialParams MetalGold() noexcept {
            PBRMaterialParams p;
            p.baseColor[0] = 1.0f; p.baseColor[1] = 0.766f;
            p.baseColor[2] = 0.336f; p.baseColor[3] = 1.0f;
            p.metallic  = 1.0f;
            p.roughness = 0.3f;
            return p;
        }

        static PBRMaterialParams PlasticWhite() noexcept {
            PBRMaterialParams p;
            p.baseColor[0] = p.baseColor[1] = p.baseColor[2] = 1.0f;
            p.baseColor[3] = 1.0f;
            p.metallic  = 0.0f;
            p.roughness = 0.5f;
            return p;
        }

        static PBRMaterialParams RubberDark() noexcept {
            PBRMaterialParams p;
            p.baseColor[0] = 0.2f; p.baseColor[1] = 0.2f;
            p.baseColor[2] = 0.2f; p.baseColor[3] = 1.0f;
            p.metallic  = 0.0f;
            p.roughness = 0.9f;
            return p;
        }

        static PBRMaterialParams EmissiveRed() noexcept {
            PBRMaterialParams p;
            p.baseColor[0] = 1.0f; p.baseColor[1] = 0.0f;
            p.baseColor[2] = 0.0f; p.baseColor[3] = 1.0f;
            p.emissive[0] = 1.0f; p.emissive[1] = 0.0f;
            p.emissive[2] = 0.0f; p.emissive[3] = 2.0f;  // 发光强度 2x
            return p;
        }
    };

    static_assert(sizeof(PBRMaterialParams) == 80,
                  "PBRMaterialParams must be 80 bytes for std140 alignment");
    static_assert(alignof(PBRMaterialParams) == 16,
                  "PBRMaterialParams must be 16-byte aligned for GPU UBO");

    // ============================================================
    // 纹理绑定标识符 — 材质使用的纹理槽位
    // ============================================================
    /**
     * @brief PBR 材质纹理集合的槽位索引
     *
     * 着色器期望的 bindless 布局：
     *   binding 0: Albedo    (sampler2D)
     *   binding 1: Normal    (sampler2D)
     *   binding 2: Metallic  (sampler2D)  — 或 R:Metallic, G:Roughness, B:AO
     *   binding 3: Roughness (sampler2D)  — 独立粗糙度贴图
     *   binding 4: AO        (sampler2D)
     *   binding 5: Emissive  (sampler2D)
     *   binding 6: Height    (sampler2D)  — Parallax mapping
     */
    struct PBRTextureSlots {
        StringID albedo    = SID("");   ///< 基础色贴图
        StringID normal    = SID("");   ///< 法线贴图
        StringID metallic  = SID("");   ///< 金属贴图
        StringID roughness = SID("");   ///< 粗糙度贴图
        StringID ao        = SID("");   ///< 环境光遮蔽贴图
        StringID emissive  = SID("");   ///< 自发光贴图
        StringID height    = SID("");   ///< 视差/高度贴图

        /// 是否有任何纹理绑定
        bool HasAny() const noexcept {
            return albedo || normal || metallic || roughness || ao || emissive || height;
        }
    };

} // namespace Rendering
} // namespace Engine