#pragma once

/**
 * @file Config.h
 * @brief 引擎配置系统 — 支持全局配置、用户配置、不可变默认模板
 *
 * 设计要点：
 *   - 基于 JSON 存储 (nlohmann/json)
 *   - 每个配置项以 "section.key" 形式组织
 *   - 支持 int32 / float32 / bool / std::string 四种值类型
 *   - SetDefaults() 注册一个只读默认模板，RestoreDefaults() 从中恢复
 *   - Load() / Save() 读写 JSON 文件
 */

#include "Engine/Types.h"
#include <string>
#include <unordered_map>
#include <functional>

// 前向声明 nlohmann::json
#include <nlohmann/json.hpp>

namespace Engine {

    // ============================================================
    // 配置值类型枚举
    // ============================================================
    enum class ConfigType : uint8 {
        Integer = 0,
        Float   = 1,
        Bool    = 2,
        String  = 3
    };

    // ============================================================
    // 配置条目
    // ============================================================
    struct ConfigEntry {
        std::string value;        // 统一以字符串形式存储
        ConfigType  type = ConfigType::String;
    };

    // ============================================================
    // 配置类
    // ============================================================
    class Config {
    public:
        Config() = default;
        virtual ~Config() = default;

        // 禁止拷贝（但允许移动）
        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;
        Config(Config&&) noexcept = default;
        Config& operator=(Config&&) noexcept = default;

        // ── 持久化 ──

        /** 从 JSON 文件加载配置（覆盖当前值） */
        virtual bool Load(const std::string& filepath);

        /** 保存当前配置到 JSON 文件 */
        virtual bool Save(const std::string& filepath) const;

        // ── 值访问 ──

        /** 获取整数值 */
        int32   GetInt(const std::string& section, const std::string& key, int32 defaultValue = 0) const;
        /** 获取浮点值 */
        float32 GetFloat(const std::string& section, const std::string& key, float32 defaultValue = 0.0f) const;
        /** 获取布尔值 */
        bool    GetBool(const std::string& section, const std::string& key, bool defaultValue = false) const;
        /** 获取字符串值 */
        std::string GetString(const std::string& section, const std::string& key, const std::string& defaultValue = "") const;

        /** 设置整数值 */
        void SetInt(const std::string& section, const std::string& key, int32 value);
        /** 设置浮点值 */
        void SetFloat(const std::string& section, const std::string& key, float32 value);
        /** 设置布尔值 */
        void SetBool(const std::string& section, const std::string& key, bool value);
        /** 设置字符串值 */
        void SetString(const std::string& section, const std::string& key, const std::string& value);

        // ── 默认模板管理 ──

        /**
         * @brief 注册不可变的默认模板
         * @param defaults 默认配置对象（其内容将被复制到此对象中）
         *
         * 调用后，此对象拥有一份 defaults 的副本作为只读基准。
         * defaults 对象后续可被销毁，不影响此对象。
         */
        void SetDefaults(const Config& defaults);

        /**
         * @brief 从默认模板恢复所有配置
         *
         * 将所有当前值覆盖为默认模板中的值。
         * 如果某个键在默认模板中不存在，则保留当前值不变。
         */
        void RestoreDefaults();

        /** 是否有注册的默认模板 */
        bool HasDefaults() const { return m_HasDefaults; }

        /** 获取当前值与默认模板的差异（仅返回被修改过的键） */
        Config GetDiff() const;

        // ── 查询 ──

        /** 检查某个键是否存在 */
        bool HasKey(const std::string& section, const std::string& key) const;

        /** 检查某个节是否存在 */
        bool HasSection(const std::string& section) const;

        /** 获取所有节的名称 */
        std::vector<std::string> GetSectionNames() const;

        /** 获取某个节中所有键的名称 */
        std::vector<std::string> GetKeyNames(const std::string& section) const;

        /** 清空所有配置（默认模板不受影响） */
        void Clear();

        /** 获取内部 JSON 数据（只读，用于调试/序列化扩展） */
        const nlohmann::json& GetRaw() const { return m_Data; }

    private:
        // 内部 JSON 存储
        nlohmann::json m_Data = nlohmann::json::object();

        // 默认模板的副本（只读基准）
        nlohmann::json m_Defaults = nlohmann::json::object();
        bool m_HasDefaults = false;

        // 辅助：确保节存在
        nlohmann::json& EnsureSection(const std::string& section);
    };

    // ============================================================
    // 便利辅助：类型擦除的 Get/Set 模板（使用方式一致）
    // ============================================================

    template<typename T>
    inline T ConfigGet(const Config& cfg, const std::string& section,
                       const std::string& key, const T& defaultValue = T()) {
        static_assert(sizeof(T) == 0, "Unsupported type for ConfigGet");
        return defaultValue;
    }

    template<>
    inline int32 ConfigGet<int32>(const Config& cfg, const std::string& section,
                                  const std::string& key, const int32& defaultValue) {
        return cfg.GetInt(section, key, defaultValue);
    }

    template<>
    inline float32 ConfigGet<float32>(const Config& cfg, const std::string& section,
                                      const std::string& key, const float32& defaultValue) {
        return cfg.GetFloat(section, key, defaultValue);
    }

    template<>
    inline bool ConfigGet<bool>(const Config& cfg, const std::string& section,
                                const std::string& key, const bool& defaultValue) {
        return cfg.GetBool(section, key, defaultValue);
    }

    template<>
    inline std::string ConfigGet<std::string>(const Config& cfg, const std::string& section,
                                               const std::string& key, const std::string& defaultValue) {
        return cfg.GetString(section, key, defaultValue);
    }

} // namespace Engine
