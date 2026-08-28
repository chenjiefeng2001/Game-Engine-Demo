using System;
using System.Runtime.InteropServices;
using AvaloniaEditor.Native;

namespace AvaloniaEditor.Services;

/// <summary>
/// Phase 0 会话服务：持有 native 会话生命周期与事件回调（防 GC 回收委托），
 /// 并把 ABI 结果翻译为 UI 可消费的 C# 面。单线程使用。
 /// </summary>
public sealed class EditorHostService : IDisposable
{
    private IntPtr _session = IntPtr.Zero;
    private EditorBridgeApi.EditorEventCallback? _pinnedCallback;

    public event Action<int, string>? NativeEvent;
    public bool IsOpen { get; private set; }

    public void CreateSession()
    {
        if (_session != IntPtr.Zero) return;
        _session = EditorBridgeApi.EditorSession_Create();
        if (_session == IntPtr.Zero)
            throw new InvalidOperationException("EditorSession_Create failed");
        _pinnedCallback = OnNativeEvent;   // 保活，防止 GC 收集后 native 回调悬垂
        EditorBridgeApi.EditorSession_SetEventCallback(_session, _pinnedCallback, IntPtr.Zero);
    }

    public bool OpenProject(string manifestPath, string scenePath)
    {
        EnsureSession();
        IsOpen = EditorBridgeApi.EditorSession_OpenProject(_session, manifestPath, scenePath);
        return IsOpen;
    }

    public bool SaveProject()
    {
        ThrowIfNoSession();
        return EditorBridgeApi.EditorSession_SaveProject(_session);
    }

    public (int entities, int assets) GetCounts()
    {
        ThrowIfNoSession();
        return (EditorBridgeApi.EditorSession_GetEntityCount(_session),
                EditorBridgeApi.EditorSession_GetAssetCount(_session));
    }

    public int CreateEntity(string? name = null)
    {
        ThrowIfNoSession();
        return EditorBridgeApi.EditorSession_CreateEntity(_session, name);
    }

    public string? GetEntityName(int index)
    {
        ThrowIfNoSession();
        var buf = Marshal.AllocHGlobal(256);
        try
        {
            int len = EditorBridgeApi.EditorSession_GetEntityName(_session, index, buf, 256);
            return len < 0 ? null : Marshal.PtrToStringUTF8(buf);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public (float x, float y, float z) GetEntityPosition(int index)
    {
        ThrowIfNoSession();
        var buf = Marshal.AllocHGlobal(12);
        try
        {
            if (EditorBridgeApi.EditorSession_GetEntityPosition(_session, index, buf) != 0)
                throw new InvalidOperationException("GetEntityPosition failed");
            var f = new float[3];
            Marshal.Copy(buf, f, 0, 3);
            return (f[0], f[1], f[2]);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public bool SetEntityPosition(int index, float x, float y, float z)
    {
        ThrowIfNoSession();
        var buf = Marshal.AllocHGlobal(12);
        try
        {
            Marshal.Copy(new[] { x, y, z }, 0, buf, 3);
            return EditorBridgeApi.EditorSession_SetEntityPosition(_session, index, buf) == 0;
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public string? GetAssetPath(int index)
    {
        ThrowIfNoSession();
        var buf = Marshal.AllocHGlobal(512);
        try
        {
            int len = EditorBridgeApi.EditorSession_GetAssetPath(_session, index, buf, 512);
            return len < 0 ? null : Marshal.PtrToStringUTF8(buf);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public int GetAssetType(int index)
    {
        ThrowIfNoSession();
        return EditorBridgeApi.EditorSession_GetAssetType(_session, index);
    }

    public string GetLastError()
    {
        if (_session == IntPtr.Zero) return "(no session)";
        var buf = Marshal.AllocHGlobal(512);
        try
        {
            EditorBridgeApi.EditorSession_GetLastError(_session, buf, 512);
            return Marshal.PtrToStringUTF8(buf) ?? "";
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    private void OnNativeEvent(int eventType, string payload, IntPtr userData)
        => NativeEvent?.Invoke(eventType, payload);

    private void EnsureSession() { if (_session == IntPtr.Zero) CreateSession(); }

    private void ThrowIfNoSession()
    {
        if (_session == IntPtr.Zero)
            throw new InvalidOperationException("native session not created");
    }

    public void Dispose()
    {
        if (_session != IntPtr.Zero)
        {
            EditorBridgeApi.EditorSession_SetEventCallback(_session, null, IntPtr.Zero);
            EditorBridgeApi.EditorSession_Destroy(_session);
            _session = IntPtr.Zero;
            _pinnedCallback = null;
        }
    }
}
