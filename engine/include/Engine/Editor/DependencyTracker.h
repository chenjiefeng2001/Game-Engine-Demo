#pragma once

/**
 * @file DependencyTracker.h
 * @brief 依赖关系追踪器 — 可视化资产之间的引用关系
 *
 * 功能：
 *   1. 追踪每个资产的"Depends On"和"Used By"关系
 *   2. 在删除资产前检查是否有其他资产引用
 *   3. 提供可视化依赖图数据
 *   4. 自动标记 Dirty 资产
 */

#include "Engine/Types.h"
#include "Engine/Editor/AssetTypes.h"
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <functional>
#include <mutex>

// 前向声明 nlohmann::json（避免在 header 中包含巨大 json.hpp）
namespace nlohmann { class json; }

namespace Engine {

    class DependencyTracker {
    public:
        DependencyTracker() = default;
        ~DependencyTracker() = default;

        // ── 依赖注册 ──
        /** 声明 assetGUID 依赖于 dependencyGUID */
        void AddDependency(const GUID& assetGUID, const GUID& dependencyGUID, bool isDirect = true);

        /** 移除指定资产的所有依赖记录 */
        void RemoveAllDependencies(const GUID& assetGUID);

        /** 清除所有依赖数据 */
        void Clear();

        // ── 依赖查询 ──
        /** 获取某资产直接依赖的所有资产 */
        std::vector<GUID> GetDirectDependencies(const GUID& assetGUID) const;

        /** 获取某资产的所有依赖（包括间接依赖） */
        std::vector<GUID> GetAllDependencies(const GUID& assetGUID) const;

        /** 获取直接引用此资产的所有资产 */
        std::vector<GUID> GetDirectUsers(const GUID& assetGUID) const;

        /** 获取间接引用此资产的所有资产 */
        std::vector<GUID> GetAllUsers(const GUID& assetGUID) const;

        /** 检查是否有任何资产引用了此资产 */
        bool IsUsed(const GUID& assetGUID) const;

        /** 检查资产A是否直接或间接依赖资产B */
        bool DependsOn(const GUID& assetGUID, const GUID& dependencyGUID) const;

        // ── 删除检查 ──
        /** 尝试删除资产前检查所有冲突 */
        struct DeleteCheckResult {
            bool canDelete = true;
            std::vector<std::string> blockingAssets;  ///< 阻止删除的资产列表（引用此资产的）
            std::vector<std::string> warnings;         ///< 警告信息
        };
        DeleteCheckResult CheckBeforeDelete(const GUID& assetGUID) const;

        // ── 序列化 ──
        /** 保存依赖数据到 JSON */
        nlohmann::json Serialize() const;

        /** 从 JSON 加载依赖数据 */
        void Deserialize(const nlohmann::json& j);

        // ── 回调 ──
        using DependencyChangedCallback = std::function<void(const GUID& assetGUID)>;
        void SetDependencyChangedCallback(DependencyChangedCallback cb) {
            m_OnDependencyChanged = std::move(cb);
        }

    private:
        // 依赖图：资产GUID → 它依赖的资产列表
        std::unordered_map<GUID, std::vector<GUID>> m_Dependencies;

        // 反向依赖图：资产GUID → 依赖它的资产列表
        std::unordered_map<GUID, std::vector<GUID>> m_ReverseDependencies;

        mutable std::mutex m_Mutex;
        DependencyChangedCallback m_OnDependencyChanged;

        // 递归查询所有依赖（去重）
        void CollectAllDependencies(const GUID& guid,
                                    std::set<GUID>& visited,
                                    std::vector<GUID>& result) const;
        void CollectAllUsers(const GUID& guid,
                             std::set<GUID>& visited,
                             std::vector<GUID>& result) const;
    };

} // namespace Engine