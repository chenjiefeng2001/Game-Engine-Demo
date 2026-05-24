#pragma once

#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RHI/IRenderable.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class IRenderQueue;

    /**
     * @brief 场景 — 管理游戏对象列表，负责遍历更新与渲染
     *
     * Scene 是一层的场景管理器，持有根级 GameObject 列表。
     * 提供统一的 Add/Remove/Find 接口，并封装了每帧 Update /
     * CollectRenderCommands / OnCreate / OnDestroy 的递归遍历逻辑。
     *
     * 渲染时通过 IRenderable 接口收集命令，不直接与 ISpriteBatch 耦合，
     * 符合 RHI 原则。
     */
    class Scene {
    public:
        Scene() = default;
        ~Scene();

        // ── 对象管理 ──
        void AddObject(std::shared_ptr<GameObject> obj);
        bool RemoveObject(GameObject* obj);
        void Clear();

        /** 按名称查找对象（递归搜索所有根对象及其子树） */
        GameObject* FindObject(const std::string& name);
        const GameObject* FindObject(const std::string& name) const;

        // ── 生命周期 ──
        /** 调用所有根对象的 OnCreate（递归） */
        void OnCreate();
        /** 每帧更新所有根对象（递归） */
        void Update(float32 dt);
        /** 每帧调用所有根对象的 Render（递归） */
        void Render();
        /** 调用所有根对象的 OnDestroy（递归） */
        void OnDestroy();

        // ── 渲染（RHI 版本） ──
        /**
         * @brief 收集所有对象的渲染命令到队列中
         *
         * 遍历所有根对象（及递归子树），对每个实现了 IRenderable 的对象
         * 调用 CollectRenderCommands。Scene 不关心具体如何渲染。
         */
        void CollectRenderCommands(IRenderQueue& queue);

        // ── 访问器 ──
        const std::vector<std::shared_ptr<GameObject>>& GetObjects() const noexcept {
            return m_Objects;
        }

    private:
        std::vector<std::shared_ptr<GameObject>> m_Objects;
    };

} // namespace Engine
