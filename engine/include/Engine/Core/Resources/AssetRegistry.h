#pragma once

/**
 * @file AssetRegistry.h
 * @brief 资产注册表 — 维护 GUID ↔ 当前文件路径的映射
 *
 * 设计原则：
 *   - 场景文件只记录 GUID，永不记录文件路径
 *   - AssetRegistry 维护映射表，支持文件移动后路径更新
 *   - 序列化为 JSON/二进制（便于版本控制合并）
 */

#include "Engine/Core/GUID.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // AssetRegistry — 全局资产注册表
    // ============================================================
    class AssetRegistry {
    public:
        /** @brief 注册一个资产映射 */
        void Register(const GUID& guid, const std::string& filePath) {
            m_GuidToPath[guid] = filePath;
            m_PathToGuid[filePath] = guid;
        }

        /** @brief 通过 GUID 查询文件路径 */
        std::string ResolvePath(const GUID& guid) const {
            auto it = m_GuidToPath.find(guid);
            return it != m_GuidToPath.end() ? it->second : "";
        }

        /** @brief 通过文件路径查询 GUID */
        GUID ResolveGUID(const std::string& filePath) const {
            auto it = m_PathToGuid.find(filePath);
            return it != m_PathToGuid.end() ? it->second : GUID::Null();
        }

        /** @brief 移除映射 */
        void Unregister(const GUID& guid) {
            auto it = m_GuidToPath.find(guid);
            if (it != m_GuidToPath.end()) {
                m_PathToGuid.erase(it->second);
                m_GuidToPath.erase(it);
            }
        }

        /** @brief 更新文件路径（文件移动后调用） */
        void UpdatePath(const GUID& guid, const std::string& newPath) {
            Unregister(guid);
            Register(guid, newPath);
        }

        /** @brief 获取所有已注册 GUID */
        std::vector<GUID> GetAllGUIDs() const {
            std::vector<GUID> result;
            for (const auto& [guid, _] : m_GuidToPath) {
                result.push_back(guid);
            }
            return result;
        }

        /** @brief 注册表是否为空 */
        bool IsEmpty() const noexcept { return m_GuidToPath.empty(); }

        /** @brief 清空注册表 */
        void Clear() noexcept {
            m_GuidToPath.clear();
            m_PathToGuid.clear();
        }

        /** @brief 全局单例 */
        static AssetRegistry& Get() noexcept {
            static AssetRegistry instance;
            return instance;
        }

    private:
        AssetRegistry() = default;
        ~AssetRegistry() = default;
        AssetRegistry(const AssetRegistry&) = delete;
        AssetRegistry& operator=(const AssetRegistry&) = delete;

        std::unordered_map<GUID, std::string, GUIDHasher> m_GuidToPath;
        std::unordered_map<std::string, GUID> m_PathToGuid;
    };

} // namespace Engine