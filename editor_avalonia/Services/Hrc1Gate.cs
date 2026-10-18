using System;
using System.Linq;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

public static class Hrc1Gate
{
    public static int Run(EditorHostService host, MainViewModel vm, Action<string> log)
    {
        try
        {
            Check(ReferenceEquals(host, vm.Host), "window and view model share one host");
            Check(!host.IsDisposed, "host starts alive");
            Check(!host.HasSession, "host has no session before ownership check");

            host.EnsureRuntime();
            Check(host.HasSession, "host owns session creation");
            host.CloseProject();
            Check(!host.HasSession, "host closes project session without terminal dispose");
            Check(!host.IsDisposed, "close project does not dispose host");

            vm.Dispose();
            Check(!host.IsDisposed, "view model dispose does not dispose host");

            host.Dispose();
            Check(host.IsDisposed, "host reaches terminal disposed state");
            host.Dispose();
            Check(host.IsDisposed, "terminal dispose is idempotent");

            bool rejected = false;
            try
            {
                host.EnsureRuntime();
            }
            catch (ObjectDisposedException)
            {
                rejected = true;
            }
            Check(rejected, "disposed host rejects session recreation");

            bool vmRejected = false;
            int consoleBefore = vm.ConsoleLines.Count;
            bool vmOpened = false;
            try
            {
                vmOpened = vm.OpenProject("missing-manifest.json", "missing-scene.json");
            }
            catch (ObjectDisposedException)
            {
                vmRejected = true;
            }
            // A2/F2-05（ratif﻿ied）：view model 不再让 host 失败以异常形式逃出 handler。
            // 契约意图不变（disposed host 不可复活），但失败现在是"可观测状态"而不是异常。
            Check(!vmOpened, "a disposed host cannot be reopened through the view model");
            Check(!vmRejected,
                "F2-05: the view model no longer lets ObjectDisposedException escape");
            Check(vm.ConsoleLines.Skip(consoleBefore)
                    .Any(line => line.Contains("failed")),
                "the view model reports the refused open instead of throwing");

            log("[HRC-G1 GATE] ALL GREEN");
            return 0;
        }
        catch (Exception ex)
        {
            log("[HRC-G1 GATE] FAIL: " + ex.Message);
            return 1;
        }
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
            throw new InvalidOperationException(message);
    }
}
