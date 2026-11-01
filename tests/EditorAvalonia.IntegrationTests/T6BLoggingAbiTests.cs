using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using AvaloniaEditor.Native;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace EditorAvalonia.IntegrationTests;

/// <summary>
/// T-6-B B2：managed 侧 ABI 镜像的运行时布局断言。
///
/// 单一来源：直接编译 <c>editor_avalonia/Native/EditorBridgeApi.cs</c>
/// （csproj Compile Link），不复制结构体定义。native 侧同一冻结布局由
/// <c>tests/test_bridge</c> 的 static_assert / EXPECT_EQ 固定；
/// 本测试以 <c>Marshal.SizeOf</c> / <c>Marshal.OffsetOf</c> 的**实测值**
/// 作为 managed 权威证据。不需要 native DLL。
/// </summary>
[TestClass]
public sealed class T6BLoggingAbiTests
{
    [TestMethod]
    public void EngineLogRecordV1_MirrorMatchesFrozenLayout()
    {
        Assert.AreEqual(2392, Marshal.SizeOf<EditorBridgeApi.EngineLogRecordV1>());
        Assert.AreEqual(0, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.Sequence)));
        Assert.AreEqual(8, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.TimestampUtcUs)));
        Assert.AreEqual(16, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.Level)));
        Assert.AreEqual(17, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.Flags)));
        Assert.AreEqual(18, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.CategoryLength)));
        Assert.AreEqual(20, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.SourceLength)));
        Assert.AreEqual(22, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.MessageLength)));
        Assert.AreEqual(24, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.Category)));
        Assert.AreEqual(88, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.Source)));
        Assert.AreEqual(344, RecordOffset(nameof(EditorBridgeApi.EngineLogRecordV1.Message)));

        Assert.AreEqual(64, EditorBridgeApi.EngineLogCategoryBytes);
        Assert.AreEqual(256, EditorBridgeApi.EngineLogSourceBytes);
        Assert.AreEqual(2048, EditorBridgeApi.EngineLogMessageBytes);
        Assert.AreEqual(2392, EditorBridgeApi.EngineLogRecordBytes);
        Assert.AreEqual(32, EditorBridgeApi.EngineLogPollBatch);
        Assert.AreEqual(76544, EditorBridgeApi.EngineLogPollBufferBytes);
    }

    [TestMethod]
    public void EngineLogPollStatsV1_MirrorMatchesFrozenLayout()
    {
        Assert.AreEqual(40, Marshal.SizeOf<EditorBridgeApi.EngineLogPollStatsV1>());
        Assert.AreEqual(0, StatsOffset(nameof(EditorBridgeApi.EngineLogPollStatsV1.StructSize)));
        Assert.AreEqual(4, StatsOffset(nameof(EditorBridgeApi.EngineLogPollStatsV1.RecordCount)));
        Assert.AreEqual(8, StatsOffset(nameof(EditorBridgeApi.EngineLogPollStatsV1.NextSequence)));
        Assert.AreEqual(16, StatsOffset(nameof(EditorBridgeApi.EngineLogPollStatsV1.DroppedTotal)));
        Assert.AreEqual(24, StatsOffset(nameof(EditorBridgeApi.EngineLogPollStatsV1.OldestSequence)));
        Assert.AreEqual(32, StatsOffset(nameof(EditorBridgeApi.EngineLogPollStatsV1.NewestSequence)));
    }

    [TestMethod]
    public void T6BIncrementIsAdditiveOnly()
    {
        // T-6-B：v1.8 -> v1.9；EV_COUNT 与 RuntimeResult 不变。
        Assert.AreEqual((1 << 16) | 9, EditorBridgeApi.EngineHostExpectedVersion);
        Assert.AreEqual(14, EditorBridgeApi.EvCount);
        Assert.AreEqual(76, Marshal.SizeOf<EditorBridgeApi.RuntimeResult>());
        Assert.AreEqual(0x01, EditorBridgeApi.EngineLogFlagCategoryTruncated);
        Assert.AreEqual(0x02, EditorBridgeApi.EngineLogFlagSourceTruncated);
        Assert.AreEqual(0x04, EditorBridgeApi.EngineLogFlagMessageTruncated);
    }

    /// <summary>
    /// B2 实验 6：用生产代码（非复制逻辑）证明 lockstep——把 editor 程序集加载进来，
    /// 直接调用 <c>EditorHostService.VerifyEngineHostAbiVersion</c>。
    /// v1.9 通过；v1.8 抛 <c>HostOperationException</c>，即 Create() 在调用任何
    /// 新导出之前就拒绝混搭。
    /// </summary>
    [TestMethod]
    public void V18MixedVersionIsRejectedByCreateLockstep()
    {
        string dll = LocateEditorDll();
        AssemblyLoadContext.Default.Resolving += (ctx, name) =>
        {
            string candidate = Path.Combine(Path.GetDirectoryName(dll)!, name.Name + ".dll");
            return File.Exists(candidate) ? ctx.LoadFromAssemblyPath(candidate) : null;
        };

        Assembly asm = Assembly.LoadFrom(dll);
        Type service = asm.GetType("AvaloniaEditor.Services.EditorHostService", throwOnError: true)!;
        MethodInfo verify = service.GetMethod(
            "VerifyEngineHostAbiVersion",
            BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)!;
        Assert.IsNotNull(verify, "EditorHostService.VerifyEngineHostAbiVersion not found");

        // 本侧期望（v1.9）必须通过
        verify.Invoke(null, new object[] { EditorBridgeApi.EngineHostExpectedVersion });

        // 旧 v1.8 native 侧必须被拒绝（Create lockstep，非运行期）
        var thrown = Assert.ThrowsException<TargetInvocationException>(
            () => { verify.Invoke(null, new object[] { (1 << 16) | 8 }); });
        Assert.IsNotNull(thrown.InnerException, "lockstep did not throw");
        StringAssert.Contains(thrown.InnerException!.Message, "ABI version mismatch");
        StringAssert.Contains(thrown.InnerException.Message, "00010009");
    }

    private static int RecordOffset(string fieldName)
        => Marshal.OffsetOf<EditorBridgeApi.EngineLogRecordV1>(fieldName).ToInt32();

    private static int StatsOffset(string fieldName)
        => Marshal.OffsetOf<EditorBridgeApi.EngineLogPollStatsV1>(fieldName).ToInt32();

    private static string LocateEditorDll()
    {
        string? env = Environment.GetEnvironmentVariable("T6B_EDITOR_DLL");
        if (!string.IsNullOrWhiteSpace(env) && File.Exists(env)) return env;

        string config = Environment.GetEnvironmentVariable("T6B_CONFIG") ?? "Debug";
        string repo = LocateRepoRoot();
        string path = Path.Combine(repo, "editor_avalonia", "bin", config,
            "net9.0", "AvaloniaEditor.dll");
        Assert.IsTrue(File.Exists(path),
            "AvaloniaEditor.dll not found: " + path +
            ". Build it first: dotnet build editor_avalonia/AvaloniaEditor.csproj -c " + config);
        return path;
    }

    private static string LocateRepoRoot()
    {
        string? env = Environment.GetEnvironmentVariable("T6B_REPO_ROOT");
        if (!string.IsNullOrWhiteSpace(env) &&
            File.Exists(Path.Combine(env, "assets", "gp01", "manifest.json")))
            return env;

        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (; dir is not null; dir = dir.Parent)
        {
            if (File.Exists(Path.Combine(dir.FullName, "assets", "gp01", "manifest.json")))
                return dir.FullName;
        }
        throw new InvalidOperationException("repo root with assets/gp01 was not found");
    }
}
