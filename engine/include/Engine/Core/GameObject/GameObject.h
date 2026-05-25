#pragma once

#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/TransformComponent.h"
#include "Engine/Core/RHI/IRenderable.h"
#include "Engine/Core/RHI/RenderCommand.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeinfo>

namespace Engine {

    class SpriteComponent;
    class PhysicsComponent;
    class ISpriteBatch;
    class Shader;
    class IRenderQueue;


    class GameObject : public IRenderable {
    public:
        GameObject();
        explicit GameObject(std::string name);
        virtual ~GameObject();
        virtual void OnCreate();

        virtual void Update(float32 dt);

        virtual void Render() {}

        virtual void OnDestroy();

        // ============================================================
        // 动态组件管理（结构化组件模型的核心 API）
        // ============================================================

        /**
         * @brief 添加组件到对象上
         * @tparam T 组件类型（必须继承自 Component）
         * @param args 构造参数
         * @return 组件指针（若已存在同类型组件则返回已存在的）
         *
         * 使用示例：
         * @code
         *   obj->AddComponent<SpriteComponent>(texture);
         *   obj->AddComponent<PhysicsComponent>()->CreateBody(world, def);
         * @endcode
         */
        template<typename T, typename... Args>
        T* AddComponent(Args&&... args) {
            static_assert(std::is_base_of_v<Component, T>,
                          "T must derive from Component");
            const size_t hash = typeid(T).hash_code();

            // 如果已存在同类型组件，返回当前实例
            auto it = m_Components.find(hash);
            if (it != m_Components.end())
                return static_cast<T*>(it->second.get());

            // 创建新组件
            auto ptr = std::make_shared<T>(std::forward<Args>(args)...);
            T* raw = ptr.get();

            // 设置组件所属对象
            raw->m_Owner = this;
            raw->OnCreate();

            m_Components[hash] = std::move(ptr);
            return raw;
        }

        /**
         * @brief 获取已挂载的组件
         * @tparam T 组件类型
         * @return 组件指针，若不存在则返回 nullptr
         */
        template<typename T>
        T* GetComponent() {
            static_assert(std::is_base_of_v<Component, T>,
                          "T must derive from Component");
            auto it = m_Components.find(typeid(T).hash_code());
            if (it != m_Components.end())
                return static_cast<T*>(it->second.get());
            return nullptr;
        }

        template<typename T>
        const T* GetComponent() const {
            static_assert(std::is_base_of_v<Component, T>,
                          "T must derive from Component");
            auto it = m_Components.find(typeid(T).hash_code());
            if (it != m_Components.end())
                return static_cast<const T*>(it->second.get());
            return nullptr;
        }

        /**
         * @brief 移除已挂载的组件
         * @tparam T 组件类型
         * @return 是否成功移除
         */
        template<typename T>
        bool RemoveComponent() {
            static_assert(std::is_base_of_v<Component, T>,
                          "T must derive from Component");
            auto it = m_Components.find(typeid(T).hash_code());
            if (it != m_Components.end()) {
                it->second->OnDestroy();
                m_Components.erase(it);
                return true;
            }
            return false;
        }

        /**
         * @brief 检查是否挂载了指定类型的组件
         */
        template<typename T>
        bool HasComponent() const {
            return GetComponent<T>() != nullptr;
        }

        // ============================================================
        // 便捷方法（向后兼容，旧代码无需改动）
        // ============================================================

        TransformComponent& GetTransform() noexcept { return m_Transform; }
        const TransformComponent& GetTransform() const noexcept { return m_Transform; }

        /**
         * @brief 获取精灵组件（便捷方法，等价于 GetComponent<SpriteComponent>）
         *
         * 若不存在则自动添加并返回默认实例。
         * 此设计确保 ->GetSprite().SetColor(...) 等旧代码无需预先 AddComponent。
         */
        class SpriteComponent& GetSprite();
        const class SpriteComponent* GetSprite() const;
        bool HasSprite() const noexcept;

        /**
         * @brief 获取物理组件（便捷方法，等价于 GetComponent<PhysicsComponent>）
         */
        class PhysicsComponent& GetPhysics();
        const class PhysicsComponent* GetPhysics() const;
        bool HasPhysics() const noexcept;

        // ============================================================
        // 通用属性
        // ============================================================

        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const noexcept { return m_Name; }

        void SetActive(bool active) { m_Active = active; }
        bool IsActive() const noexcept { return m_Active; }
        bool IsActiveInHierarchy() const;

        // ── IRenderable ──
        void CollectRenderCommands(IRenderQueue& queue) override;

        // ── 层级 ──
        void SetParent(GameObject* parent);
        GameObject* GetParent() const noexcept { return m_Parent; }

        void AddChild(std::shared_ptr<GameObject> child);
        bool RemoveChild(GameObject* child);
        GameObject* FindChild(const std::string& name);
        const GameObject* FindChild(const std::string& name) const;
        const std::vector<std::shared_ptr<GameObject>>& GetChildren() const noexcept {
            return m_Children;
        }

        void SubmitSprite(ISpriteBatch& batch);

    protected:
        std::string m_Name;

        TransformComponent  m_Transform;   // 每个 GameObject 都有的内置变换

        bool m_Active = true;

        GameObject* m_Parent = nullptr;
        std::vector<std::shared_ptr<GameObject>> m_Children;

        // 动态组件存储（type_index → Component）
        std::unordered_map<size_t, std::shared_ptr<Component>> m_Components;
    };

} // namespace Engine
