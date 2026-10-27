// ============================================================
// NHI-1.4 — native-only teardown probe
//
// 目的：回答二分问题 —— "去掉 Avalonia / CLR / managed Host 后，
// native teardown hang 是否仍然存在？"
//
// 设计约束（来自 NHI-1.4 冻结范围）：
//   - 极薄的 driver：只调用**现有** C ABI（capi.h），不复制 EngineHost 生命周期；
//   - 不新增 production ABI、不改 A2/A6/A7 代码、不进 CTest、不影响 I2=240；
//   - 一个进程只跑一个 scenario（由 argv[1] 选择）；
//   - 记录 native 返回码 / teardown 证据 / 每阶段耗时；
//   - "正常退出" 只是 lifecycle outcome，不是功能 PASS —— 判定由 harness 做。
//
// 用法：
//   nhi1_native_probe.exe <scenario>
//   scenario: A-fbo | A-appinit | A-session
//             B-tick-managed | B-tick-legacy
//             B-frame-managed | B-frame-legacy
//             C-legacy-session
//
// 退出码：0 = 该 scenario 的 native outcome 与预期一致；2 = 不一致；
//         进程 hang 时不会有退出码（harness 记为 FINDING）。
// ============================================================
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "editor_bridge/capi.h"

namespace {

using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void Line(const char* scenario,
          const char* phase,
          const char* detail,
          double ms = -1.0) {
    if (ms >= 0.0)
        std::printf("[NHI1.4] scenario=%s phase=%-22s %6.1fms  %s\n",
                    scenario, phase, ms, detail);
    else
        std::printf("[NHI1.4] scenario=%s phase=%-22s          %s\n",
                    scenario, phase, detail);
    std::fflush(stdout);
}

std::string Describe(const RuntimeResult& r) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "code-return status=%d stage=%d diag_len=%u frame=%llu",
                  r.status, r.stage, r.diagnostic_length,
                  static_cast<unsigned long long>(r.frame_id));
    return buf;
}

void SetFlag(const char* name) { _putenv_s(name, "1"); }
void ClearFlag(const char* name) { _putenv_s(name, ""); }

void ReportTeardownState(const char* scenario, EngineHostHandle host) {
    int emergencyRequested = 0, emergencyCompleted = 0;
    int graphicsAttempted = 0, graphicsSucceeded = 0, lifecycleTimedOut = 0;
    const int code = EngineHost_GetTeardownState(host, &emergencyRequested,
                                                 &emergencyCompleted,
                                                 &graphicsAttempted,
                                                 &graphicsSucceeded,
                                                 &lifecycleTimedOut);
    char buf[224];
    std::snprintf(buf, sizeof(buf),
                  "teardown-state code=%d emergency=%d/%d graphics=%d/%d timedOut=%d",
                  code, emergencyRequested, emergencyCompleted,
                  graphicsAttempted, graphicsSucceeded, lifecycleTimedOut);
    Line(scenario, "teardown-probe", buf);
}

// ── A: bootstrap failure → rollback → process exit ──────────────────
int RunBootstrapFailure(const char* scenario) {
    // 与 managed host 完全一致的前置：先注册 main thread（EngineHost_Create 的
    // WrongThread 守卫要求它）。这不是新生命周期，只是现有 ABI 的既有顺序。
    if (EditorBridge_RegisterMainThread() != 1) {
        Line(scenario, "register", "FAILED to register main thread");
        return 2;
    }
    // 注入由 harness 在进程启动前设置（EditorBridge 的 TestFailure 读 CRT env）。
    RuntimeResult result{};
    const auto start = Clock::now();
    EngineHostHandle host = EngineHost_Create(&result);
    const double ms = MsSince(start);

    char detail[256];
    std::snprintf(detail, sizeof(detail), "create handle=%p %s",
                  static_cast<void*>(host), Describe(result).c_str());
    Line(scenario, "create", detail, ms);

    if (host != nullptr) {
        // 预期之外：注入没有生效。仍然完整拆除，避免把"注入失效"变成 hang。
        Line(scenario, "unexpected", "bootstrap unexpectedly succeeded; tearing down");
        EngineHost_SetEventCallback(host, nullptr, nullptr);
        RuntimeResult shutdownResult{};
        const int code = EngineHost_Shutdown(host, &shutdownResult);
        Line(scenario, "shutdown", Describe(shutdownResult).c_str());
        if (code == 0) EngineHost_Destroy(host);
        return 2;
    }

    // native 侧契约：失败后不得留下半初始化句柄；无句柄可 Destroy。
    Line(scenario, "rollback", "no handle returned: nothing to destroy, returning to CRT exit");
    ClearFlag("EDITOR_BRIDGE_FAIL_FBO");
    ClearFlag("EDITOR_BRIDGE_FAIL_APPLICATION_INIT");
    ClearFlag("EDITOR_BRIDGE_FAIL_SESSION_ATTACH");
    return 0;
}

