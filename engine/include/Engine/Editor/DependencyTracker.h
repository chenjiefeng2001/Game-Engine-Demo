#pragma once

#include "Engine/Types.h"
#include "Engine/Editor/AssetTypes.h"
#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <functional>
#include <mutex>

namespace Engine {

    class DependencyTracker {
    public:
        DependencyTracker() = default;
        ~DependencyTracker() = default;

        void AddDependency(const GUID& assetGUID, const GUID& dependencyGUID, bool isDirect = true);
        void RemoveAllDependencies(const GUID& assetGUID);
        void Clear();

        std::vector<GUID> GetDirectDependencies(const GUID& assetGUID) const;
        std::vector<GUID> GetAllDependencies(const GUID& assetGUID) const;
        std::vector<GUID> GetDirectUsers(const GUID& assetGUID) const;
        std::vector<GUID> GetAllUsers(const GUID& assetGUID) const;
        bool IsUsed(const GUID& assetGUID) const;
        bool DependsOn(const GUID& assetGUID, const GUID& dependencyGUID) const;

        struct DeleteCheckResult {
            bool canDelete = true;
            std::vector<std::string> blockingAssets;
            std::vector<std::string> warnings;
        };
        DeleteCheckResult CheckBeforeDelete(const GUID& assetGUID) const;

        // 使用 string 序列化，避免在 header 中引入 nlohmann/json
        std::string SerializeToJson() const;
        bool DeserializeFromJson(const std::string& jsonStr);

        using DependencyChangedCallback = std::function<void(const GUID& assetGUID)>;
        void SetDependencyChangedCallback(DependencyChangedCallback cb) {
            m_OnDependencyChanged = std::move(cb);
        }

    private:
        std::unordered_map<GUID, std::vector<GUID>> m_Dependencies;
        std::unordered_map<GUID, std::vector<GUID>> m_ReverseDependencies;
        mutable std::mutex m_Mutex;
        DependencyChangedCallback m_OnDependencyChanged;

        void CollectAllDependencies(const GUID& guid, std::set<GUID>& visited, std::vector<GUID>& result) const;
        void CollectAllUsers(const GUID& guid, std::set<GUID>& visited, std::vector<GUID>& result) const;
    };

} // namespace Engine