#include "Engine/Scripting/PluginSystem.h"
#include "Engine/Core/Log.h"
#include <algorithm>

namespace Engine { namespace Plugin {

    namespace {
        Logger s_Log("PluginSystem");
    }

    PluginManager& PluginManager::Get() {
        static PluginManager instance;
        return instance;
    }

    bool PluginManager::Init() {
        s_Log.Info("Plugin system initialized");
        TriggerHook(EditorHook::OnInit);
        return true;
    }

    void PluginManager::Shutdown() {
        TriggerHook(EditorHook::OnShutdown);

        // 反向卸载（最后加载的先卸载）
        for (auto it = m_Plugins.rbegin(); it != m_Plugins.rend(); ++it) {
            (*it)->OnUnload();
            s_Log.Info("Plugin unloaded: {}", (*it)->GetMeta().name);
        }
        m_Plugins.clear();
        m_MenuItems.clear();
        m_PropertyDrawers.clear();
        m_Hooks.clear();

        s_Log.Info("Plugin system shut down");
    }

    void PluginManager::LoadPluginsFromDirectory(const std::string& dir) {
        // 插件从目录加载（通常是共享库 .dll/.so）
        // 当前实现：静态注册的插件通过 LoadPlugin() 手动添加
        (void)dir;
        s_Log.Info("Dynamic plugin loading from directory: {} (not yet implemented)", dir);
    }

    bool PluginManager::LoadPlugin(std::unique_ptr<Plugin> plugin) {
        if (!plugin) {
            s_Log.Error("Cannot load null plugin");
            return false;
        }

        auto meta = plugin->GetMeta();

        // 检查依赖
        auto missing = GetMissingDependencies(meta.dependencies);
        if (!missing.empty()) {
            s_Log.Error("Plugin '{}' missing dependencies:", meta.name);
            for (const auto& dep : missing) {
                s_Log.Error("  - {}", dep);
            }
            return false;
        }

        // 加载插件
        if (!plugin->OnLoad()) {
            s_Log.Error("Failed to load plugin: {}", meta.name);
            return false;
        }

        // 注册插件的菜单项
        for (const auto& item : plugin->GetMenuItems()) {
            RegisterMenuItem(item);
        }

        // 注册插件的属性绘制器
        for (const auto& drawer : plugin->GetPropertyDrawers()) {
            RegisterPropertyDrawer(drawer);
        }

        m_Plugins.push_back(std::move(plugin));
        s_Log.Info("Plugin loaded: {} v{}", meta.name, meta.version);
        return true;
    }

    bool PluginManager::UnloadPlugin(const std::string& name) {
        for (auto it = m_Plugins.begin(); it != m_Plugins.end(); ++it) {
            if ((*it)->GetMeta().name == name) {
                (*it)->OnUnload();

                // 移除该插件的菜单项
                m_MenuItems.erase(
                    std::remove_if(m_MenuItems.begin(), m_MenuItems.end(),
                        [&](const MenuItem& item) { return item.pluginName == name; }),
                    m_MenuItems.end());

                // 移除该插件的属性绘制器
                for (auto dit = m_PropertyDrawers.begin(); dit != m_PropertyDrawers.end();) {
                    if (dit->second.pluginName == name)
                        dit = m_PropertyDrawers.erase(dit);
                    else
                        ++dit;
                }

                m_Plugins.erase(it);
                s_Log.Info("Plugin unloaded: {}", name);
                return true;
            }
        }
        s_Log.Warn("Plugin not found: {}", name);
        return false;
    }

    std::vector<PluginMeta> PluginManager::GetLoadedPlugins() const {
        std::vector<PluginMeta> result;
        for (const auto& p : m_Plugins) {
            result.push_back(p->GetMeta());
        }
        return result;
    }

    void PluginManager::RegisterMenuItem(const MenuItem& item) {
        // 检查是否已存在相同路径的菜单项
        for (auto& existing : m_MenuItems) {
            if (existing.path == item.path) {
                existing = item;  // 替换
                return;
            }
        }
        m_MenuItems.push_back(item);

        // 按优先级排序
        std::sort(m_MenuItems.begin(), m_MenuItems.end(),
                  [](const MenuItem& a, const MenuItem& b) {
                      return a.priority < b.priority;
                  });
    }

    std::vector<MenuItem> PluginManager::GetMenuItemsByPath(
        const std::string& pathPrefix) const {
        std::vector<MenuItem> result;
        for (const auto& item : m_MenuItems) {
            if (item.path.starts_with(pathPrefix)) {
                result.push_back(item);
            }
        }
        return result;
    }

    void PluginManager::RegisterPropertyDrawer(const PropertyDrawerInfo& drawer) {
        m_PropertyDrawers[drawer.typeName] = drawer;
    }

    const PropertyDrawerInfo* PluginManager::FindPropertyDrawer(
        const std::string& typeName) const {
        auto it = m_PropertyDrawers.find(typeName);
        if (it != m_PropertyDrawers.end()) {
            return &it->second;
        }
        return nullptr;
    }

    void PluginManager::AddHook(EditorHook hook, std::function<void()> callback) {
        m_Hooks[static_cast<int>(hook)].listeners.push_back(std::move(callback));
    }

    void PluginManager::TriggerHook(EditorHook hook) {
        auto it = m_Hooks.find(static_cast<int>(hook));
        if (it != m_Hooks.end()) {
            for (const auto& listener : it->second.listeners) {
                if (listener) listener();
            }
        }
    }

    bool PluginManager::CheckDependencies(const std::vector<std::string>& deps) const {
        return GetMissingDependencies(deps).empty();
    }

    std::vector<std::string> PluginManager::GetMissingDependencies(
        const std::vector<std::string>& deps) const {
        std::vector<std::string> missing;

        // 构建已加载插件名集合
        std::set<std::string> loaded;
        for (const auto& p : m_Plugins) {
            loaded.insert(p->GetMeta().name);
        }

        for (const auto& dep : deps) {
            if (loaded.find(dep) == loaded.end()) {
                missing.push_back(dep);
            }
        }

        return missing;
    }

}} // namespace Engine::Plugin