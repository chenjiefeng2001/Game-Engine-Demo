using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Interactivity;
using AvaloniaEditor.ViewModels;

namespace AvaloniaEditor.Services;

/// <summary>
/// T-1 Managed UI Acceptance A：真实 editor_avalonia 生产入口的
/// RigidBody Inspector 配置、保存、关闭、重开与 Play/Stop 后回填。
/// 使用临时 scratch 工程，不写 repo fixture。
/// </summary>
public static class T1ManagedAcceptanceGate
{
    public static async Task<int> RunAsync(
        MainWindow window, EditorHostService host, MainViewModel vm, Action<string> log)
    {
        string? scratch = null;
        try
        {
            Console.WriteLine("[T1UI] start");
            var repo = FindRepoRoot();
            Check(repo is not null, "repo root with assets/gp01 was found");
            scratch = Path.Combine(Path.GetTempPath(),
                                   "engine_t1ui_" + Guid.NewGuid().ToString("N"));
            CopyDirectory(Path.Combine(repo!.FullName, "assets", "gp01"),
                          Path.Combine(scratch, "assets", "gp01"));
            Console.WriteLine("[T1UI] copied " + scratch);

            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "managed editor opened the scratch project");
            Console.WriteLine("[T1UI] opened");
            Check(vm.IsOpen, "managed editor reports the project open");

            Check(vm.CreateEntity("T1UI_Body").Succeeded, "CreateEntity succeeded");
            await Task.Delay(150);   // 等待生产事件/pump 完成集合刷新
            Check(vm.SelectByName("T1UI_Body"), "new entity is selected");
            int idx = vm.SelectedEntity!.Index;
            Console.WriteLine("[T1UI] created index=" + idx);

            // Collider schema 对照：只通过通用组件 API 配置，不做任何 T-1 修改。
            Check(host.AddComponent(idx, "Collider"), "Collider added through host");
            Check(host.SetComponentProperty(idx, "Collider", "shape", "Box"),
                  "Collider shape set to Box");

            Click(window, "AddRigidBodyBtn");
            Check(vm.HasRigidBody, "Inspector reveals the rigidbody block after Add");
            Check(host.HasComponent(idx, "RigidBody"), "session holds RigidBody");
            Console.WriteLine("[T1UI] rigidbody added");

            vm.RigidBodyMotionType = "Kinematic";
            vm.RigidBodyLinearDamping = "0.25";
            vm.RigidBodyAngularDamping = "0.5";
            Click(window, "ApplyRigidBodyBtn");
            Check(host.GetComponentProperty(idx, "RigidBody", "motionType") == "Kinematic",
                  "Inspector Apply wrote motionType");
            Check(host.GetComponentProperty(idx, "RigidBody", "linearDamping") == "0.25",
                  "Inspector Apply wrote linearDamping");
            Check(host.GetComponentProperty(idx, "RigidBody", "angularDamping") == "0.5",
                  "Inspector Apply wrote angularDamping");
            Console.WriteLine("[T1UI] applied");

            Check(vm.SaveProject(), "SaveProject succeeded");
            Check(!vm.IsDirty, "save cleared dirty state");
            Console.WriteLine("[T1UI] saved");

            vm.ResetSession();
            Check(!vm.IsOpen, "session closed");
            Console.WriteLine("[T1UI] closed");
            Check(vm.OpenProjectRooted(scratch,
                                       "assets/gp01/manifest.json",
                                       "assets/gp01/Main.scene"),
                  "managed editor reopened the scratch project");
            Check(vm.SelectByName("T1UI_Body"), "reloaded entity is selectable");
            Check(vm.HasRigidBody, "Inspector back-fills RigidBody after reopen");
            Check(vm.RigidBodyMotionType == "Kinematic", "motionType restored");
            Check(vm.RigidBodyLinearDamping == "0.25", "linearDamping restored");
            Check(vm.RigidBodyAngularDamping == "0.5", "angularDamping restored");
            Check(host.GetComponentProperty(vm.SelectedEntity!.Index, "Collider", "shape") == "Box",
                  "Collider schema/data remains readable and unchanged");
            Console.WriteLine("[T1UI] reopened and fields restored");

            Check(vm.PlayScript(-1), "Play started through the managed runtime");
            Check(vm.IsRunning, "managed editor reports running");
            Console.WriteLine("[T1UI] playing");
            await Task.Delay(200);                 // production pump drives RuntimeTick
            vm.StopPlaying();
            Check(!vm.IsRunning, "Stop restored edit state");
            Check(vm.HasRigidBody, "Inspector still holds RigidBody after Play/Stop");
            Check(vm.RigidBodyMotionType == "Kinematic",
                  "Play/Stop did not replace edit component state");
            Console.WriteLine("[T1UI] stopped");

            log("[T1UI-A] managed Inspector add/edit/save/close/reopen/Play/Stop PASS");
            return 0;
        }
        catch (Exception ex)
        {
            Console.WriteLine("[T1UI] FAIL: " + ex.Message);
            log("[T1UI-A] FAIL: " + ex.Message);
            return 1;
        }
        finally
        {
            try { vm.ResetSession(); } catch { }
            if (scratch is not null)
            {
                try { Directory.Delete(scratch, recursive: true); } catch { }
            }
        }
    }

    private static void Click(MainWindow window, string name)
    {
        var button = window.FindControl<Button>(name);
        Check(button is not null, $"button '{name}' was found");
        button!.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
    }

    private static DirectoryInfo? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir;
        return null;
    }

    private static void CopyDirectory(string source, string target)
    {
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source))
            File.Copy(file, Path.Combine(target, Path.GetFileName(file)), overwrite: true);
        foreach (var dir in Directory.GetDirectories(source))
            CopyDirectory(dir, Path.Combine(target, Path.GetFileName(dir)));
    }

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }
}
