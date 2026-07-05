#include "Engine/Core/TaskManager.h"
#include <atomic>
#include <mutex>

namespace Engine {

    StatusBar* TaskManager::s_StatusBar = nullptr;

    // ── 内部共享状态（线程安全） ──
    struct TaskState {
        std::string name;
        std::string description;
        std::atomic<float> progress{ 0.0f };
        std::atomic<bool> running{ false };
        std::atomic<bool> completed{ false };
        std::function<void()> onComplete;
    };

    static std::mutex s_TaskMutex;
    static std::vector<std::shared_ptr<TaskState>> s_ActiveTasks;

    void TaskManager::Init(StatusBar* statusBar) {
        s_StatusBar = statusBar;
    }

    void TaskManager::Shutdown() {
        s_StatusBar = nullptr;
        std::lock_guard<std::mutex> lock(s_TaskMutex);
        s_ActiveTasks.clear();
    }

    uint32 TaskManager::GetActiveTaskCount() {
        std::lock_guard<std::mutex> lock(s_TaskMutex);

        // 清理已完成的
        auto it = s_ActiveTasks.begin();
        while (it != s_ActiveTasks.end()) {
            if ((*it)->completed.load()) {
                it = s_ActiveTasks.erase(it);
            } else {
                ++it;
            }
        }
        return static_cast<uint32>(s_ActiveTasks.size());
    }

    void TaskManager::Run(const std::string& name,
                           std::function<void()> task,
                           std::function<void()> onComplete,
                           const std::string& description) {
        auto state = std::make_shared<TaskState>();
        state->name = name;
        state->description = description.empty() ? name : description;
        state->progress = 0.0f;
        state->running = true;
        state->onComplete = std::move(onComplete);

        {
            std::lock_guard<std::mutex> lock(s_TaskMutex);
            s_ActiveTasks.push_back(state);
        }

        // 推送到状态栏（线程安全，StatusBar 内部有 mutex）
        if (s_StatusBar) {
            BackgroundTask bt;
            bt.name = name;
            bt.description = description.empty() ? name : description;
            bt.progress = 0.0f;
            bt.isIndeterminate = true;
            s_StatusBar->PushTask(bt);
        }

        // 通过 JobSystem 在后台执行
        if (JobSystem::IsInitialized()) {
            JobSystem::Get()->Schedule([state, task = std::move(task)](
                uint32 /*threadIndex*/) {
                if (task) {
                    task();
                }
                state->progress.store(1.0f);
                state->running.store(false);
                state->completed.store(true);

                // 通过 EventBus 通知主线程
                EventBus::Publish(TaskCompletedEvent{state->name, true});

                if (state->onComplete) {
                    // onComplete 在后台线程运行！
                    // 用户应通过 EventBus 订阅来处理主线程逻辑
                    state->onComplete();
                }
            });
        } else {
            // 无 JobSystem 回退：同步执行
            if (task) task();
            state->completed = true;
            EventBus::Publish(TaskCompletedEvent{name, true});
            if (onComplete) onComplete();
        }
    }

    void TaskManager::RunWithProgress(
        const std::string& name,
        std::function<void(std::function<void(float)>)> task,
        std::function<void()> onComplete,
        const std::string& description) {

        auto state = std::make_shared<TaskState>();
        state->name = name;
        state->description = description.empty() ? name : description;
        state->progress = 0.0f;
        state->running = true;
        state->onComplete = std::move(onComplete);

        {
            std::lock_guard<std::mutex> lock(s_TaskMutex);
            s_ActiveTasks.push_back(state);
        }

        if (s_StatusBar) {
            BackgroundTask bt;
            bt.name = name;
            bt.description = description.empty() ? name : description;
            bt.progress = 0.0f;
            bt.isIndeterminate = false;
            s_StatusBar->PushTask(bt);
        }

        if (JobSystem::IsInitialized()) {
            JobSystem::Get()->Schedule([state, task = std::move(task)](
                uint32 /*threadIndex*/) {
                if (task) {
                    // 传入 setProgress callback
                    auto setProgress = [state](float p) {
                        state->progress.store(p);
                    };
                    task(setProgress);
                }
                state->progress.store(1.0f);
                state->running.store(false);
                state->completed.store(true);

                EventBus::Publish(TaskCompletedEvent{state->name, true});
                if (state->onComplete) {
                    state->onComplete();
                }
            });
        } else {
            if (task) {
                auto setProgress = [state](float p) {
                    state->progress.store(p);
                };
                task(setProgress);
            }
            state->completed = true;
            EventBus::Publish(TaskCompletedEvent{name, true});
            if (onComplete) onComplete();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 主线程轮询（由 Application::Run() 或 EngineEditor::OnUpdate() 调用）
    // ═══════════════════════════════════════════════════════════════
    // 此函数在每帧主线程调用，同步 TaskState ↔ StatusBar
    // ═══════════════════════════════════════════════════════════════
    void TaskManager::PollTasks(float32 dt) {
        (void)dt;
        std::lock_guard<std::mutex> lock(s_TaskMutex);

        for (auto& state : s_ActiveTasks) {
            if (!state->running.load() && state->completed.load()) {
                // 任务已完成 → 从状态栏移除
                if (s_StatusBar) {
                    s_StatusBar->PopTask(state->name);
                }
            } else if (state->running.load()) {
                // 任务运行中 → 更新状态栏进度
                float p = state->progress.load();
                if (s_StatusBar) {
                    s_StatusBar->UpdateTask(state->name, p);
                }
            }
        }

        // 清理完成的 state 对象
        auto it = s_ActiveTasks.begin();
        while (it != s_ActiveTasks.end()) {
            if ((*it)->completed.load()) {
                it = s_ActiveTasks.erase(it);
            } else {
                ++it;
            }
        }
    }

} // namespace Engine