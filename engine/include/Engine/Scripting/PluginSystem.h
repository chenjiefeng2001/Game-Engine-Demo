#pragma once

/**
 * @file PluginSystem.h
 * @brief 插件系统 — 菜单注入 + 生命周期钩子 + 包管理
 *
 * 架构设计（参考 Unity UI Toolkit + Unreal Plugin System）：
 *
 *   1. 插件管理：加载/卸载/启用/禁用插件
 *   2. 菜单注入：通过 [MenuItem] 宏在编辑器中注册菜单项
 *   3. 属性绘制器：自定义数据类型在 Inspector 中的显示
 *   4. 生命周期钩子：OnEditorUpdate / OnSceneOpen / OnBeforeBuild
 *   5. 包管理系统：版本管理 + 依赖检查
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <set>
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace Engine { namespace Plugin {

    // ============================================================
    // 生命周期钩子类型
    // ============================================================
    enum class EditorHook : uint8_t {
        OnInit,             ///< 编辑器初始化完成后
        OnUpdate,           ///< 每帧更新
        OnSceneOpen,        ///< 场景打开时
        OnSceneSave,        ///< 场景保存时
        OnBeforeBuild,      ///< 构建前预处理
        OnShutdown,         ///< 编辑器关闭时
        OnSelectionChanged, ///< 选中对象变化时
        OnPlayModeChanged,  ///< 播放模式切换时
        COUNT
    };

    inline const char* EditorHookName(EditorHook hook) {
        switch (hook) {
            case EditorHook::OnInit:             return "OnInit";
            case EditorHook::OnUpdate:           return "OnUpdate";
            case EditorHook::OnSceneOpen:        return "OnSceneOpen";
            case EditorHook::OnSceneSave:        return "OnSceneSave";
            case EditorHook::OnBeforeBuild:      return "OnBeforeBuild";
            case EditorHook::OnShutdown:         return "OnShutdown";
            case EditorHook::OnSelectionChanged: return "OnSelectionChanged";
            case EditorHook::OnPlayModeChanged:  return "OnPlayModeChanged";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 菜单项描述
    // ============================================================
    struct MenuItem {
        std::string path;        ///< 菜单路径，如 "Tools/MyTool"
        std::string shortcut;    ///< 快捷键，如 "Ctrl+Shift+T"
        int         priority;    ///< 排序优先级
        std::function<void()> action;
        std::string pluginName;  ///< 所属插件
    };

    // ============================================================
    // 属性绘制器描述
    // ============================================================
    struct PropertyDrawerInfo {
        std::string           typeName;
        int                   priority = 100;
        std::function<bool(void* data, void* object)> drawFn; // return true if modified
        std::string           pluginName;
    };

    // ============================================================
    // 生命周期钩子回调
    // ============================================================
    struct EditorHookCallbacks {
        std::vector<std::function<void()>> listeners;
    };

    // ============================================================
    // 插件元数据
    // ============================================================
    struct PluginMeta {
        std::string name;
        std::string version;
        std::string description;
        std::string author;
        std::string website;
        std::vector<std::string> dependencies; ///< 依赖的其他插件名
        bool isBuiltin = false;
    };

    // ============================================================
    // 插件实例
    // ============================================================
    class Plugin {
    public:
        virtual ~Plugin() = default;
        Plugin(const Plugin&) = delete;
        Plugin& operator=(const Plugin&) = delete;

        virtual PluginMeta GetMeta() const = 0;
        virtual bool OnLoad() = 0;
        virtual void OnUnload() = 0;
        virtual void OnUpdate(float dt) { (void)dt; }

        const std::vector<MenuItem>& GetMenuItems() const { return m_MenuItems; }
        const std::vector<PropertyDrawerInfo>& GetPropertyDrawers() const { return m_PropertyDrawers; }

    protected:
        friend class PluginManager;

        /** 注册一个菜单项 */
        void RegisterMenuItem(const std::string& path,
                              std::function<void()> action,
                              const std::string& shortcut = "",
                              int priority = 500) {
            m_MenuItems.push_back({path, shortcut, priority, std::move(action), GetMeta().name});
        }

        /** 注册一个属性绘制器 */
        void RegisterPropertyDrawer(const std::string& typeName,
                                     std::function<bool(void*, void*)> drawFn,
                                     int priority = 100) {
            m_PropertyDrawers.push_back({typeName, priority, std::move(drawFn), GetMeta().name});
        }

        std::vector<MenuItem> m_MenuItems;
        std::vector<PropertyDrawerInfo> m_PropertyDrawers;
    };

    // ============================================================
    // 插件管理器（单例）
    // ============================================================
    class PluginManager {
    public:
        static PluginManager& Get();

        PluginManager(const PluginManager&) = delete;
        PluginManager& operator=(const PluginManager&) = delete;

        // ── 插件生命周期 ──
        /** 初始化插件系统 */
        bool Init();

        /** 关机，卸载所有插件 */
        void Shutdown();

        /** 从目录加载所有插件 */
        void LoadPluginsFromDirectory(const std::string& dir);

        /** 加载单个插件 */
        bool LoadPlugin(std::unique_ptr<Plugin> plugin);

        /** 卸载指定插件 */
        bool UnloadPlugin(const std::string& name);

        /** 获取已加载的插件列表 */
        std::vector<PluginMeta> GetLoadedPlugins() const;

        // ── 菜单系统 ──
        /** 注册一个菜单项 */
        void RegisterMenuItem(const MenuItem& item);

        /** 获取所有已注册的菜单项 */
        const std::vector<MenuItem>& GetAllMenuItems() const { return m_MenuItems; }

        /** 按路径获取菜单项 */
        std::vector<MenuItem> GetMenuItemsByPath(const std::string& pathPrefix) const;

        // ── 属性绘制器 ──
        /** 注册一个自定义属性绘制器 */
        void RegisterPropertyDrawer(const PropertyDrawerInfo& drawer);

        /** 获取匹配的绘制器 */
        const PropertyDrawerInfo* FindPropertyDrawer(const std::string& typeName) const;

        // ── 生命周期钩子 ──
        /** 注册一个生命周期钩子回调 */
        void AddHook(EditorHook hook, std::function<void()> callback);

        /** 触发生命周期钩子 */
        void TriggerHook(EditorHook hook);

        // ── 包管理 ──
        /** 检查依赖是否满足 */
        bool CheckDependencies(const std::vector<std::string>& deps) const;

        /** 获取有冲突的依赖 */
        std::vector<std::string> GetMissingDependencies(
            const std::vector<std::string>& deps) const;

    private:
        PluginManager() = default;
        ~PluginManager() = default;

        std::vector<std::unique_ptr<Plugin>> m_Plugins;
        std::vector<MenuItem> m_MenuItems;
        std::unordered_map<std::string, PropertyDrawerInfo> m_PropertyDrawers;
        std::unordered_map<int, EditorHookCallbacks> m_Hooks;
        mutable std::mutex m_Mutex;
    };

}} // namespace Engine::Plugin

// ============================================================
// 便捷宏 — 在插件实现中使用
// ============================================================
#define ENGINE_MENU_ITEM(path, action) \
    RegisterMenuItem(path, action)

#define ENGINE_PROPERTY_DRAWER(type, drawFn) \
    RegisterPropertyDrawer(#type, drawFn)