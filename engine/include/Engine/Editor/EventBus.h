#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>

namespace Engine {

    // 简单的事件总线，支持发布-订阅模式
    class EventBus {
    public:
        using HandlerId = size_t;

        template<typename EventType>
        using EventHandler = std::function<void(const EventType&)>;

        template<typename EventType>
        HandlerId Subscribe(EventHandler<EventType> handler) {
            auto& handlers = GetHandlers<EventType>();
            handlers.push_back({ ++s_NextId, std::move(handler) });
            return s_NextId;
        }

        template<typename EventType>
        void Unsubscribe(HandlerId id) {
            auto& handlers = GetHandlers<EventType>();
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [id](const auto& entry) { return entry.first == id; }),
                handlers.end());
        }

        template<typename EventType>
        void Publish(const EventType& event) {
            const auto& handlers = GetHandlers<EventType>();
            for (const auto& [id, handler] : handlers) {
                handler(event);
            }
        }

        static EventBus& Get() {
            static EventBus instance;
            return instance;
        }

    private:
        EventBus() = default;

        template<typename EventType>
        using HandlerList = std::vector<std::pair<HandlerId, EventHandler<EventType>>>;

        template<typename EventType>
        HandlerList<EventType>& GetHandlers() {
            static HandlerList<EventType> handlers;
            return handlers;
        }

        static inline size_t s_NextId = 0;
    };

} // namespace Engine