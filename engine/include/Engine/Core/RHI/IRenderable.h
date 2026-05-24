#pragma once

namespace Engine {

    class IRenderQueue;

    /**
     * @brief 可渲染对象接口 — 实现此接口的对象可以向渲染队列提交渲染命令
     *
     * RHI 原则：Scene 只依赖 IRenderable，不关心具体对象类型。
     * 任何需要渲染的游戏对象／组件只需实现 CollectRenderCommands。
     */
    class IRenderable {
    public:
        virtual ~IRenderable() = default;

        /**
         * @brief 收集本对象的渲染命令到队列中
         * @param queue 渲染队列（由 SceneRenderer 创建）
         */
        virtual void CollectRenderCommands(IRenderQueue& queue) = 0;
    };

} // namespace Engine
