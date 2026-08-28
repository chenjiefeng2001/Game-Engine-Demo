using System;
using System.Runtime.InteropServices;

namespace AvaloniaEditor.Native;

/// <summary>
/// EditorBridge C ABI 的 P/Invoke 面（AV-002）。
/// C# 只见句柄与 UTF-8 文本，绝无 C++ 指针/STL 泄漏（迁移方案 §3/§4）。
/// </summary>
public static class EditorBridgeApi
{
    public const int EvProjectLoaded = 1;
    public const int EvEntityCreated = 2;
    public const int EvEntityMoved = 3;

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void EditorEventCallback(int eventType,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string payload, IntPtr userData);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr EditorSession_Create();

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_Destroy(IntPtr session);

    [return: MarshalAs(UnmanagedType.I1)]
    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern bool EditorSession_OpenProject(IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string manifestPath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string scenePath);

    [return: MarshalAs(UnmanagedType.I1)]
    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern bool EditorSession_SaveProject(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityCount(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetCount(IntPtr session);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_CreateEntity(IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? name);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityName(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetEntityPosition(IntPtr session,
        int index, IntPtr outPos3);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_SetEntityPosition(IntPtr session,
        int index, IntPtr pos3);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetPath(IntPtr session,
        int index, IntPtr outBuffer, int capacity);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetAssetType(IntPtr session,
        int index);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern void EditorSession_SetEventCallback(IntPtr session,
        EditorEventCallback? callback, IntPtr userData);

    [DllImport("EditorBridge", CallingConvention = CallingConvention.Cdecl)]
    public static extern int EditorSession_GetLastError(IntPtr session,
        IntPtr outBuffer, int capacity);
}
