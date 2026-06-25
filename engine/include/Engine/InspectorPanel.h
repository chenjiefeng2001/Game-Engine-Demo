#pragma once

/**
 * @file InspectorPanel.h
 * @brief 工业级属性检视器面板 — 七大核心维度全支持
 *
 * 架构：
 *   1. Automatically reflect component properties using PropertyAccessor system
 *   2. Rich field components via PropertyDrawer (expression, range, color, etc.)
 *   3. Prefab override visualization with blue markers
 *   4. Multi-selection editing with mixed value display
 *   5. Custom editor registration (PropertyDrawer per component/class)
 *   6. Search & filter within Inspector
 *   7. Debug mode toggle (show private/hidden properties)
 *   8. Component context menu (copy/paste, remove, move up/down)
 *   9. Lock Inspector to current object
 *  10. Undo/Redo integration via snapshot callbacks
 */

#include "Engine/Types.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Editor/PropertyAttributes.h"
#include "Engine/Editor/PropertyDrawer.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <set>

namespace Engine {

    class GameObject;

    // ============================================================
    // 组件绘制器 — 扩展自旧版 ComponentDrawFn, 加入完整上下文
    // ============================================================
    struct ComponentDrawer {
        std::string displayName;
        std::string category;          ///< 分组类别
        int         orderInInspector = 0; ///< 排序权重
        bool        builtin = false;

        // 绘制回调: (GameObject*, const DrawContext&) → void
        std::function<void(GameObject*, const DrawContext&)> drawFn;

        // 提供属性的访问器列表（用于自动化反射 UI）
        std::vector<PropertyAccessor> properties;
    };

    // ============================================================
    // 搜索过滤器状态
    // ============================================================
    struct InspectorFilter {
        std::string searchText;
        bool        matchCase = false;
        bool        showDisabled = true;
        bool        showHidden = false;  ///< Debug 模式下显示隐藏属性
    };

    // ============================================================
    // 检视器面板
    // ============================================================
    class InspectorPanel {
    public:
        InspectorPanel();
        ~InspectorPanel() = default;

        InspectorPanel(const InspectorPanel&) = delete;
        InspectorPanel& operator=(const InspectorPanel&) = delete;

        // ── 目标管理 ──
        void SetTarget(GameObject* target);
        GameObject* GetTarget() const { return m_Target; }

        /** 多选设置（多个对象同时编辑） */
        void SetMultiTarget(const std::vector<GameObject*>& targets);
        void ClearMultiTarget();
        bool IsMultiSelection() const { return m_MultiTargets.size() > 1; }

        // ── 锁定 ──
        void SetLocked(bool locked) { m_Locked = locked; }
        bool IsLocked() const { return m_Locked; }
        void ToggleLocked() { m_Locked = !m_Locked; }

        // ── Debug 模式 ──
        void SetDebugMode(bool debug) { m_DebugMode = debug; }
        bool IsDebugMode() const { return m_DebugMode; }
        void ToggleDebugMode() { m_DebugMode = !m_DebugMode; }

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 渲染 ──
        void OnImGui();

        // ── 组件绘制器注册 ──
        using ComponentDrawFn = std::function<void(GameObject*)>;

        template<typename T>
        void RegisterDrawer(ComponentDrawFn drawFn) {
            RegisterDrawerByType(typeid(T).hash_code(),
                ComponentDrawer{
                    Component::GetTypeName<T>(),
                    "Default",
                    100,
                    false,
                    [drawFn](GameObject* obj, const DrawContext&) { drawFn(obj); },
                    {}  // no auto properties
                });
        }

        template<typename T>
        void RegisterDrawer(const ComponentDrawer& drawer) {
            RegisterDrawerByType(typeid(T).hash_code(), drawer);
        }

        void RegisterDrawerByType(size_t typeId, ComponentDrawer drawer);
        void UnregisterDrawerByType(size_t typeId);

        // ── 结果回调（外部监听修改事件） ──
        using ModifyCallback = std::function<void(GameObject*, const std::string& propertyPath)>;
        void SetModifyCallback(ModifyCallback cb) { m_ModifyCallback = std::move(cb); }

        // ── 撤销回调 ──
        using UndoCallback = std::function<void(GameObject*)>;
        void SetUndoCallback(UndoCallback cb) { m_UndoCallback = std::move(cb); }

        // ── 外部依赖注入 ──
        void SetSearchFilter(const std::string& filter) { m_Filter.searchText = filter; }
        const InspectorFilter& GetFilter() const { return m_Filter; }

    private:
        // ── 内部绘制函数 ──
        void DrawToolbar();
        void DrawHeader(GameObject* obj);
        void DrawTransformComponent(GameObject* obj, const DrawContext& ctx);
        void DrawComponentSection(GameObject* obj, Component& comp, const DrawContext& ctx);
        void DrawAddComponentMenu();
        void DrawDebugInfo(GameObject* obj);

        // ── 注册内置组件绘制器 ──
        void RegisterBuiltins();

        // ── 工具函数 ──
        DrawContext MakeDrawContext(GameObject* obj);
        bool PassesFilter(const std::string& name) const;
        void OnPropertyModified(GameObject* obj, const char* propertyName);

        // ── 数据 ──
        GameObject* m_Target = nullptr;
        std::vector<GameObject*> m_MultiTargets;
        bool m_Visible = true;
        bool m_Locked = false;
        bool m_DebugMode = false;
        bool m_BuiltinsRegistered = false;

        // 组件绘制器注册表
        std::unordered_map<size_t, ComponentDrawer> m_DrawerRegistry;

        // 组件排序列表（按 orderInInspector 排序）
        std::vector<ComponentDrawer*> m_SortedDrawers;

        // 搜索/过滤
        InspectorFilter m_Filter;

        // 回调
        ModifyCallback m_ModifyCallback;
        UndoCallback m_UndoCallback;

        // 添加组件菜单状态
        char m_AddComponentSearch[128] = {};
        bool m_ShowAddComponentMenu = false;
    };

} // namespace Engine