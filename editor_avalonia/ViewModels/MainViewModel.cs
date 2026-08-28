using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using AvaloniaEditor.Native;
using AvaloniaEditor.Services;

namespace AvaloniaEditor.ViewModels;

/// <summary>P1-C 单向依赖：View → ViewModel → EditorHostService(Session ABI) → Engine。
/// 禁止 View 直接触碰 native 句柄或引擎对象。</summary>
public sealed class MainViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly EditorHostService _host = new();

    public ObservableCollection<EntityVm> Entities { get; } = new();
    public ObservableCollection<AssetVm> Assets { get; } = new();
    public ObservableCollection<string> ConsoleLines { get; } = new();

    private EntityVm? _selectedEntity;
    public EntityVm? SelectedEntity
    {
        get => _selectedEntity;
        set
        {
            if (!Set(ref _selectedEntity, value)) return;
            RefreshInspectorFromSession();
            Raise(nameof(HasSelection));
        }
    }

    public bool HasSelection => _selectedEntity is not null;

    // ── Inspector（Transform 冻结契约：仅 px/py/pz）──
    private string _selectedName = "(none)";
    public string SelectedName
    {
        get => _selectedName;
        private set => Set(ref _selectedName, value);
    }

    private string _posX = "0";
    public string PosX { get => _posX; set => Set(ref _posX, value); }
    private string _posY = "0";
    public string PosY { get => _posY; set => Set(ref _posY, value); }
    private string _posZ = "0";
    public string PosZ { get => _posZ; set => Set(ref _posZ, value); }

    private string _statusText = "no project loaded";
    public string StatusText
    {
        get => _statusText;
        private set => Set(ref _statusText, value);
    }

    public bool IsOpen => Entities.Count > 0;

    /// 供外部（gate）在会话事件之外强制重算派生状态
    public event Action? DerivedStateChanged;

    public MainViewModel()
    {
        _host.NativeEvent += OnNativeEvent;
    }

    // ── 会话生命周期（P1-B）────────────────────────────────
    public bool OpenProject(string manifestPath, string scenePath)
    {
        if (!_host.OpenProject(manifestPath, scenePath))
        {
            LogToConsole("OPEN FAILED: " + _host.GetLastError());
            return false;
        }
        ReloadCollectionsFromSession();     // ProjectLoaded → 各 Pane 刷新
        return true;
    }

    public bool SaveProject()
    {
        if (!IsOpen)
        {
            LogToConsole("Save ignored: no project");
            return false;
        }
        bool ok = _host.SaveProject();
        LogToConsole(ok ? "project saved" : "SAVE FAILED: " + _host.GetLastError());
        return ok;
    }

    public void ApplyInspectorPosition()
    {
        if (_selectedEntity is null) return;
        if (!TryParse(PosX, out var x) || !TryParse(PosY, out var y) ||
            !TryParse(PosZ, out var z))
        {
            LogToConsole("invalid transform input");
            return;
        }
        if (_host.SetEntityPosition(_selectedEntity.Index, x, y, z))
            LogToConsole($"transform applied: {_selectedEntity.Name} -> ({Format(x)}, {Format(y)}, {Format(z)})");
        else
            LogToConsole("SET FAILED: " + _host.GetLastError());
    }

    // ── Session 事件订阅（P1-B 事件流：Pane 不互相调用）─────
    private void OnNativeEvent(int type, string payload)
    {
        switch (type)
        {
            case EditorBridgeApi.EvProjectLoaded:
                LogToConsole("EV ProjectLoaded: " + payload);
                break;
            case EditorBridgeApi.EvEntityCreated:
                LogToConsole("EV EntityCreated: " + payload);
                ReloadCollectionsFromSession();          // 单一事实源刷新
                break;
            case EditorBridgeApi.EvEntityMoved:
                LogToConsole("EV EntityMoved: " + payload);
                break;
        }
    }

    private void ReloadCollectionsFromSession()
    {
        var (entCount, assetCount) = _host.GetCounts();
        Entities.Clear();
        for (int i = 0; i < entCount; i++)
            Entities.Add(new EntityVm(i, _host.GetEntityName(i) ?? $"?{i}"));

        Assets.Clear();
        for (int i = 0; i < assetCount; i++)
            Assets.Add(new AssetVm(i, _host.GetAssetPath(i) ?? "",
                _host.GetAssetType(i) switch { 0 => "Texture", 1 => "Script", _ => "Unknown" }));

        StatusText = $"[Avalonia] project loaded: {entCount} objects, {assetCount} assets";
        LogToConsole(StatusText);
        DerivedStateChanged?.Invoke();
    }

    private void RefreshInspectorFromSession()
    {
        if (_selectedEntity is null)
        {
            SelectedName = "(none)";
        }
        else
        {
            SelectedName = _selectedEntity.Name;
            var p = _host.GetEntityPosition(_selectedEntity.Index);
            PosX = Format(p.x); PosY = Format(p.y); PosZ = Format(p.z);
        }
    }

    internal void LogToConsole(string line)
    {
        ConsoleLines.Add($"[{DateTime.Now:HH:mm:ss.fff}] {line}");
    }

    /// GG1 用：按名选择实体（模拟 Hierarchy 点击的数据路径）
    public bool SelectByName(string name)
    {
        var hit = Entities.FirstOrDefault(e => e.Name == name);
        if (hit is null) return false;
        SelectedEntity = hit;
        return true;
    }

    // ── helpers ────────────────────────────────────────────
    private static string Format(float v) => v.ToString("0.0###");
    private static bool TryParse(string s, out float v) => float.TryParse(s, out v);

    public event PropertyChangedEventHandler? PropertyChanged;
    private bool Set<T>(ref T field, T value,
        [CallerMemberName] string? name = null)
    {
        if (Equals(field, value)) return false;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        return true;
    }
    private void Raise([CallerMemberName] string? name = null)
        => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    public void Dispose() => _host.Dispose();
}

public sealed record EntityVm(int Index, string Name);

public sealed record AssetVm(int Index, string Path, string TypeName);