// ── B: runtime fault → faulted teardown → process exit ───────────────
// policy=managed : A2 §3.3 之后的策略（Shutdown 未完成则不 Destroy）
// policy=legacy  : A2 之前的策略（总是 Destroy）——用于对比 late-free
int RunFaultTeardown(const char* scenario, const char* flag, bool playFirst) {
    const bool managedPolicy = std::strstr(scenario, "-managed") != nullptr;

    if (EditorBridge_RegisterMainThread() != 1) {
        Line(scenario, "register", "FAILED to register main thread");
        return 2;
    }

    RuntimeResult result{};
    auto start = Clock::now();
    EngineHostHandle host = EngineHost_Create(&result);
    Line(scenario, "create", Describe(result).c_str(), MsSince(start));
    if (host == nullptr) {
        Line(scenario, "unexpected", "create failed before the fault could be injected");
        return 2;
    }

    start = Clock::now();
    const int openCode = EngineHost_OpenProject(
        host, "assets/gp01/manifest.json", "assets/gp01/Main.scene", &result);
    Line(scenario, "open-project", Describe(result).c_str(), MsSince(start));
    if (openCode != 0) {
        EngineHost_Destroy(host);
        return 2;
    }

    if (playFirst) {
        start = Clock::now();
        const int playCode = EngineHost_Play(host, -1, &result);
        Line(scenario, "play", Describe(result).c_str(), MsSince(start));
        if (playCode != 0) {
            RuntimeResult shutdownResult{};
            EngineHost_Shutdown(host, &shutdownResult);
            EngineHost_Destroy(host);
            return 2;
        }
    }

    // 注入 fault（native 侧 _putenv_s 对 EditorBridge 的 getenv 可见）。
    SetFlag(flag);
    start = Clock::now();
    const int pumpCode = EngineHost_PumpOneFrame(host, 0.016f, &result);
    Line(scenario, "pump(injected)", Describe(result).c_str(), MsSince(start));
    ClearFlag(flag);
    if (pumpCode == 0) {
        Line(scenario, "unexpected", "injected fault did not trigger");
        RuntimeResult shutdownResult{};
        EngineHost_Shutdown(host, &shutdownResult);
        EngineHost_Destroy(host);
        return 2;
    }

    ReportTeardownState(scenario, host);

    // managed teardown 顺序：detach callback → Shutdown → (完成才 Destroy)
    start = Clock::now();
    EngineHost_SetEventCallback(host, nullptr, nullptr);
    RuntimeResult shutdownResult{};
    const int shutdownCode = EngineHost_Shutdown(host, &shutdownResult);
    Line(scenario, "shutdown", Describe(shutdownResult).c_str(), MsSince(start));
    ReportTeardownState(scenario, host);

    if (managedPolicy) {
        if (shutdownCode == 0 &&
            shutdownResult.status == ENGINE_HOST_STATUS_SHUTDOWN_COMPLETE) {
            start = Clock::now();
            EngineHost_Destroy(host);
            Line(scenario, "destroy", "shutdown complete -> destroyed", MsSince(start));
        } else {
            Line(scenario, "destroy",
                 "shutdown incomplete -> host intentionally NOT destroyed (A2 policy)");
        }
    } else {
        start = Clock::now();
        EngineHost_Destroy(host);   // A2 之前的策略：late-free 风险路径
        Line(scenario, "destroy", "destroyed unconditionally (legacy policy)", MsSince(start));
    }
    return 0;
}

// ── C: no project → legacy session destruction → process exit ─────────
int RunLegacySessionDestroy(const char* scenario) {
    if (EditorBridge_RegisterMainThread() != 1) {
        Line(scenario, "register", "FAILED to register main thread");
        return 2;
    }
    auto start = Clock::now();
    EditorSessionHandle session = EditorSession_Create();
    Line(scenario, "create-session",
         session ? "created" : "null", MsSince(start));
    if (session == nullptr) return 2;

    start = Clock::now();
    EditorSession_Destroy(session);   // 未 OpenProject 就销毁（与 --hrc1 同形）
    Line(scenario, "destroy-session", "destroyed without OpenProject", MsSince(start));
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: nhi1_native_probe.exe <scenario>\n");
        return 2;
    }
    const std::string scenario = argv[1];
    const auto started = Clock::now();

    int rc = 2;
    if (scenario == "A-fbo") {
        rc = RunBootstrapFailure("A-fbo");
    } else if (scenario == "A-appinit") {
        rc = RunBootstrapFailure("A-appinit");
    } else if (scenario == "A-session") {
        rc = RunBootstrapFailure("A-session");
    } else if (scenario == "B-tick-managed") {
        rc = RunFaultTeardown("B-tick-managed", "EDITOR_BRIDGE_FAIL_RUNTIME_TICK", true);
    } else if (scenario == "B-tick-legacy") {
        rc = RunFaultTeardown("B-tick-legacy", "EDITOR_BRIDGE_FAIL_RUNTIME_TICK", true);
    } else if (scenario == "B-frame-managed") {
        rc = RunFaultTeardown("B-frame-managed", "EDITOR_BRIDGE_FAIL_FRAME", false);
    } else if (scenario == "B-frame-legacy") {
        rc = RunFaultTeardown("B-frame-legacy", "EDITOR_BRIDGE_FAIL_FRAME", false);
    } else if (scenario == "C-legacy-session") {
        rc = RunLegacySessionDestroy("C-legacy-session");
    } else {
        std::printf("unknown scenario: %s\n", scenario.c_str());
        return 2;
    }

    std::printf("[NHI1.4] scenario=%s DONE rc=%d total=%.1fms\n",
                scenario.c_str(), rc, MsSince(started));
    std::fflush(stdout);
    return rc;
}
