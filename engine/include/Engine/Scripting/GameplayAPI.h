#pragma once

/**
 * @file GameplayAPI.h
 * @brief Gameplay Vertical Slice — Input → Script → Entity → Transform 链路
 *        （Scripting 主线 · API Contract v2）
 *
 * 分域边界（S2 契约的延续 —— 现在建立 ownership，防止 God Object）：
 *   Engine.log.*      日志
 *   Engine.time.*     时钟
 *   Engine.input.*    键盘状态（后端可注入）
 *   Engine.entity.*   实体生命周期（句柄制，脚本不持有 C++ 指针）
 *   Engine.transform.* 位置读写
 *
 * 注入点设计：
 *   - 输入经 `IScriptInputProvider` 抽象注入 —— 测试用 mock、沙盒用 GLFW 适配器，
 *     脚本与 Lua 绑定层永远不接触 OS API
 *   - 场景经 `SetScene` 绑定；句柄表由绑定层维护，Reload 不影响句柄有效性
 *
 * v2 契约说明：单进程单世界假设（MVP）。多世界/多场景需求出现时再演进。
 */

#include <cstdint>
#include <string>
#include <memory>

namespace Engine {
    class Scene;
    class GameObject;
    class LuaEngine;   // fwd 由 Scripting 命名空间内声明，此处仅前向使用
namespace Scripting {

    class LuaEngine;

    namespace GameplayAPI {

        /// 输入后端抽象（S2 边界的输入侧实现位）
        class IScriptInputProvider {
        public:
            virtual ~IScriptInputProvider() = default;
            /// keyName 形如 "W"、"Space"、"LeftShift"
            virtual bool IsKeyDown(const char* keyName) = 0;
        };

        // ── 进程级上下文注入（v2：单世界假设）──
        void SetScene(Scene* scene);
        void SetInputProvider(IScriptInputProvider* provider);
        void Reset();                       ///< 测试隔离：清空场景绑定与句柄表

        // ── 分域注册 ──
        /// 在 engine 的全局 `Engine` 表下注册 input/entity/transform 三域。
        /// 幂等：重复调用重建对应子表。
        void RegisterDomains(LuaEngine& engine);

        // ── 句柄操作（供测试与未来编辑器直接驱动）──
        /// 按名称查找实体句柄；重名返回首个匹配；未找到返回 0
        uint32_t HandleFindByName(const std::string& name);
        uint32_t HandleSpawn(const std::string& name);
        bool     HandleDestroy(uint32_t handle);
        /// 将外部创建的对象（如序列化加载产物）纳入句柄体系 —— Ring13 Load 必需
        uint32_t HandleAdopt(std::shared_ptr<GameObject> obj);
        bool     HandleGetPosition(uint32_t h, float& x, float& y, float& z);
        bool     HandleSetPosition(uint32_t h, float x, float y, float z);
        bool     HandleTranslate(uint32_t h, float dx, float dy, float dz);
        std::string HandleGetName(uint32_t h);
        uint32_t HandleCount();

    } // namespace GameplayAPI

}} // namespace Engine::Scripting
