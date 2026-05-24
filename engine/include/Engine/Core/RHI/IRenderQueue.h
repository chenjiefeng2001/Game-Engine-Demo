#pragma once

#include "Engine/Core/RHI/RenderCommand.h"
#include <cstddef>
#include <vector>

namespace Engine {

    /**
     * @brief 渲染队列接口 — 用于收集每帧的渲染命令
     *
     * RHI 原则：Scene / GameObject 只依赖此接口收集数据，
     * 不直接与 ISpriteBatch 等具体渲染器耦合。
     */
    class IRenderQueue {
    public:
        virtual ~IRenderQueue() = default;

        /** 压入一条渲染命令 */
        virtual void Push(const RenderCommand& cmd) = 0;

        /** 清空队列（每帧开始前调用） */
        virtual void Clear() = 0;

        /** 获取当前队列中的命令数 */
        virtual size_t GetCount() const = 0;

        /** 获取所有命令的只读引用（供 SceneRenderer 处理） */
        virtual const std::vector<RenderCommand>& GetCommands() const = 0;
    };

} // namespace Engine
