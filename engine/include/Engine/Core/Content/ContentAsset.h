#pragma once

/**
 * @file ContentAsset.h
 * @brief Content Pipeline v1 — 资产身份与注册表（Ring8/Ring9）
 *
 * 身份模型（Phase A 契约）：
 *   场景文件只保存 AssetGUID；路径是注册表的解析信息。
 *   ⇒ 移动/重命名资产只需更新注册表，场景文件不变。
 *
 * v1 范围红线：
 *   仅两种资产类型（Texture / Script）；无内容寻址/版本控制/VFS/cooking。
 *
 * 导入契约（Phase E 显式化）：
 *   - Import(同一路径)  → 幂等：返回既有 GUID
 *   - RegisterExplicit  → GUID 已被【不同路径】占用 = 拒绝（重复检测）
 *   - Resolve(未知GUID) → 空路径 = 明确的"缺失"，调用方按契约处理（非崩溃）
 *
 * 持久化：清单 JSON（guid/path/type），跨会话稳定 —— Ring9 验收点。
 */

#include "Engine/Core/Resources/ResourceGUID.h"
#include "Engine/Core/Resources/ResourceGUID.h"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Engine::Content {

    enum class AssetType : uint8_t { Texture = 0, Script };

    inline const char* ToString(AssetType t) {
        return t == AssetType::Texture ? "Texture" : "Script";
    }

    struct AssetEntry {
        ResourceGUID guid;
        std::string  path;
        AssetType    type = AssetType::Texture;
    };

    class ContentRegistry {
    public:
        // ── Phase A: 导入（幂等）──
        /// 路径已存在 → 返回既有 GUID；否则生成新 GUID 并登记
        ResourceGUID Import(const std::string& path, AssetType type);

        /// 显式登记指定 GUID（迁移/修复场景）。GUID 冲突（同 GUID 不同路径）→ false
        bool RegisterExplicit(const ResourceGUID& guid,
                              const std::string& path, AssetType type);

        // ── 解析 ──
        /// 未知 GUID → 返回空串（= 缺失，非崩溃）
        std::string ResolvePath(const ResourceGUID& guid) const;
        AssetType   TypeOf(const ResourceGUID& guid) const;
        bool        ContainsGuid(const ResourceGUID& guid) const;
        bool        ContainsPath(const std::string& path) const;

        void Unregister(const ResourceGUID& guid);
        void Clear() { m_ByGuid.clear(); m_ByPath.clear(); }
        size_t Count() const { return m_ByGuid.size(); }

        /// 遍历所有资产条目（Asset Browser 用）
        std::vector<AssetEntry> GetAllEntries() const {
            std::vector<AssetEntry> result;
            result.reserve(m_ByGuid.size());
            for (const auto& [hex, e] : m_ByGuid) result.push_back(e);
            return result;
        }

        // ── Phase B: 清单持久化（跨会话身份稳定）──
        bool SaveManifest(const std::string& filePath) const;
        bool LoadManifest(const std::string& filePath);   // 损坏 → false + 保持空表

    private:
        std::unordered_map<std::string, AssetEntry> m_ByGuid;   // key = guid.ToHex()
        std::unordered_map<std::string, ResourceGUID> m_ByPath; // key = path
    };

} // namespace Engine::Content
