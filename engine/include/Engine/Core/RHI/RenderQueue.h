#pragma once

#include "Engine/Core/RHI/IRenderQueue.h"
#include <vector>

namespace Engine {

    /**
     * @brief 默认渲染队列实现
     *
     * 简单的动态数组容器，线程不安全，适用于单线程每帧重建。
     */
    class RenderQueue : public IRenderQueue {
    public:
        void Push(const RenderCommand& cmd) override;
        void Clear() override;
        size_t GetCount() const override;
        const std::vector<RenderCommand>& GetCommands() const override;

    private:
        std::vector<RenderCommand> m_Commands;
    };

} // namespace Engine
