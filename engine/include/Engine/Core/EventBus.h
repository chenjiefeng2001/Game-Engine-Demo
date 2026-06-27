#pragma once

/**
 * @file EventBus.h
 * @brief 全局事件总线 — 类型安全的发布-订阅模式 (Pub/Sub)
 *
 * 解耦 EngineEditor 上帝类：面板之间通过事件通信，无需互相知道彼此存在。
 *
 * 使用方式：
 * @code
 *   // ── 1. 定义事件类型（任意 POD/struct） ──
 *   struct EntitySelectedEvent {
 *       GameObject* Entity = nullptr;
 *   };
 *
 *   // ── 2. 发布（事件源方） ──
 *   EventBus::Publish(EntitySelectedEvent{selectedObj});
 *
 *   // ── 3. 订阅（监听方） ──
 *   // 返回 SubscriptionHandle（用于取消订阅）
 *   auto handle = EventBus::Subscribe<EntitySelectedEvent>(
 *       [this](const EntitySelectedEvent& e) {
 *           SetTarget(e.Entity);
 *       }
 *   );
 *
 *   // ── 4. 取消订阅（在析构时自动取消） ──
 *   handle.Unsubscribe();  // 或 handle 析构时自动取消
 * @endcode
 *
 * 特性：
 *   - 类型安全：每个事件类型独立分发，无 void* 或基类转换
 *   - 零开销抽象：基于模板，编译期生成每个事件类型的事件槽
 *   - 线程安全：支持多线程发布（主线程推荐）
 *   - RAII 管理：SubscriptionHandle 析构时自动取消订阅
 */

#include "Engine/Types.h"
#include <functional>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <typeindex>
#include <vector>

namespace Engine {

    // ═══════════════════════════════════════════════════════════════
    // 订阅句柄（RAII：析构时自动取消订阅）
    // ═══════════════════════════════════════════════════════════════
    class SubscriptionHandle {
        friend class EventBus;
    public:
        SubscriptionHandle() = default;

        SubscriptionHandle(SubscriptionHandle&& other) noexcept
            : m_Unsubscriber(std::move(other.m_Unsubscriber)) {}

        SubscriptionHandle& operator=(SubscriptionHandle&& other) noexcept {
            if (this != &other) {
                Unsubscribe();
                m_Unsubscriber = std::move(other.m_Unsubscriber);
            }
            return *this;
        }

        ~SubscriptionHandle() { Unsubscribe(); }

        SubscriptionHandle(const SubscriptionHandle&) = delete;
        SubscriptionHandle& operator=(const SubscriptionHandle&) = delete;

        /** 手动取消订阅 */
        void Unsubscribe() {
            if (m_Unsubscriber) {
                m_Unsubscriber();
                m_Unsubscriber = nullptr;
            }
        }

        explicit operator bool() const { return m_Unsubscriber != nullptr; }

    private:
        explicit SubscriptionHandle(std::function<void()> unsubscriber)
            : m_Unsubscriber(std::move(unsubscriber)) {}

        std::function<void()> m_Unsubscriber;
    };

    // ═══════════════════════════════════════════════════════════════
    // 事件总线
    // ═══════════════════════════════════════════════════════════════
    class EventBus {
    public:
        // ── 发布事件 ──
        template<typename T>
        static void Publish(const T& event) {
            auto& slots = GetSlots<T>();
            std::vector<std::function<void(const T&)>> callbacks;
            {
                std::lock_guard<std::mutex> lock(GetMutex());
                callbacks.reserve(slots.size());
                for (const auto& [id, cb] : slots) {
                    callbacks.push_back(cb);
                }
            }
            for (const auto& cb : callbacks) {
                if (cb) cb(event);
            }
        }

        // ── 订阅事件 ──
        template<typename T>
        static SubscriptionHandle Subscribe(std::function<void(const T&)> callback) {
            auto& slots = GetSlots<T>();
            uint32 id = GetNextId();

            {
                std::lock_guard<std::mutex> lock(GetMutex());
                slots[id] = std::move(callback);
            }

            return SubscriptionHandle([id]() {
                auto& s = GetSlots<T>();
                std::lock_guard<std::mutex> lock(GetMutex());
                s.erase(id);
            });
        }

        /** 便捷模板：成员函数订阅 */
        template<typename T, typename Obj>
        static SubscriptionHandle Subscribe(Obj* obj, void (Obj::*method)(const T&)) {
            return Subscribe<T>([obj, method](const T& e) {
                (obj->*method)(e);
            });
        }

    private:
        using SlotMap = std::unordered_map<uint32, std::function<void(const void*)>>;

        template<typename T>
        static std::unordered_map<uint32, std::function<void(const T&)>>& GetSlots() {
            static std::unordered_map<uint32, std::function<void(const T&)>> s_Slots;
            return s_Slots;
        }

        static std::mutex& GetMutex() {
            static std::mutex s_Mutex;
            return s_Mutex;
        }

        static uint32 GetNextId() {
            static uint32 s_NextId = 1;
            return s_NextId++;
        }
    };

    // ═══════════════════════════════════════════════════════════════
    // 预定义编辑器事件类型
    // ═══════════════════════════════════════════════════════════════

    /** 物体选中事件（Hierarchy 点击 / Viewport 拾取时触发） */
    struct EntitySelectedEvent {
        class GameObject* Entity = nullptr;

        EntitySelectedEvent() = default;
        explicit EntitySelectedEvent(class GameObject* entity) : Entity(entity) {}
    };

    /** 物体取消选中 / 清空选择 */
    struct SelectionClearedEvent {};

    /** 场景播放状态变更 */
    struct ScenePlayStateEvent {
        enum State { Edit, Play, Pause } NewState = Edit;
    };

    /** 场景被切换（Play/Stop 时触发） */
    struct SceneSwitchedEvent {
        class Scene* ActiveScene = nullptr;
    };

    /** 物体变换被修改（Inspector / Gizmo 修改时触发） */
    struct TransformModifiedEvent {
        class GameObject* Entity = nullptr;
    };

    /** 组件被添加/移除 */
    struct ComponentChangedEvent {
        class GameObject* Entity = nullptr;
        const char* ComponentType = nullptr;
        bool Added = true;
    };

    /** 请求聚焦到某个物体 */
    struct FocusEntityRequest {
        class GameObject* Entity = nullptr;
    };

} // namespace Engine