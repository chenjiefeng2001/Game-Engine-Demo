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

    private bool _isDirty;
    public bool IsDirty
    {
        get => _isDirty;
        private set => Set(ref _isDirty, value);
    }

    public bool IsOpen => Entities.Count > 0;

    // ── P2-C2 实体回路 ──
    private string _renameInput = "";
    public string RenameInput
    {
        get => _renameInput;
        set => Set(ref _renameInput, value);
    }

    // ── P2-D Asset Browser ──
    private AssetVm? _selectedAsset;
    public AssetVm? SelectedAsset
    {
        get => _selectedAsset;
        set => Set(ref _selectedAsset, value);
    }
    public bool HasAssetSelection => _selectedAsset is not null;

    private string _selectedSprite = "(none)";
    public string SelectedSprite
    {
        get => _selectedSprite;
        private set => Set(ref _selectedSprite, value);
    }

    // ── P2-F Script Editor ──
    private string _scriptText = "";
    public string ScriptText
    {
        get => _scriptText;
        set => Set(ref _scriptText, value);
    }
    private string _scriptTitle = "(no script)";
    public string ScriptTitle
    {
        get => _scriptTitle;
        private set => Set(ref _scriptTitle, value);
    }
    private int _scriptAssetIndex = -1;
    private bool _scriptDirty;
    public bool ScriptDirty
    {
        get => _scriptDirty;
        private set => Set(ref _scriptDirty, value);
    }

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
        RefreshDirtyState();   // Save → Clean（EV_PROJECT_SAVED 亦会刷新）
        return ok;
    }

    /// P2-E 配套：清空当前会话（不落盘）。关闭工程用；丢弃未保存编辑。
    public void ResetSession()
    {
        _host.Dispose();
        Entities.Clear();
        Assets.Clear();
        SelectedEntity = null;
        SelectedAsset = null;
        _scriptAssetIndex = -1;
        ScriptTitle = "(no script)";
        ScriptText = "";
        ScriptDirty = false;
        IsDirty = false;
        StatusText = "no project loaded";
        LogToConsole("session closed");
        DerivedStateChanged?.Invoke();
    }

    /// P2-C2：Create（VM 层统一入口；EV_ENTITY_CREATED 会自动刷新集合）
    public bool CreateEntityFromVm(string? name = null)
    {
        if (!IsOpen)
        {
            LogToConsole("+ Entity ignored: no project");
            return false;
        }
        int idx = _host.CreateEntity(name);
        if (idx < 0)
        {
            LogToConsole("CREATE FAILED: " + _host.GetLastError());
            return false;
        }
        RefreshDirtyState();
        LogToConsole("entity created: " + (_host.GetEntityName(idx) ?? $"#{idx}"));
        return true;
    }

    // ── P2-C2 实体回路：Rename / Delete ──
    public bool RenameSelected(string newName)
    {
        if (_selectedEntity is null || string.IsNullOrWhiteSpace(newName))
        {
            LogToConsole("rename ignored: no selection or empty name");
            return false;
        }
        bool ok = _host.RenameEntity(_selectedEntity.Index, newName);
        LogToConsole(ok ? $"renamed: {_selectedEntity.Name} -> {newName}"
                        : "RENAME FAILED: " + _host.GetLastError());
        if (ok) ReloadCollectionsFromSession();   // 事件也会触发，双保险
        return ok;
    }

    public bool DeleteSelected()
    {
        if (_selectedEntity is null)
        {
            LogToConsole("delete ignored: no selection");
            return false;
        }
        bool ok = _host.DeleteEntity(_selectedEntity.Index);
        LogToConsole(ok ? $"deleted: {_selectedEntity.Name}"
                        : "DELETE FAILED: " + _host.GetLastError());
        if (ok) SelectedEntity = null;
        return ok;
    }

    // ── P2-D Asset Browser：Assign Sprite 到选中实体 ──
    public bool AssignSpriteToSelected()
    {
        if (_selectedEntity is null || _selectedAsset is null)
        {
            LogToConsole("assign ignored: need entity + asset selection");
            return false;
        }
        bool ok = _host.AssignSprite(_selectedAsset.Index, _selectedEntity.Index);
        LogToConsole(ok ? $"assigned {_selectedAsset.Path} -> {_selectedEntity.Name}"
                        : "ASSIGN FAILED: " + _host.GetLastError());
        if (ok) RefreshInspectorFromSession();   // 刷新 SelectedSprite
        return ok;
    }

    // ── P2-F Script Editor：装载 / 保存游戏脚本 ──
    public bool OpenScript(int assetIndex)
    {
        var title = Assets.FirstOrDefault(a => a.Index == assetIndex)?.Path ?? "?";
        string text = _host.ScriptRead(assetIndex);
        if (text.Length == 0 && _host.GetLastError().Length > 0)
        {
            LogToConsole("SCRIPT READ FAILED: " + _host.GetLastError());
            return false;
        }
        _scriptAssetIndex = assetIndex;
        ScriptTitle = title;
        ScriptText = text;
        ScriptDirty = false;
        LogToConsole($"script loaded: {title} ({text.Length} chars)");
        return true;
    }

    public bool SaveScript()
    {
        if (_scriptAssetIndex < 0)
        {
            LogToConsole("script save ignored: no script open");
            return false;
        }
        bool ok = _host.ScriptSave(_scriptAssetIndex, ScriptText);
        LogToConsole(ok ? $"script saved: {ScriptTitle}"
                        : "SCRIPT SAVE FAILED: " + _host.GetLastError());
        if (ok) ScriptDirty = false;
        return ok;
    }

    public void MarkScriptEdited() => ScriptDirty = true;

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
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvProjectSaved:
                LogToConsole("EV ProjectSaved: " + payload);
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvEntityDeleted:
                LogToConsole("EV EntityDeleted: " + payload);
                ReloadCollectionsFromSession();
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvEntityRenamed:
                LogToConsole("EV EntityRenamed: " + payload);
                ReloadCollectionsFromSession();
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvEntityAssigned:
                LogToConsole("EV EntityAssigned: " + payload);
                RefreshInspectorFromSession();
                RefreshDirtyState();
                break;
        }
    }

    private void ReloadCollectionsFromSession()
    {
        // P2-C 纪律：重载后保持选择（重命名/编辑后 selection 不丢）。
        // 删除路径由调用方显式清空选择。
        int? keepIdx = _selectedEntity?.Index;

        var (entCount, assetCount) = _host.GetCounts();
        Entities.Clear();
        for (int i = 0; i < entCount; i++)
            Entities.Add(new EntityVm(i, _host.GetEntityName(i) ?? $"?{i}"));

        Assets.Clear();
        for (int i = 0; i < assetCount; i++)
            Assets.Add(new AssetVm(i, _host.GetAssetPath(i) ?? "",
                _host.GetAssetType(i) switch { 0 => "Texture", 1 => "Script", _ => "Unknown" }));

        if (keepIdx is int idx)
        {
            var hit = Entities.FirstOrDefault(e => e.Index == idx);
            if (hit is not null)
            {
                _selectedEntity = hit;   // 直写避免 SelectedEntity setter 重入刷新
                RefreshInspectorFromSession();   // 重命名后 Inspector 显示新名
            }
        }

        StatusText = $"[Avalonia] project loaded: {entCount} objects, {assetCount} assets";
        LogToConsole(StatusText);
        DerivedStateChanged?.Invoke();
    }

    private void RefreshInspectorFromSession()
    {
        if (_selectedEntity is null)
        {
            SelectedName = "(none)";
            SelectedSprite = "(none)";
        }
        else
        {
            SelectedName = _selectedEntity.Name;
            var p = _host.GetEntityPosition(_selectedEntity.Index);
            PosX = Format(p.x); PosY = Format(p.y); PosZ = Format(p.z);
            SelectedSprite = _host.GetEntitySprite(_selectedEntity.Index);
            if (SelectedSprite.Length == 0) SelectedSprite = "(none)";
        }
    }

    private void RefreshDirtyState()
    {
        IsDirty = _host.IsDirty();
        Raise(nameof(StatusText));
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
