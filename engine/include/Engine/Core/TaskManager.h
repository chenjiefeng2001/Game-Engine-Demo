#pragma once

/**
 * @file TaskManager.h
 * @brief 后台任务管理器 — 非阻塞主线程 + 状态栏进度 + 完成通知
 *
 * 封装 JobSystem + StatusBar + EventBus 三件套：
 *   - 后台执行不阻塞主线程（利用 JobSystem 线程池）
 *   - 实时更新 StatusBar 菊花图和进度
 *   - 完成后通过 EventBus 发布事件
 *
 * 使用方式：
 * @code
 *   // ── 执行后台任务（自动显示进度） ──
 *   TaskManager::Run("Compile Shaders",
 *       []() {
 *           VFXCompiler::CompileAll();
 *       },
 *       []() {
 *           EventBus::Publish(ShaderCompiledEvent{});
 *           StatusBar::ShowNotification("Shaders compiled!");
 *       }
 *   );
 *
 *   // ── 带进度的任务 ──
 *   TaskManager::RunWithProgress("Import Model",
 *       [&](std::function<void(float)> setProgress) {
 *           setProgress(0.3f);
 *           LoadMesh();
 *           setProgress(0.6f);
 *           GenerateMaterials();
 *           setProgress(1.0f);
 *       },
 *       []() { EventBus::Publish(ModelImportedEvent{}); }
 *   );
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Editor/StatusBar.h"
#include "Engine/Core/EventBus.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>

namespace Engine {

    // ============================================================
    // 预定义事件类型
    // ============================================================
    struct TaskCompletedEvent {
        std::string taskName;
        bool        success = true;
    };

    struct TaskProgressEvent {
        std::string taskName;
        float       progress = 0.0f;  // 0-1
    };

    // ============================================================
    // 任务管理器（单例）
    // ============================================================
    class TaskManager {
    public:
        // ── 执行后台任务 ──
        /**
         * @brief 在后台线程执行任务，不阻塞主线程
         * @param name        任务名称（显示在状态栏）
         * @param task        后台执行函数
         * @param onComplete  完成回调（在主线程执行）
         * @param description 状态栏描述（可选）
         */
        static void Run(const std::string& name,
                        std::function<void()> task,
                        std::function<void()> onComplete = nullptr,
                        const std::string& description = "");

        /**
         * @brief 带进度报告的后台任务
         * @param name        任务名称
         * @param task        后台执行函数，接收 setProgress(float) 回调
         * @param onComplete  完成回调（在主线程执行）
         * @param description 状态栏描述
         */
        static void RunWithProgress(const std::string& name,
                                    std::function<void(std::function<void(float)>)> task,
                                    std::function<void()> onComplete = nullptr,
                                    const std::string& description = "");

        // ── 查询 ──
        static uint32 GetActiveTaskCount();
        static bool   IsBusy() { return GetActiveTaskCount() > 0; }

        // ── 初始化 ──
        static void Init(StatusBar* statusBar);
        static void Shutdown();

        // ── 主线程轮询（每帧调用，同步状态栏进度） ──
        static void PollTasks(float32 dt);

    private:
        static StatusBar* s_StatusBar;
    };

} // namespace Engine