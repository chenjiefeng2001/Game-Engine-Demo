#pragma once

/**
 * @file System.h
 * @brief ECS System 基类 — 所有游戏系统的抽象基类
 *
 * System 是 ECS 模式的"逻辑"部分，封装操作特定组件组合的算法。
 * 每个系统在每帧 OnUpdate 中接收 EntityManager 引用，
 * 通过 ForEach 遍历拥有所需组件的实体。
 */

#include "Engine/Core/ECS/ECS.h"
#include "Engine/Types.h"
#include <string>

namespace Engine {

    class System {
    public:
        System() = default;
        virtual ~System() = default;

        System(const System&) = delete;
        System& operator=(const System&) = delete;

        /** 系统名称（调试用） */
        virtual const char* GetName() const = 0;

        /** 每帧更新（系统在此处操作 EntityManager） */
        virtual void OnUpdate(EntityManager& em, float32 dt) = 0;

        /** 渲染相关更新（分离渲染和逻辑步进，供需要特殊渲染顺序的系统使用） */
        virtual void OnRender(EntityManager& em) { (void)em; }

        /** 优先级（值越小越先执行） */
        virtual int32 GetPriority() const { return 0; }
    };

} // namespace Engine