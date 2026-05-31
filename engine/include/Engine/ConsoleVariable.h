#pragma once

/**
 * @file ConsoleVariable.h
 * @brief CVar 系统 — 可通过控制台实时修改的变量
 *
 * 设计原则：
 *   - 类型安全：Bool / Int / Float / String 四种类型
 *   - 注册后可通过控制台命令 `set <name> <value>` 修改
 *   - 支持变更回调（值变化时通知）
 *   - 典型用途：作弊开关（god_mode、noclip）、调试参数（player_speed）
 *
 * 使用方式：
 * @code
 *   // 定义全局 CVar（推荐方式）
 *   static Engine::CVar<bool> g_GodMode("god_mode", "无敌模式", false);
 *
 *   // 在游戏逻辑中读取
 *   // if (g_GodMode) { 无敌模式 }
 *
 *   // 在控制台中修改（通过 ConsoleCommandRegistry 的 set/get 命令）
 *   // > set god_mode true
 *   // > set player_speed 5.0
 * @endcode
 */

#include "Engine/Types.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Engine {

    // ============================================================
    // CVar 类型枚举
    // ============================================================
    enum class CVarType : uint8 {
        Bool    = 0,
        Int     = 1,
        Float   = 2,
        String  = 3,
        COUNT
    };

    /// 获取 CVar 类型的显示名称
    inline const char* CVarTypeName(CVarType type) {
        switch (type) {
            case CVarType::Bool:   return "bool";
            case CVarType::Int:    return "int";
            case CVarType::Float:  return "float";
            case CVarType::String: return "string";
            default:               return "unknown";
        }
    }

    // ── 前向声明 ──
    class CVarRegistry;

    // ============================================================
    // CVar 基类（多态接口）
    // ============================================================
    class CVarBase {
    public:
        CVarBase(std::string name, std::string description, CVarType type)
            : m_Name(std::move(name))
            , m_Description(std::move(description))
            , m_Type(type)
        {}

        virtual ~CVarBase() = default;

        const std::string& GetName()        const { return m_Name; }
        const std::string& GetDescription() const { return m_Description; }
        CVarType           GetType()         const { return m_Type; }

        /// 将当前值转换为字符串
        virtual std::string ToString() const = 0;

        /// 从字符串设置值，返回是否成功
        virtual bool FromString(const std::string& value) = 0;

        /// 注册变更回调
        using ChangeCallback = std::function<void()>;
        void AddCallback(ChangeCallback cb) { m_Callbacks.push_back(std::move(cb)); }

    protected:
        void NotifyChanged() {
            for (auto& cb : m_Callbacks)
                cb();
        }

    private:
        std::string               m_Name;
        std::string               m_Description;
        CVarType                  m_Type;
        std::vector<ChangeCallback> m_Callbacks;
    };

    // ============================================================
    // 类型化 CVar（模板实现）
    // ============================================================
    template<typename T>
    class CVar : public CVarBase {
    public:
        CVar(std::string name, std::string description, T defaultValue);

        ~CVar() override = default;

        // ── 类型转换 ──
        operator T() const { return m_Value; }
        CVar& operator=(const T& val) {
            if (m_Value != val) {
                m_Value = val;
                NotifyChanged();
            }
            return *this;
        }

        const T& Get() const { return m_Value; }
        void Set(const T& val) { *this = val; }

        // ── 基类接口 ──
        std::string ToString() const override { return ToStringImpl(m_Value); }
        bool FromString(const std::string& str) override { return FromStringImpl(str, m_Value); }

    private:
        static CVarType DeduceType();
        static std::string ToStringImpl(const T& val);
        static bool FromStringImpl(const std::string& str, T& out);

        T m_Value;
    };

    // ============================================================
    // 特化实现
    // ============================================================

    template<> inline CVarType CVar<bool>::DeduceType()   { return CVarType::Bool; }
    template<> inline CVarType CVar<int32>::DeduceType()  { return CVarType::Int; }
    template<> inline CVarType CVar<float32>::DeduceType(){ return CVarType::Float; }
    template<> inline CVarType CVar<std::string>::DeduceType() { return CVarType::String; }

    template<> inline std::string CVar<bool>::ToStringImpl(const bool& val) {
        return val ? "true" : "false";
    }
    template<> inline bool CVar<bool>::FromStringImpl(const std::string& str, bool& out) {
        if (str == "true" || str == "1" || str == "yes" || str == "on") {
            out = true; return true;
        }
        if (str == "false" || str == "0" || str == "no" || str == "off") {
            out = false; return true;
        }
        return false;
    }

    template<> inline std::string CVar<int32>::ToStringImpl(const int32& val) {
        return std::to_string(val);
    }
    template<> inline bool CVar<int32>::FromStringImpl(const std::string& str, int32& out) {
        try {
            out = std::stoi(str);
            return true;
        } catch (...) { return false; }
    }

    template<> inline std::string CVar<float32>::ToStringImpl(const float32& val) {
        return std::to_string(val);
    }
    template<> inline bool CVar<float32>::FromStringImpl(const std::string& str, float32& out) {
        try {
            out = std::stof(str);
            return true;
        } catch (...) { return false; }
    }

    template<> inline std::string CVar<std::string>::ToStringImpl(const std::string& val) {
        return val;
    }
    template<> inline bool CVar<std::string>::FromStringImpl(const std::string& str, std::string& out) {
        out = str;
        return true;
    }

    // ============================================================
    // CVar 注册表（Meyer's 单例）
    // ============================================================
    class CVarRegistry {
    public:
        static CVarRegistry& Instance();

        /// 注册一个 CVar（由 CVar 构造函数自动调用）
        void Register(CVarBase* cvar);

        /// 按名称查找 CVar
        CVarBase* Find(const std::string& name);

        /// 获取所有 CVar
        const std::unordered_map<std::string, CVarBase*>& GetAll() const {
            return m_CVars;
        }

        /// 清空所有 CVar
        void Clear();

    private:
        CVarRegistry() = default;
        ~CVarRegistry() = default;
        CVarRegistry(const CVarRegistry&) = delete;
        CVarRegistry& operator=(const CVarRegistry&) = delete;

        std::unordered_map<std::string, CVarBase*> m_CVars;
    };

    // ============================================================
    // CVar 模板构造函数（在 CVarRegistry 完整声明后定义）
    // ============================================================
    template<typename T>
    inline CVar<T>::CVar(std::string name, std::string description, T defaultValue)
        : CVarBase(std::move(name), std::move(description), DeduceType())
        , m_Value(defaultValue)
    {
        // 自动注册到全局注册表
        CVarRegistry::Instance().Register(this);
    }

} // namespace Engine
