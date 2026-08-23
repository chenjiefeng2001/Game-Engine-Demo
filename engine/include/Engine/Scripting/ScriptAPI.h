#pragma once

/**
 * @file ScriptAPI.h
 * @brief 脚本 → 引擎 稳定 API 边界（Scripting MVP · S2）
 *
 * 设计约束（docs/GPU-Physics-v3.0 · Scripting 主线）：
 *   - 脚本只能通过此处注册的稳定接口访问引擎，禁止直触 C++ internals
 *   - 接口以全局表 `Engine.*` 暴露，命名与语义保持向前兼容
 *   - 本 MVP 仅包含最小面：日志 / 时间 / 随机数 / 版本
 *     gameplay 面（输入、实体、场景查询）后续在同一边界内扩展，不改机制
 */

#include <string>

namespace Engine { namespace Scripting {

    class LuaEngine;

    namespace ScriptAPI {

        /// 向 Lua 全局环境注册 `Engine` 表（幂等：重复注册先清空重建）
        void RegisterAll(LuaEngine& engine);

        /// 当前 API Contract 版本（与 Engine.api_version 同源）
        double GetApiVersion();

        /// M005：脚本推送的 HUD 消息（渲染端每帧读取后调 ClearHudText）
        void        SetHudText(const std::string& msg);
        const char* GetHudText();       ///< nullptr = 无新消息
        void        ClearHudText();

        /// 引擎启动时刻（steady_clock 秒基点，供 Engine.time_now 计算）
        void ResetClockOrigin();

    } // namespace ScriptAPI

}} // namespace Engine::Scripting
