#pragma once

/**
 * @file LODSystem.h
 * @brief LOD (Level of Detail) / HLOD (Hierarchical LOD) 系统
 *
 * 设计灵感：Unreal 的 DistantField / Nanite LOD, Frostbite 的 HLOD 链
 *
 * 核心算法：
 *   - 屏幕空间误差估算：根据物体到相机的距离 + FOV 计算近似像素大小
 *   - LOD 链切换：每个 mesh 挂接 LOD0 → LOD1 → LOD2 → ... → HLOD
 *   - HLOD 合并：将远距离物体的多个 mesh 合并为单一代理（Proxy Mesh）
 *   - Dithering 过渡：相邻 LOD 之间使用屏幕空间 dither 融合
 *
 * LOD 策略：
 *   - DistanceBased: 简单的距离阈值
 *   - ScreenSizeBased: 屏幕空间占比阈值（推荐）
 *   - Custom: 用户自定义回调
 *
 * 使用方式：
 * @code
 *   LODSystem::Get().RegisterAsset(meshAsset, {
 *       LODLevel(0.0f, "assets/meshes/hero_lod0.fbx"),   // LOD0: < 10m
 *       LODLevel(10.0f, "assets/meshes/hero_lod1.fbx"),  // LOD1: 10-30m
 *       LODLevel(30.0f, "assets/meshes/hero_lod2.fbx"),  // LOD2: 30-80m
 *       LODLevel(80.0f, "", true),                        // HLOD: > 80m (代理合并)
 *   });
 *
 *   // 每帧选择 LOD：
 *   uint32_t lod = LODSystem::Get().SelectLOD(meshGUID, cameraPos, screenSize);
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/StringID.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <cstdint>

namespace Engine {
namespace Rendering {

    // ============================================================
    // LOD 级别定义
    // ============================================================
    /**
     * @brief 单个 LOD 级别描述
     */
    struct LODLevel {
        float       distance    = 0.0f;    ///< 切换距离（屏幕像素占比或世界距离，取决于筛选策略）
        std::string meshPath;              ///< 此 LOD 级别的 mesh 路径
        float       pixelError  = 0.0f;    ///< 允许的像素误差（ScreenSizeBased 模式）
        uint32_t    triangleCount = 0;     ///< 三角形面数（统计用）
        bool        isHLOD      = false;   ///< 是否为 HLOD 代理 mesh
        bool        isBillboard = false;   ///< 是否为 billboard 代理
    };

    // ============================================================
    // LOD 资产描述符
    // ============================================================
    /**
     * @brief 一个 Mesh 资产的完整 LOD 链
     */
    struct LODAsset {
        StringID             assetID;         ///< 资产 GUID
        std::string          assetName;       ///< 显示名称
        std::vector<LODLevel> lodLevels;      ///< LOD 链（LOD0 在最前）
        float                lodBias   = 1.0f; ///< 全局 LOD 偏移（>1 = 更远才切换）

        /** 获取 LOD 级别数 */
        uint32_t GetLODCount() const noexcept { return static_cast<uint32_t>(lodLevels.size()); }

        /** 获取指定 LOD 级别的 mesh 路径 */
        const std::string& GetMeshPath(uint32_t lod) const noexcept {
            static const std::string empty;
            return (lod < lodLevels.size()) ? lodLevels[lod].meshPath : empty;
        }
    };

    // ============================================================
    // LOD 选择策略
    // ============================================================
    enum class LODStrategy : uint8 {
        DistanceBased     = 0,  ///< 基于世界距离
        ScreenSizeBased   = 1,  ///< 基于屏幕空间占比（推荐）
        PixelErrorBased   = 2,  ///< 基于像素误差容限
        Custom            = 3,  ///< 用户自定义回调
    };

    // ============================================================
    // LOD 选择结果
    // ============================================================
    struct LODSelectionResult {
        uint32_t selectedLOD    = 0;         ///< 选中的 LOD 级别
        float    screenSize     = 0.0f;      ///< 当前屏幕空间占比
        float    transition     = 0.0f;      ///< 过渡系数（0-1，用于 dither）
        bool     isHLODSelected = false;     ///< 是否选中 HLOD 代理
        float    distance       = 0.0f;      ///< 距相机距离
    };

    // ============================================================
    // HLOD 配置
    // ============================================================
    struct HLODConfig {
        bool     enabled         = true;
        uint32_t minClusterSize  = 4;        ///< 最少物体数触发 HLOD 合并
        float    mergeDistance   = 100.0f;    ///< HLOD 生效的最小距离
        float    cellSize        = 50.0f;     ///< HLOD 空间划分单元大小
        uint32_t maxProxyTriangles = 2000;    ///< 代理 mesh 的最大三角形数
    };

    // ============================================================
    // LOD 系统 — 主接口
    // ============================================================
    /**
     * @brief 全局 LOD 管理系统
     *
     * 线程安全。所有查询为只读（const），注册在初始化期间进行。
     */
    class LODSystem {
    public:
        static LODSystem& Get() {
            static LODSystem instance;
            return instance;
        }

        LODSystem(const LODSystem&) = delete;
        LODSystem& operator=(const LODSystem&) = delete;

        // ── 资产注册 ──

        /** 注册一个 LOD 资产 */
        void RegisterAsset(std::shared_ptr<LODAsset> asset);

        /** 移除一个 LOD 资产 */
        void UnregisterAsset(StringID assetID);

        // ── LOD 选择 ──

        /**
         * @brief 根据屏幕空间大小选择 LOD 级别
         *
         * @param assetID     资产 GUID
         * @param screenSize  物体在屏幕上的近似占比（0-1），如 0.3 = 占屏幕 30%
         * @param distance    距相机距离（世界单位）
         * @return LOD 选择结果
         */
        LODSelectionResult SelectLOD(StringID assetID,
                                     float screenSize,
                                     float distance) const;

        /**
         * @brief 使用自定义选择回调
         */
        using CustomSelector = std::function<uint32_t(const LODAsset&,
                                                      float screenSize,
                                                      float distance)>;

        void SetCustomSelector(StringID assetID, CustomSelector selector);

        // ── HLOD ──

        /** 获取 HLOD 配置 */
        const HLODConfig& GetHLODConfig() const noexcept { return m_HLODConfig; }
        void SetHLODConfig(const HLODConfig& cfg) { m_HLODConfig = cfg; }

        // ── 统计 ──

        uint32_t GetAssetCount() const noexcept { return static_cast<uint32_t>(m_Assets.size()); }

    private:
        LODSystem() = default;

        std::unordered_map<uint64_t, std::shared_ptr<LODAsset>> m_Assets;
        std::unordered_map<uint64_t, CustomSelector> m_CustomSelectors;
        mutable std::mutex m_Mutex;

        HLODConfig m_HLODConfig;

        /** 内部：默认距离筛选 */
        uint32_t SelectByScreenSize(const LODAsset& asset,
                                    float screenSize) const noexcept;

        /** 内部：计算屏幕空间占比对应的实际距离 */
        static float ScreenSizeToDistance(float screenSize, float objectRadius,
                                          float fovRadians, float screenHeight);
    };

    // ============================================================
    // LOD 批处理工具 — 批量选取所有物体的 LOD
    // ============================================================
    struct LODBatchQuery {
        StringID assetID;
        float    screenSize;
        float    distance;
        LODSelectionResult result;
    };

    /** 批量选择 LOD（线程安全，可并行） */
    inline void SelectLODBatch(std::vector<LODBatchQuery>& queries) {
        for (auto& q : queries) {
            q.result = LODSystem::Get().SelectLOD(q.assetID, q.screenSize, q.distance);
        }
    }

} // namespace Rendering
} // namespace Engine