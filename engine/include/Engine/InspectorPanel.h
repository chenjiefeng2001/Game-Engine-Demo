#pragma once

/**
 * @file InspectorPanel.h
 * @brief 属性检视器面板 — 自动化组件 UI 生成
 *
 * 核心机制：
 *   1. GameObject 挂载的每个 Component 通过 ForEachComponent 遍历
 *   2. 使用 type_index → DrawFn 注册表自动映射到对应的 UI 绘制器
 *   3. 内置 Transform、Sprite、Physics 三组件的绘制器
 *   4. 通过 RegisterDrawer<T>() 注册任意自定义组件的 UI
 *
 * 使用方式：
 * @code
 *   InspectorPanel inspector;
 *   inspector.SetTarget(selectedObject);
 *   inspector.OnImGui();
 *
 *   // 注册自定义组件 UI（模板方式）
 *   inspector.RegisterDrawer<AudioComponent>([](GameObject* obj) {
 *       auto* ac = obj->GetComponent<AudioComponent>();
 *       if (ac) {
 *           if (ImGui::CollapsingHeader("Audio Source")) {
 *               float vol = ac->GetVolume();
 *               if (ImGui::SliderFloat("Volume", &vol, 0, 1))
 *                   ac->SetVolume(vol);
 *           }
 *       }
 *   });
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/GameObject/Component.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <typeinfo>
#include <memory>

namespace Engine {

    class GameObject;

    class InspectorPanel {
    public:
        InspectorPanel();
        ~InspectorPanel() = default;

        InspectorPanel(const InspectorPanel&) = delete;
        InspectorPanel& operator=(const InspectorPanel&) = delete;

        void SetTarget(GameObject* target) { m_Target = target; }
        GameObject* GetTarget() const { return m_Target; }

        void OnImGui();

        using ComponentDrawFn = std::function<void(GameObject* obj)>;

        struct ComponentMeta {
            std::string     displayName;
            ComponentDrawFn drawFn;
        };

        template<typename T>
        void RegisterDrawer(ComponentDrawFn drawFn) {
            RegisterDrawerByType(typeid(T).hash_code(),
                ComponentMeta{ GetTypeNameFor<T>(), std::move(drawFn) });
        }

        void RegisterDrawerByType(size_t typeId, ComponentMeta meta);

        template<typename T>
        void UnregisterDrawer() {
            UnregisterDrawerByType(typeid(T).hash_code());
        }

        void UnregisterDrawerByType(size_t typeId);

        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

    private:
        void DrawHeader(GameObject* obj);
        void RegisterBuiltins();

        template<typename T>
        static const char* GetTypeNameFor() {
            return Component::ParseTypeName(typeid(T).name());
        }

        GameObject* m_Target = nullptr;
        bool m_Visible = true;

        std::unordered_map<size_t, ComponentMeta> m_DrawerRegistry;
        bool m_BuiltinsRegistered = false;
    };

} // namespace Engine