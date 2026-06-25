#include "Engine/Editor/DependencyTracker.h"
#include "Engine/Core/Log.h"
#include <nlohmann/json.hpp>
#include <queue>
#include <algorithm>

namespace Engine {

    void DependencyTracker::AddDependency(const GUID& assetGUID, const GUID& dependencyGUID, bool isDirect) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 避免自依赖
        if (assetGUID == dependencyGUID) return;

        // 添加到正向依赖
        auto& deps = m_Dependencies[assetGUID];
        if (std::find(deps.begin(), deps.end(), dependencyGUID) == deps.end()) {
            deps.push_back(dependencyGUID);
        }

        // 添加到反向依赖
        auto& revDeps = m_ReverseDependencies[dependencyGUID];
        if (std::find(revDeps.begin(), revDeps.end(), assetGUID) == revDeps.end()) {
            revDeps.push_back(assetGUID);
        }

        if (m_OnDependencyChanged) {
            m_OnDependencyChanged(assetGUID);
        }
    }

    void DependencyTracker::RemoveAllDependencies(const GUID& assetGUID) {
        std::lock_guard<std::mutex> lock(m_Mutex);

        // 从正向依赖中移除
        auto it = m_Dependencies.find(assetGUID);
        if (it != m_Dependencies.end()) {
            // 从每个被依赖资产的反向依赖列表中移除自己
            for (const auto& depGUID : it->second) {
                auto revIt = m_ReverseDependencies.find(depGUID);
                if (revIt != m_ReverseDependencies.end()) {
                    auto& revList = revIt->second;
                    revList.erase(std::remove(revList.begin(), revList.end(), assetGUID), revList.end());
                    if (revList.empty()) {
                        m_ReverseDependencies.erase(revIt);
                    }
                }
            }
            m_Dependencies.erase(it);
        }

        // 从反向依赖中移除
        auto revIt = m_ReverseDependencies.find(assetGUID);
        if (revIt != m_ReverseDependencies.end()) {
            for (const auto& userGUID : revIt->second) {
                auto depIt = m_Dependencies.find(userGUID);
                if (depIt != m_Dependencies.end()) {
                    auto& depList = depIt->second;
                    depList.erase(std::remove(depList.begin(), depList.end(), assetGUID), depList.end());
                    if (depList.empty()) {
                        m_Dependencies.erase(depIt);
                    }
                }
            }
            m_ReverseDependencies.erase(revIt);
        }

        if (m_OnDependencyChanged) {
            m_OnDependencyChanged(assetGUID);
        }
    }

    void DependencyTracker::Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Dependencies.clear();
        m_ReverseDependencies.clear();
    }

    std::vector<GUID> DependencyTracker::GetDirectDependencies(const GUID& assetGUID) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_Dependencies.find(assetGUID);
        if (it != m_Dependencies.end()) return it->second;
        return {};
    }

    std::vector<GUID> DependencyTracker::GetAllDependencies(const GUID& assetGUID) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::set<GUID> visited;
        std::vector<GUID> result;
        CollectAllDependencies(assetGUID, visited, result);
        return result;
    }

    std::vector<GUID> DependencyTracker::GetDirectUsers(const GUID& assetGUID) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_ReverseDependencies.find(assetGUID);
        if (it != m_ReverseDependencies.end()) return it->second;
        return {};
    }

    std::vector<GUID> DependencyTracker::GetAllUsers(const GUID& assetGUID) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        std::set<GUID> visited;
        std::vector<GUID> result;
        CollectAllUsers(assetGUID, visited, result);
        return result;
    }

    bool DependencyTracker::IsUsed(const GUID& assetGUID) const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto it = m_ReverseDependencies.find(assetGUID);
        return it != m_ReverseDependencies.end() && !it->second.empty();
    }

    bool DependencyTracker::DependsOn(const GUID& assetGUID, const GUID& dependencyGUID) const {
        auto deps = GetAllDependencies(assetGUID);
        return std::find(deps.begin(), deps.end(), dependencyGUID) != deps.end();
    }

    void DependencyTracker::CollectAllDependencies(const GUID& guid,
                                                     std::set<GUID>& visited,
                                                     std::vector<GUID>& result) const {
        if (visited.find(guid) != visited.end()) return;
        visited.insert(guid);

        auto it = m_Dependencies.find(guid);
        if (it == m_Dependencies.end()) return;

        for (const auto& dep : it->second) {
            if (visited.find(dep) == visited.end()) {
                result.push_back(dep);
                CollectAllDependencies(dep, visited, result);
            }
        }
    }

    void DependencyTracker::CollectAllUsers(const GUID& guid,
                                              std::set<GUID>& visited,
                                              std::vector<GUID>& result) const {
        if (visited.find(guid) != visited.end()) return;
        visited.insert(guid);

        auto it = m_ReverseDependencies.find(guid);
        if (it == m_ReverseDependencies.end()) return;

        for (const auto& user : it->second) {
            if (visited.find(user) == visited.end()) {
                result.push_back(user);
                CollectAllUsers(user, visited, result);
            }
        }
    }

    DependencyTracker::DeleteCheckResult DependencyTracker::CheckBeforeDelete(const GUID& assetGUID) const {
        DeleteCheckResult result;

        auto users = GetDirectUsers(assetGUID);
        if (!users.empty()) {
            result.canDelete = false;
            for (const auto& userGUID : users) {
                // 在实际实现中，应通过 AssetDatabase 获取资产名称
                result.blockingAssets.push_back("Asset (referenced)");
            }
        }

        return result;
    }

    nlohmann::json DependencyTracker::Serialize() const {
        std::lock_guard<std::mutex> lock(m_Mutex);
        nlohmann::json j = nlohmann::json::object();

        for (const auto& [guid, deps] : m_Dependencies) {
            nlohmann::json depArray = nlohmann::json::array();
            for (const auto& dep : deps) {
                depArray.push_back(dep.ToString());
            }
            j[guid.ToString()] = depArray;
        }

        return j;
    }

    void DependencyTracker::Deserialize(const nlohmann::json& j) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Dependencies.clear();
        m_ReverseDependencies.clear();

        for (auto it = j.begin(); it != j.end(); ++it) {
            GUID guid = GUID::FromString(it.key());
            for (const auto& depStr : it.value()) {
                GUID depGUID = GUID::FromString(depStr);
                AddDependency(guid, depGUID, true);
            }
        }
    }

} // namespace Engine