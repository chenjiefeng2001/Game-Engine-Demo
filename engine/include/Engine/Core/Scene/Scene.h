#pragma once

/**
 * @file Scene.h
 * @brief 场景管理 — 管理实体集合、更新/渲染顺序、场景级属性
 *
 * Scene 是游戏中所有 GameObject 的容器，负责：
 *   - 实体生命周期（添加、移除、查找、清空）
 *   - 更新/渲染顺序（支持递归遍历）
 *   - 场景级属性（环境光、重力、雾等）
 *   - 物理同步（将物理引擎结果写回 TransformComponent）
 *
 * 使用示例：
 * @code
 *   Scene scene("MainScene");
 *
 *   auto player = std::make_shared<GameObject>("Player");
 *   player->AddComponent<SpriteComponent>(tex);
 *   scene.AddObject(player);
 *
 *   scene.SetAmbientColor(Vec4(0.1f, 0.1f, 0.15f, 1.0f));
 *
 *   while (running) {
 *       scene.Update(dt);
 *       physicsWorld.Step(dt);
 *       scene.PostPhysicsUpdate();
 *       scene.CollectRenderCommands(queue);
 *       scene.Render();
 *   }
 * @endcode
 */

#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/RHI/IRenderable.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace Engine {

    class IRenderQueue;

    // ============================================================
    // 场景属性（纯数据，可序列化）
    // ============================================================
    struct SceneProperties {
        Vec4   ambientColor     = Vec4(0.02f, 0.02f, 0.03f, 1.0f);  // 环境光颜色
        Vec2   gravity          = Vec2(0.0f, -9.81f);                 // 重力
        float32 fogDensity      = 0.0f;                               // 雾浓度（0 = 无雾）
        Vec4   fogColor         = Vec4(0.5f, 0.5f, 0.5f, 1.0f);      // 雾颜色
        bool   enableFog        = false;
        uint32 renderingOrder   = 0;                                  // 多场景渲染顺序
    };

    // ============================================================
    // 场景
    // ============================================================
    class Scene {
    public:
        Scene();
        explicit Scene(std::string name);
        ~Scene();

        // ── 场景标识 ──
        void SetName(const std::string& name) { m_Name = name; }
        const std::string& GetName() const noexcept { return m_Name; }

        // ── 场景属性 ──
        SceneProperties& GetProperties() noexcept { return m_Properties; }
        const SceneProperties& GetProperties() const noexcept { return m_Properties; }

        void SetAmbientColor(const Vec4& color) noexcept { m_Properties.ambientColor = color; }
        const Vec4& GetAmbientColor() const noexcept { return m_Properties.ambientColor; }

        void SetGravity(const Vec2& gravity) noexcept { m_Properties.gravity = gravity; }
        const Vec2& GetGravity() const noexcept { return m_Properties.gravity; }

        void SetFogDensity(float32 density) noexcept { m_Properties.fogDensity = density; }
        float32 GetFogDensity() const noexcept { return m_Properties.fogDensity; }

        void SetFogColor(const Vec4& color) noexcept { m_Properties.fogColor = color; }
        const Vec4& GetFogColor() const noexcept { return m_Properties.fogColor; }
        void SetFogEnabled(bool enabled) noexcept { m_Properties.enableFog = enabled; }
        bool IsFogEnabled() const noexcept { return m_Properties.enableFog; }

        // ── 激活状态（非活跃场景跳过更新/渲染） ──
        void SetActive(bool active) noexcept { m_Active = active; }
        bool IsActive() const noexcept { return m_Active; }

        // ── 对象管理 ──
        /** 添加对象到场景 */
        void AddObject(std::shared_ptr<GameObject> obj);
        /** 从场景移除对象（自动调用 OnDestroy） */
        bool RemoveObject(GameObject* obj);
        /** 清空所有对象 */
        void Clear();

        /** 按名称查找对象（递归搜索所有根对象及其子树） */
        GameObject* FindObject(const std::string& name);
        const GameObject* FindObject(const std::string& name) const;

        /** 按 ID 查找对象（递归，用于 Picking 系统） */
        GameObject* FindByID(uint32 id);
        const GameObject* FindByID(uint32 id) const;

        /** 根对象数量 */
        size_t GetObjectCount() const noexcept { return m_Objects.size(); }
        /** 场景中所有对象的递归总数 */
        size_t GetTotalObjectCount() const;
        /** 检查对象是否属于此场景 */
        bool Contains(GameObject* obj) const;

        // ── 生命周期（由 Application 每帧调用） ──
        void OnCreate();
        void Update(float32 dt);
        void Render();
        void OnDestroy();

        // ── 物理同步 ──
        void PostPhysicsUpdate();

        // ── 渲染 ──
        void CollectRenderCommands(IRenderQueue& queue);

        // ── 序列化/反序列化 ──
        /**
         * @brief 将场景保存到 JSON 文件
         * @param filePath 输出文件路径（建议 .json 后缀）
         * @return 是否成功
         *
         * 保存场景属性、所有 GameObject、组件数据、资源路径引用。
         * 可通过 LoadFromFile 重建完整场景。
         */
        bool SaveToFile(const std::string& filePath) const;

        /**
         * @brief 从 JSON 文件加载场景（重建所有 GameObject 和组件）
         * @param filePath 输入文件路径
         * @return 是否成功
         *
         * 自动通过 ResourceManager 加载引用的纹理等资源。
         */
        bool LoadFromFile(const std::string& filePath);

        // ── 访问器 ──
        const std::vector<std::shared_ptr<GameObject>>& GetObjects() const noexcept {
            return m_Objects;
        }

    private:
        std::string          m_Name;
        SceneProperties      m_Properties;
        bool                 m_Active = true;
        std::vector<std::shared_ptr<GameObject>> m_Objects;
    };

} // namespace Engine
