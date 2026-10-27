using System;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Linq;
using System.Runtime.CompilerServices;
using Avalonia.Threading;
using AvaloniaEditor.Native;
using AvaloniaEditor.Services;

namespace AvaloniaEditor.ViewModels;

/// <summary>GL 依赖编辑的异步状态机：submitted ≠ executed。</summary>
public enum PendingEditStatus
{
    /// <summary>没有在途的 pending edit。</summary>
    Idle,
    /// <summary>native 已受理，等待下一次 pump 帧内执行。</summary>
    Submitted,
    /// <summary>帧内执行成功（此时才允许报告“完成”）。</summary>
    Executed,
    /// <summary>帧内执行失败（诊断经 ScriptError / Console / StatusBar 呈现）。</summary>
    Failed,
}

/// <summary>P1-C 单向依赖：View → ViewModel → EditorHostService(Session ABI) → Engine。
/// 禁止 View 直接触碰 native 句柄或引擎对象。</summary>
public sealed class MainViewModel : INotifyPropertyChanged, IDisposable
{
    private readonly EditorHostService _host;

    internal EditorHostService Host => _host;

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

    /// <summary>
    /// Commit C1：VM 内部（选择意图解析 / 集合刷新）改写选择的**唯一**入口。
    ///
    /// 之前这些路径直接写 <c>_selectedEntity</c> 字段，理由是"避免 SelectedEntity
    /// setter 重入刷新"（setter 会无条件 RefreshInspectorFromSession，而刷新路径
    /// 已经自己刷过）。代价是**不发 PropertyChanged**：模型选中了新实体，
    /// 而 OneWay 绑定的 Hierarchy 列表还高亮着旧项 —— 模型对、控件错。
    ///
    /// 这里把两件事分开：刷新由调用方决定（<paramref name="refreshInspector"/>），
    /// 通知由本方法统一负责。值相等（record 值语义）时不发通知，因此"同一次选择
    /// 变更"恰好一次通知，不会因为 ListBox 回写而产生风暴。
    /// </summary>
    private void SetSelectedEntityCore(EntityVm? hit, bool refreshInspector,
                                       bool forceNotify = false)
    {
        bool changed = forceNotify || !Equals(_selectedEntity, hit);
        _selectedEntity = hit;
        if (refreshInspector) RefreshInspectorFromSession();
        if (!changed) return;
        Raise(nameof(SelectedEntity));
        Raise(nameof(HasSelection));
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

    // ── A3/D2：选择意图（index 载体，刷新时解析）──────────────────────
    // 绝不持 EntityVm 引用：ReloadCollectionsFromSession 每次都会重建 record。
    private int? _pendingSelectionIndex;

    /// <summary>最近一次选择意图的解析结果（A3/D2 evidence；null = 已丢弃）。</summary>
    public int? SelectionIntentResolved { get; private set; }

    /// <summary>最近一次 create 登记的选择意图（A3 evidence）。</summary>
    public int? PendingSelectionIndex => _pendingSelectionIndex;

    /// <summary>
    /// A3/D2 evidence：create 登记意图时事件是否已经送达（即 create 走的是哪种时序）。
    /// <c>event-before-return</c> = 集合里已能看到新实体；<c>return-before-event</c> = 还看不到。
    /// 顺序不同，最终 selection 必须一致 —— 这正是 D2 要证明的不变量。
    /// </summary>
    public string LastCreateOrder { get; private set; } = "(none)";

    /// <summary>最近一次 create 回执（A3/D3 evidence）。</summary>
    public CreateEntityReceipt LastCreateReceipt { get; private set; }
        = new(CreateEntityOutcome.Refused, -1, string.Empty, "(no create yet)");

    private bool _isDirty;
    public bool IsDirty
    {
        get => _isDirty;
        private set => Set(ref _isDirty, value);
    }

    /// <summary>
    /// A3/D1：open 状态的**唯一**事实源是 Host（<c>IsOpen</c> + <c>State</c>）。
    /// 本地集合是 Host 状态的投影（见 <see cref="ReconcileWithHostState"/>），
    /// 不再由 <c>Entities.Count</c> 承担 open-state 语义。
    /// </summary>
    public bool IsOpen => _host.IsOpen && _host.State == HostRuntimeState.Ready;

    /// <summary>A3/D1：Host 状态与本地投影最近一次对齐的原因（gate evidence）。</summary>
    public string LastReconcileReason { get; private set; } = "(none)";


    // ── P3-A Hierarchy：Search ──
    /// 主列表（会话事实源）；FilteredEntities 为视图消费的过滤视图。
    public ObservableCollection<EntityVm> FilteredEntities { get; } = new();

    private string _searchText = "";
    public string SearchText
    {
        get => _searchText;
        set
        {
            if (!Set(ref _searchText, value)) return;
            RebuildFilteredEntities();
        }
    }
    public bool HasFilter => _searchText.Length > 0;
    public int FilteredCount => FilteredEntities.Count;

    /// 过滤条件：空串=全显；否则大小写不敏感子串。
    private bool MatchesFilter(EntityVm e)
        => _searchText.Length == 0 ||
           e.Name.Contains(_searchText, StringComparison.OrdinalIgnoreCase);

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

    /// P3-C：按资产索引选中（gate / 双击等数据路径）
    public bool SelectAssetByIndex(int index)
    {
        var asset = Assets.FirstOrDefault(a => a.Index == index);
        if (asset is null) { LogToConsole($"select asset: no asset #{index}"); return false; }
        SelectedAsset = asset;
        return true;
    }

    // ── P3-C Asset Browser：类型过滤 + 搜索 + 过滤视图 ──
    public ObservableCollection<AssetVm> FilteredAssets { get; } = new();

    /// 过滤选项（C1）：All / Texture / Script
    public string[] AssetTypeOptions { get; } = { "All", "Texture", "Script" };

    /// 当前选中资产的裸文件名（不含扩展名）—— Rename 预填用
    public string AssetRenameBaseName
    {
        get
        {
            if (_selectedAsset is null) return "";
            var name = System.IO.Path.GetFileNameWithoutExtension(_selectedAsset.Path);
            return name ?? "";
        }
    }

    private string _assetTypeFilter = "All";
    /// "All" / "Texture" / "Script"
    public string AssetTypeFilter
    {
        get => _assetTypeFilter;
        set
        {
            if (!Set(ref _assetTypeFilter, value)) return;
            RebuildFilteredAssets();
        }
    }

    private string _assetSearchText = "";
    public string AssetSearchText
    {
        get => _assetSearchText;
        set
        {
            if (!Set(ref _assetSearchText, value)) return;
            RebuildFilteredAssets();
        }
    }

    private bool MatchesAssetFilter(AssetVm a)
    {
        if (_assetTypeFilter != "All" && a.TypeName != _assetTypeFilter)
            return false;
        return _assetSearchText.Length == 0 ||
               a.Path.Contains(_assetSearchText, StringComparison.OrdinalIgnoreCase);
    }

    private void RebuildFilteredAssets()
    {
        FilteredAssets.Clear();
        foreach (var a in Assets)
            if (MatchesAssetFilter(a)) FilteredAssets.Add(a);
        Raise(nameof(FilteredAssetCount));
    }

    public int FilteredAssetCount => FilteredAssets.Count;

    private string _selectedSprite = "(none)";
    public string SelectedSprite
    {
        get => _selectedSprite;
        private set => Set(ref _selectedSprite, value);
    }

    private string _selectedScript = "(none)";
    public string SelectedScript
    {
        get => _selectedScript;
        private set => Set(ref _selectedScript, value);
    }

    // ── F1 (Component Contract)：Camera Inspector（反射元数据驱动）──
    private bool _hasCamera;
    public bool HasCamera
    {
        get => _hasCamera;
        private set => Set(ref _hasCamera, value);
    }
    private string _cameraEnabled = "true";
    public string CameraEnabled { get => _cameraEnabled; set => Set(ref _cameraEnabled, value); }
    private string _cameraZoom = "1";
    public string CameraZoom { get => _cameraZoom; set => Set(ref _cameraZoom, value); }
    private string _cameraViewport = "0 0 1280 720";
    public string CameraViewport { get => _cameraViewport; set => Set(ref _cameraViewport, value); }
    private string _cameraBounds = "";
    public string CameraBounds { get => _cameraBounds; set => Set(ref _cameraBounds, value); }

    // ── F1 (Component Contract)：Camera Inspector 操作 ──
    public bool AddCameraToSelected()
    {
        if (_selectedEntity is null) { LogToConsole("add camera ignored: no selection"); return false; }
        if (!_host.AddComponent(_selectedEntity.Index, "Camera"))
        {
            LogToConsole("ADD CAMERA FAILED: " + _host.GetLastError());
            return false;
        }
        LogToConsole($"camera added: {_selectedEntity.Name}");
        RefreshInspectorFromSession();
        RefreshDirtyState();
        return true;
    }

    public bool RemoveCameraFromSelected()
    {
        if (_selectedEntity is null) { LogToConsole("remove camera ignored: no selection"); return false; }
        if (!_host.RemoveComponent(_selectedEntity.Index, "Camera"))
        {
            LogToConsole("REMOVE CAMERA FAILED: " + _host.GetLastError());
            return false;
        }
        LogToConsole($"camera removed: {_selectedEntity.Name}");
        RefreshInspectorFromSession();
        RefreshDirtyState();
        return true;
    }

    /// 应用 Camera 属性（enabled/zoom/viewport/bounds）到选中实体
    public void ApplyCamera()
    {
        if (_selectedEntity is null || !HasCamera) return;
        var idx = _selectedEntity.Index;
        var enabled = CameraEnabled.Equals("true", StringComparison.OrdinalIgnoreCase) ? "true" : "false";
        if (!_host.SetComponentProperty(idx, "Camera", "enabled", enabled)
            || !_host.SetComponentProperty(idx, "Camera", "zoom", CameraZoom)
            || !_host.SetComponentProperty(idx, "Camera", "viewport", CameraViewport)
            || !_host.SetComponentProperty(idx, "Camera", "bounds", CameraBounds))
        {
            LogToConsole("CAMERA APPLY FAILED: " + _host.GetLastError());
        }
        else
        {
            LogToConsole($"camera applied: {_selectedEntity.Name} (zoom={CameraZoom})");
        }
        RefreshInspectorFromSession();
        RefreshDirtyState();
    }

    // ── Viewport（Phase 7）──

    /// 获取视口 FBO 颜色纹理的 OpenGL 句柄（用于 Avalonia 纹理互操作）
    public uint ViewportTextureHandle => _host.GetViewportTextureHandle();

    /// 视口当前渲染尺寸
    public (int width, int height) ViewportSize => _host.GetViewportSize();

    /// 触发视口尺寸变化（Avalonia 控件 resize 时调用）
    public void SetViewportSize(int width, int height) => _host.SetViewportSize(width, height);

    /// 触发视口重绘（每帧调用）
    public void ViewportTick(float dt) => _host.ViewportTick(dt);

    // ── Viewport Camera ──

    private float _viewportCamX = 0f;
    private float _viewportCamY = 0f;
    private float _viewportCamZ = 5f;

    /// 设置视口相机位置
    public void SetViewportCameraPosition(float x, float y, float z)
    {
        _host.SetViewportCameraPosition(x, y, z);
    }

    /// 获取视口相机位置
    public (float x, float y, float z) GetViewportCameraPosition()
    {
        return _host.GetViewportCameraPosition();
    }

    /// 视口相机投影类型：0=Perspective, 1=Orthographic
    private int _viewportProjection = 0;
    public int ViewportProjection
    {
        get => _viewportProjection;
        set
        {
            if (Set(ref _viewportProjection, value))
                _host.SetViewportCameraProjection(value);
        }
    }

    /// 视口相机缩放
    private float _viewportZoom = 1.0f;
    public float ViewportZoom
    {
        get => _viewportZoom;
        set
        {
            if (Set(ref _viewportZoom, value))
                _host.SetViewportCameraZoom(value);
        }
    }

    // ── Viewport Input ──

    /// 转发鼠标移动事件到视口
    public void ViewportMouseMove(float x, float y) => _host.ViewportMouseMove(x, y);

    /// 转发鼠标按钮事件到视口
    public void ViewportMouseButton(int button, bool pressed) => _host.ViewportMouseButton(button, pressed);

    /// 转发鼠标滚轮事件到视口
    public void ViewportScroll(float delta) => _host.ViewportScroll(delta);

    /// 转发键盘按键事件到视口
    public void ViewportKeyDown(int key) => _host.ViewportKeyDown(key);

    /// 转发键盘释放事件到视口
    public void ViewportKeyUp(int key) => _host.ViewportKeyUp(key);

    /// 视口鼠标拾取：返回实体索引（>=0），无命中返回 -1
    public int ViewportPick(float ndcX, float ndcY) => _host.ViewportPick(ndcX, ndcY);

    // ── Gizmo ──

    private int _gizmoMode = 0;
    /// Gizmo 操作模式：0=Select, 1=Translate, 2=Rotate, 3=Scale
    public int GizmoMode
    {
        get => _gizmoMode;
        set
        {
            if (Set(ref _gizmoMode, value))
                _host.SetGizmoMode(value);
        }
    }

    private int _gizmoSpace = 0;
    /// Gizmo 坐标空间：0=World, 1=Local
    public int GizmoSpace
    {
        get => _gizmoSpace;
        set
        {
            if (Set(ref _gizmoSpace, value))
                _host.SetGizmoSpace(value);
        }
    }

    private bool _gizmoSnapEnabled = false;
    public bool GizmoSnapEnabled
    {
        get => _gizmoSnapEnabled;
        set
        {
            if (Set(ref _gizmoSnapEnabled, value))
                _host.SetGizmoSnap(value, _gizmoSnapValue);
        }
    }

    private float _gizmoSnapValue = 0.5f;
    public float GizmoSnapValue
    {
        get => _gizmoSnapValue;
        set
        {
            if (Set(ref _gizmoSnapValue, value))
                _host.SetGizmoSnap(_gizmoSnapEnabled, value);
        }
    }

    // ── Viewport Grid ──

    private bool _viewportGridVisible = true;
    public bool ViewportGridVisible
    {
        get => _viewportGridVisible;
        set
        {
            if (Set(ref _viewportGridVisible, value))
                _host.SetViewportGridVisible(value);
        }
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

    // ── P3-D Script Editor：空态 + 运行时 ──
    /// 是否已打开脚本（ScriptTitle != "(no script)"）。空态驱动用。
    public bool HasScriptOpen => ScriptTitle != "(no script)";

    private bool _isRunning;
    public bool IsRunning
    {
        get => _isRunning;
        private set => Set(ref _isRunning, value);
    }

    private string _scriptError = "";
    /// P3-D D4：最近一次运行时/保存的 Lua 诊断文本（成功时空串）
    public string ScriptError
    {
        get => _scriptError;
        private set => Set(ref _scriptError, value);
    }

    // ── Pending edit（assigned ≠ executed）的 UI 状态 ──
    private string _pendingEditDescription = "";
    private PendingEditStatus _pendingEditState = PendingEditStatus.Idle;

    /// <summary>当前 GL 依赖编辑的异步状态；只有 Executed 才能被呈现为“完成”。</summary>
    public PendingEditStatus PendingEditState
    {
        get => _pendingEditState;
        private set => Set(ref _pendingEditState, value);
    }

    /// 供外部（gate）在会话事件之外强制重算派生状态
    public event Action? DerivedStateChanged;

    public MainViewModel(EditorHostService host)
    {
        _host = host ?? throw new ArgumentNullException(nameof(host));
        _host.NativeEvent += OnNativeEvent;
        // A2/F2-05：pump/runtime 侧 typed failure 必须成为可观测 UI 状态。
        _host.RuntimeFaulted += OnRuntimeFaulted;
    }

    // ── A3/D1：Host 状态 → UI 投影的唯一对齐入口 ──────────────────
    // 契约：Host 是唯一事实源；集合/选择/状态栏都是它的投影。
    // usable  → 重新从 Host 拉取集合（投影对齐）
    // 不可用  → 清空投影（Closed / Fault 都不得呈现 stale state）
    private void ReconcileWithHostState(string reason)
    {
        LastReconcileReason = reason;
        if (_host.IsDisposed)
        {
            // A2/F2-08 契约：终态 Dispose 之后 view model **保留最后一帧状态**，
            // 不得再驱动或清空投影 —— host 已经不存在，投影只是快照。
            // D1 的四态不变式只适用于 host 存活期间（Closed/Opening/Fault）。
            return;
        }
        if (_host.IsOpen && _host.State == HostRuntimeState.Ready)
        {
            ReloadCollectionsFromSession();
            return;
        }
        ClearProjectProjection(reason);
    }

    /// <summary>
    /// 清空"当前 project"投影。幂等：已经是空投影时直接返回（不刷屏）。
    /// 关键：Fault 走这里，**不是**只把 <c>IsOpen</c> 置 false（A3/D1）。
    /// </summary>
    private void ClearProjectProjection(string reason)
    {
        _pendingSelectionIndex = null;      // D2：意图必须随投影一起丢弃
        // teardown/fault 会作废所有在途 pending edit：不得继续显示 Submitted。
        ResetPendingEditState();
        // Slice D：在途 pick 同样随投影作废（teardown 后旧结果不得回流）。
        ResetPickState(reason);
        if (Entities.Count == 0 && Assets.Count == 0
            && _selectedEntity is null && _selectedAsset is null)
        {
            return;
        }
        int dropped = Entities.Count;
        Entities.Clear();
        Assets.Clear();
        SelectedEntity = null;
        SelectedAsset = null;
        RebuildFilteredEntities();
        RebuildFilteredAssets();
        HasCamera = false;
        IsRunning = false;
        StatusText = $"no project loaded ({reason})";
        LogToConsole($"project projection cleared: {reason} (dropped {dropped} local entities)");
        DerivedStateChanged?.Invoke();
    }

    // ── 会话生命周期（P1-B）────────────────────────────────
    public bool OpenProject(string manifestPath, string scenePath)
    {
        // A2/F2-05：bootstrap / open 的 native typed failure 在此转成可观测状态，
        // 绝不让异常逃进 XAML handler（否则会被反射派发包成 TargetInvocationException 并丢失诊断）。
        if (!TryHost("open project", () => _host.OpenProject(manifestPath, scenePath), out bool opened))
        {
            ReconcileWithHostState("open failed");   // A3/D1：消除"旧集合 + host 已关闭"分叉
            return false;
        }
        if (!opened)
        {
            LogToConsole("OPEN FAILED: " + _host.GetLastError());
            StatusText = "open failed (see console)";
            ReconcileWithHostState("open rejected");
            return false;
        }
        // E3 负路径：契约内缺失 GUID 资产的非致命告警（实体保留）入 Console
        var warns = _host.GetWarnings();
        if (warns.Length > 0)
            foreach (var w in warns.Split('\n'))
                if (w.Length > 0) LogToConsole("[WARN] " + w);
        ReconcileWithHostState("open succeeded");   // 集合是 Host 的投影
        // 新 project 不继承上一代的 pending edit 结果。
        ResetPendingEditState();
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
        _host.CloseProject();
        // A3/D1：关闭工程走同一个投影清理入口（A1 的 Filtered* 重建语义保留在其中）。
        ClearProjectProjection("session closed");
        _scriptAssetIndex = -1;
        ScriptTitle = "(no script)";
        ScriptText = "";
        ScriptDirty = false;
        ScriptError = "";
        IsRunning = false;
        HasCamera = false;
        Raise(nameof(HasScriptOpen));
        IsDirty = false;
        StatusText = "no project loaded";
        LogToConsole("session closed");
        DerivedStateChanged?.Invoke();
    }

    /// <summary>A3/D3：create 统一入口（生产 handler 与 gate 共用同一份语义）。</summary>
    public CreateEntityReceipt CreateEntity(string? name = null)
    {
        // D1：可执行性判断只看 Host 事实源。
        if (!_host.IsOpen || _host.State != HostRuntimeState.Ready)
        {
            // 文案"A no project"是 A1(PP-01)/A2 冻结的断言内容，不得改写。
            var refused = CreateEntityReceipt.Refused("no project");

            LogToConsole("+ Entity ignored: " + refused.Diagnostic);
            LastCreateReceipt = refused;
            return refused;
        }

        if (!TryHost("create entity", () => _host.CreateEntity(name), out int idx) || idx < 0)
        {
            // host 侧失败：诊断必须可见（typed failure 已由 TryHost 记录，这里补结构化回执）
            var failed = CreateEntityReceipt.Failed(_host.GetLastError());
            LogToConsole("CREATE FAILED: " + failed.Diagnostic);
            LastCreateReceipt = failed;
            return failed;
        }

        string created = _host.GetEntityName(idx) ?? $"#{idx}";
        // D2：只登记索引意图；不解析事件 payload，不假设事件到达时机。
        LastCreateOrder = Entities.Any(e => e.Index == idx)
            ? "event-before-return"      // 事件已送达：集合里能看到新实体
            : "return-before-event";     // 事件尚未送达（生产路径的常态）
        _pendingSelectionIndex = idx;
        // 顺序无关性由设计保证：解析点有两个 ——
        //   strict=false：create 返回后立刻试解（legacy 顺序，事件已先到，集合已是最新）；
        //   strict=true ：集合真正刷新时试解（EngineHost 顺序，事件后到）。
        // 两种顺序最终收敛到同一 selection，不依赖时序假设。
        ResolvePendingSelectionIntent(strict: false);
        RefreshDirtyState();
        LogToConsole("entity created: " + created);
        LastCreateReceipt = CreateEntityReceipt.Created(idx, created);
        return LastCreateReceipt;
    }

    /// <summary>
    /// A3/D2：解析并消费选择意图。幂等；**不伪造选择**。
    /// <paramref name="strict"/> 区分两个调用点的语义：
    /// <list type="bullet">
    /// <item>false（create 返回后）：集合可能仍是旧快照，目标暂时不在其中
    ///       —— 这是 EngineHost 的常态，意图必须**保留**给随后的刷新。</item>
    /// <item>true（刷新时）：集合已是最新，此时目标仍不存在说明意图确实不可满足
    ///       —— 丢弃，绝不落到无关实体上。</item>
    /// </list>
    /// </summary>
    private void ResolvePendingSelectionIntent(bool strict)
    {
        if (_pendingSelectionIndex is not int wanted) return;
        var hit = Entities.FirstOrDefault(e => e.Index == wanted);
        if (hit is null)
        {
            if (strict)
            {
                _pendingSelectionIndex = null;
                SelectionIntentResolved = null;   // 不可满足的意图被丢弃
                return;
            }
            return;                              // 保留意图，等真实刷新
        }
        _pendingSelectionIndex = null;
        // C1：选择意图解析必须**通知**控件（否则列表仍高亮旧项）。
        // 刷新由本方法显式完成，因此 core 不再重复刷新 Inspector。
        SetSelectedEntityCore(hit, refreshInspector: false);
        RefreshInspectorFromSession();
        SelectionIntentResolved = wanted;
    }

    /// <summary>P2-C2 兼容入口（HRC-1 gate 使用）；语义已收敛到 <see cref="CreateEntity"/>。</summary>
    public bool CreateEntityFromVm(string? name = null) => CreateEntity(name).Succeeded;

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
        if (ok)
        {
            // A3/D2：删除后索引失效，任何待解析意图都必须丢弃。
            _pendingSelectionIndex = null;
            SelectedEntity = null;
        }
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
        // accepted ≠ executed：在上一笔 pending edit 尚未结算前不再排队，
        // 否则 managed 侧的单笔回执跟踪会丢失前一笔的执行结果。
        if (PendingEditState == PendingEditStatus.Submitted)
        {
            LogToConsole("assign ignored: a pending edit is already awaiting its frame result");
            return false;
        }

        int assetIndex = _selectedAsset.Index;
        string assetPath = _selectedAsset.Path;
        int entityIndex = _selectedEntity.Index;
        string entityName = _selectedEntity.Name;

        bool accepted = _host.AssignSprite(assetIndex, entityIndex);
        if (!accepted)
        {
            LogToConsole("ASSIGN REJECTED: " + _host.GetLastError());
            return false;
        }

        // 只报告“已受理”。成功/失败必须等帧内执行回执（PendingEditCompleted）。
        NotePendingEditSubmitted($"assign sprite {assetPath} -> {entityName}");
        return true;
    }

    // ── P3-C：资产导入（File Dialog → ContentRegistry → 自动刷新）──
    /// path 为相对进程 CWD 的工程路径；type 0=Texture 1=Script。
    public bool ImportAssetFromPath(string path, int type)
    {
        if (!IsOpen)
        {
            LogToConsole("import ignored: no project");
            return false;
        }
        int idx = _host.ImportAsset(path, type);
        if (idx < 0)
        {
            LogToConsole("IMPORT FAILED: " + _host.GetLastError());
            return false;
        }
        ReloadCollectionsFromSession();   // EV_ASSET_IMPORTED 亦触发，双保险
        LogToConsole($"asset imported: {path} (#{idx})");
        return true;
    }

    // ── P3-C：资产重命名（GUID 不变 → 绑定稳定）──
    public bool RenameAssetFromVm(int assetIndex, string newName)
    {
        if (!IsOpen)
        {
            LogToConsole("rename asset ignored: no project");
            return false;
        }
        bool ok = _host.RenameAsset(assetIndex, newName);
        LogToConsole(ok ? $"asset renamed: {newName}"
                        : "RENAME ASSET FAILED: " + _host.GetLastError());
        if (ok) ReloadCollectionsFromSession();
        return ok;
    }

    // ── P3-B：Assign Script 到选中实体（契约内实体级脚本绑定）──
    public bool AssignScriptToSelected()
    {
        if (_selectedEntity is null || _selectedAsset is null)
        {
            LogToConsole("assign script ignored: need entity + asset selection");
            return false;
        }
        bool ok = _host.AssignScript(_selectedAsset.Index, _selectedEntity.Index);
        LogToConsole(ok ? $"assigned script {_selectedAsset.Path} -> {_selectedEntity.Name}"
                        : "ASSIGN SCRIPT FAILED: " + _host.GetLastError());
        if (ok) RefreshInspectorFromSession();   // 刷新 SelectedScript
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
        ScriptError = "";
        Raise(nameof(HasScriptOpen));
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

    // ── P3-D 运行时：Play / Reload / Stop（D3 保存→重载生效 + _PERSIST；D4 错误恢复）──

    /// 从指定脚本资产启动运行时；assetIndex<0 = 项目 director。
    public bool PlayScript(int assetIndex = -1)
    {
        if (!IsOpen)
        {
            LogToConsole("Play ignored: no project");
            return false;
        }
        // D4：若脚本还有未保存编辑，先落盘再 Play，避免运行陈旧内容
        if (ScriptDirty && _scriptAssetIndex >= 0)
        {
            LogToConsole("Play: saving unsaved script edits first");
            SaveScript();
        }
        // A2/F2-05：Play 的 native typed failure 不得逃出 handler；失败已由 TryHost 呈现为 UI 状态。
        if (!TryHost("play", () => _host.Play(assetIndex), out bool ok))
        {
            IsRunning = false;
            return false;
        }
        if (ok)
        {
            IsRunning = true;
            ScriptError = "";
            LogToConsole($"PLAY started (script asset#{(assetIndex < 0 ? "<director>" : assetIndex.ToString())})");
        }
        else
        {
            ScriptError = _host.GetRuntimeError();
            if (ScriptError.Length == 0) ScriptError = _host.GetLastError();
            LogToConsole("PLAY FAILED: " + _host.GetRuntimeError() + " | " + _host.GetLastError());
        }
        return ok;
    }

    /// 热重载当前运行脚本：保留 _PERSIST，随后重新 OnCreate。
    public bool ReloadActiveScript()
    {
        if (!IsRunning)
        {
            LogToConsole("Reload ignored: not playing");
            return false;
        }
        if (!TryHost("reload", () => _host.Reload(), out bool ok))
        {
            IsRunning = false;
            return false;
        }
        if (ok)
        {
            ScriptError = "";
            LogToConsole("RELOAD ok (persist preserved)");
        }
        else
        {
            ScriptError = _host.GetRuntimeError();
            LogToConsole("RELOAD FAILED: " + _host.GetRuntimeError());
        }
        return ok;
    }

    public void StopPlaying()
    {
        if (!IsRunning) return;
        // A2/F2-05：faulted host 上 Stop 会返回 typed ShutdownFailed；
        // 失败也必须结束"运行中"的 UI 状态（runtime 已不可用），并显式上报。
        if (!TryHost("stop", _host.Stop))
        {
            IsRunning = false;
            return;
        }
        IsRunning = false;
        ScriptError = "";
        LogToConsole("STOP -> edit state restored");
    }

    /// P3-D D3b：运行态 `_PERSIST[key]` 整型探针（非运行/取不到返回 default）
    public int RuntimePersistInt(string key, int defaultVal)
    {
        if (!IsRunning) return defaultVal;
        return _host.GetRuntimePersistInt(key, defaultVal);
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

    /// <summary>A7-C04/G2-05：事件必须已在 UI 线程消费；越界即视为契约违例。</summary>
    public int OffUiThreadEventCount { get; private set; }
    public int ConsumedEventCount { get; private set; }

    /// <summary>C1：请求已受理（尚未执行）。不得在此处报告成功。</summary>
    public void NotePendingEditSubmitted(string description)
    {
        _pendingEditDescription = description;
        PendingEditState = PendingEditStatus.Submitted;
        LogToConsole($"PENDING EDIT SUBMITTED: {description} "
                     + "(accepted; execution result arrives on a later frame)");
    }

    /// <summary>
    /// C2：帧内执行回执。只有 executed=true 才更新为“已完成”；
    /// 失败时诊断进入 ScriptError / Console，并由 View 同步到状态栏。
    /// </summary>
    public void CompletePendingEdit(bool executed, string diagnostic)
    {
        string description = _pendingEditDescription.Length > 0
            ? _pendingEditDescription
            : "pending edit";
        _pendingEditDescription = "";

        if (executed)
        {
            PendingEditState = PendingEditStatus.Executed;
            // 不清空 ScriptError：它可能属于无关的 Lua / Play 诊断，不能被一次成功覆盖。
            StatusText = $"pending edit executed: {description}";
            LogToConsole($"PENDING EDIT EXECUTED: {description}");
            return;
        }

        PendingEditState = PendingEditStatus.Failed;
        string text = string.IsNullOrWhiteSpace(diagnostic)
            ? "native execution failed without a diagnostic"
            : diagnostic;
        ReportPendingEditFailure($"{description}: {text}");
    }

    /// <summary>A7-C03：GL 依赖 edit 在帧内失败时的对外报告入口。</summary>
    public void ReportPendingEditFailure(string diagnostic)
    {
        PendingEditState = PendingEditStatus.Failed;
        ScriptError = diagnostic;
        StatusText = "pending edit failed (see console)";
        LogToConsole("PENDING EDIT FAILED: " + diagnostic);
    }

    private void ResetPendingEditState()
    {
        _pendingEditDescription = "";
        PendingEditState = PendingEditStatus.Idle;
    }

    // ── Slice D：production viewport pick 投影（D-C4）───────────────
    //
    // 与 B 的四态同构：**受理 ≠ 选中**。D3 的 PICK 是 request → 下一帧 → fetch，
    // 因此"Dispatch 返回 Accepted" 只能把状态推到 Pending，绝不能改选中。

    /// <summary>D-C4：pick 的在途状态。</summary>
    public enum PickRequestState
    {
        /// <summary>没有在途请求。</summary>
        Idle,
        /// <summary>native 已受理，结果将在下一次 pump 帧之后产出。</summary>
        Pending,
        /// <summary>已有在途请求，本次点击被 native 拒绝（PICK_BUSY），在途请求不被替换。</summary>
        Busy,
    }

    /// <summary>D-C5：身份解析 seam（v1.4 起由 native <c>entity_index</c> 驱动）。</summary>
    public IPickIdentityResolver PickIdentity { get; set; } = new PacketPickIdentityResolver();

    private PickRequestState _pickState = PickRequestState.Idle;
    public PickRequestState PickState
    {
        get => _pickState;
        private set => Set(ref _pickState, value);
    }

    /// <summary>在途请求的配对键（native <c>operation_id</c>）；Idle 时为 0。</summary>
    public ulong PendingPickRequestId { get; private set; }

    /// <summary>最近一次被消费的 pick 结果所属帧（D3-C4 的 frame identity）。</summary>
    public ulong LastPickFrameId { get; private set; }

    /// <summary>最近一次 pick 的结果分类，供 gate / 诊断机械断言。</summary>
    public string PickOutcome { get; private set; } = "(no pick yet)";

    /// <summary>最近一次命中的 native entity id（= <c>GameObject::ID</c>），未命中为 null。</summary>
    public int? LastPickedEntityId { get; private set; }

    /// <summary>
    /// 最近一次命中结果里 native 解析出的**场景索引**（v1.4 <c>entity_index</c>）。
    /// 未命中 / 无法解析时为 -1。它与 <see cref="LastPickedEntityId"/> 是**不同的身份空间**，
    /// 刻意分开暴露，使 gate 能断言"ID 不变而索引变了"。
    /// </summary>
    public int LastPickEntityIndex { get; private set; } = -1;

    /// <summary>成功把 "hit" 落到选中项上的次数（仅当身份 seam 可用时才能增长）。</summary>
    public int PickHitsApplied { get; private set; }

    /// <summary>
    /// D-C4：提交一次 pick 请求。canonical 像素坐标由 <see cref="ViewportPickMapper"/> 产出，
    /// 本方法**不**做坐标变换。
    /// </summary>
    public bool RequestViewportPick(float x, float y)
    {
        if (!IsOpen)
        {
            LogToConsole("pick ignored: no project");
            return false;
        }
        // PICK 是 EngineHost 能力。legacy 后端上 `RequestPick` 会抛
        // HostCapabilityNotAvailableException（NotSupportedException），
        // 那不是 TryHost 捕获的宿主失败类型 —— 在入口就显式拒绝，
        // 绝不让它从 pointer handler 逃进 XAML 反射派发。
        if (_host.Backend != HostBackend.EngineHost)
        {
            LogToConsole("pick ignored: PICK is an EngineHost capability (legacy backend has no pick pass)");
            return false;
        }
        if (PickState == PickRequestState.Pending)
        {
            // 单笔在途：不排队、不替换（与 assign 的单笔回执同一纪律）。
            LogToConsole($"pick ignored: request #{PendingPickRequestId} is still awaiting its frame result");
            return false;
        }

        PickRequestResult request;
        if (!TryHost("request pick", () => _host.RequestPick(x, y), out request))
        {
            PickOutcome = "request failed";
            return false;
        }

        if (request.Busy)
        {
            PickState = PickRequestState.Busy;
            PickOutcome = "busy";
            LogToConsole("PICK BUSY: an outstanding request was not replaced");
            return false;
        }
        if (!request.Accepted)
        {
            PickOutcome = "rejected";
            LogToConsole($"PICK REJECTED (status={request.Status})");
            return false;
        }

        PickState = PickRequestState.Pending;
        PendingPickRequestId = request.RequestId;
        PickOutcome = "accepted";
        // 只报告“已受理”。选中变化必须等帧内结果（D-C4）。
        LogToConsole($"PICK SUBMITTED: ({x:0.##},{y:0.##}) -> request #{request.RequestId} "
                     + "(accepted; selection is unchanged until the frame result arrives)");
        return true;
    }

    /// <summary>
    /// D-C4：在 pump 发布的帧上取回 pick 结果。
    /// <paramref name="presentedFrameId"/> 用于 D3-C4 的 stale-frame 判别（0 = 尚无可比帧）。
    /// 返回 <c>true</c> 表示**确实消费了一个结果**（不论命中与否）。
    /// </summary>
    public bool TryConsumeViewportPick(ulong presentedFrameId)
    {
        if (PickState != PickRequestState.Pending) return false;

        PickFetchResult fetch;
        if (!TryHost("fetch pick result", () => _host.TryFetchPickResult(), out fetch))
        {
            return false;
        }

        if (!fetch.HasResult)
        {
            if (fetch.IsInvalidatedByTeardown)
            {
                // 依冻结契约：teardown 后不产生 packet，且旧结果不得回流。
                ResetPickState("invalidated by teardown");
                LogToConsole("PICK DROPPED: the outstanding request was invalidated by teardown");
                return true;
            }
            // 已受理但尚未产出：继续保持 Pending（不阻塞 UI、不轮询等待）。
            return false;
        }

        var result = fetch.Result!;
        PickState = PickRequestState.Idle;
        PendingPickRequestId = 0;
        LastPickFrameId = result.FrameId;
        LastPickEntityIndex = result.HasResolvedIndex ? (int)result.EntityIndex : -1;

        // D3-C4：stale 结果必须显式区分，不得当当前帧用。
        if (presentedFrameId != 0 && result.FrameId < presentedFrameId)
        {
            PickOutcome = "stale-frame";
            LogToConsole($"PICK STALE: result frame {result.FrameId} < presented frame {presentedFrameId}"
                         + " (rejected, the selection is unchanged)");
            return true;
        }

        if (result.IsHit)
        {
            ApplyPickHit(result);
            return true;
        }

        if (result.IsNoHit)
        {
            // 正常结果（该像素无实体）。语义上等价于“点空白” ⇒ 取消选择。
            PickOutcome = "no-hit";
            LastPickedEntityId = null;
            if (SelectedEntity is not null)
            {
                SelectedEntity = null;
                LogToConsole($"PICK NO HIT (frame {result.FrameId}): clicked empty space, selection cleared");
            }
            else
            {
                LogToConsole($"PICK NO HIT (frame {result.FrameId}): clicked empty space");
            }
            return true;
        }

        // 查询无效 / 未就绪 / fault：都**不得**改变选中（D-C4）。
        PickOutcome = DescribePickFailure(result);
        LastPickedEntityId = null;
        LogToConsole($"PICK NOT APPLIED: {PickOutcome} (frame {result.FrameId}; the selection is unchanged)");
        return true;
    }

    private void ApplyPickHit(PickResult result)
    {
        uint entityId = result.EntityId;

        // D3-C2：有效 id 恒 >= 1。0 是 reserved，命中 0 属协议违例。
        if (entityId < 1)
        {
            PickOutcome = "protocol-violation";
            LastPickedEntityId = null;
            LogToConsole($"PICK PROTOCOL VIOLATION: hit with entity id {entityId} (valid ids are >= 1)");
            return;
        }

        LastPickedEntityId = (int)entityId;

        // D-C5：身份解析只经 seam。v1.4 起索引来自 packet（native 在该帧用
        // Scene::FindByID 解析），**不再**用 entityId - 1 推导。
        if (!PickIdentity.TryResolve(result, out int sceneIndex, out string diagnostic))
        {
            PickOutcome = "hit-unresolved";
            LogToConsole($"PICK HIT UNRESOLVED: {diagnostic} (the selection is unchanged)");
            return;
        }

        var hit = Entities.FirstOrDefault(e => e.Index == sceneIndex);
        if (hit is null)
        {
            // D3-C6 StaleEntity：帧与结果之间实体被删除，或投影尚未追上。
            // 索引是"结果产生时"的快照位置，因此这里必须按当前投影再校验一次。
            PickOutcome = "stale-entity";
            LogToConsole($"PICK STALE ENTITY: id {entityId} -> index {sceneIndex}, "
                         + "which is not in the current projection (the selection is unchanged)");
            return;
        }

        SelectedEntity = hit;
        PickHitsApplied++;
        PickOutcome = "hit";
        LogToConsole($"PICK HIT: id {entityId} -> index {sceneIndex} '{hit.Name}' "
                     + $"(frame {result.FrameId}, selected)");
    }

    private static string DescribePickFailure(PickResult result)
        => result.IsOutsideViewport ? "outside-viewport"
         : result.IsInvalidCoordinate ? "invalid-coordinate"
         : result.IsNotReady ? "not-ready"
         : result.IsFault ? "fault"
         : $"unknown-status-{result.Status}";

    private void ResetPickState(string reason)
    {
        PickState = PickRequestState.Idle;
        PendingPickRequestId = 0;
        LastPickedEntityId = null;
        LastPickEntityIndex = -1;
        PickOutcome = $"idle ({reason})";
    }

    // ── A2/F2-05：host 失败 → 可观测 UI 状态 ─────────────────

    /// <summary>
    /// 把 host 侧的显式失败（typed native failure / 三态 gate / 已释放）转成 Console + 状态栏。
    /// 绝不向上抛：View 边界不允许异常逃进 XAML 反射派发。
    /// 返回 <c>false</c> 表示调用失败，调用方据此走自己的失败分支。
    /// </summary>
    private bool TryHost<T>(string operation, Func<T> call, out T value)
    {
        try
        {
            value = call();
            return true;
        }
        catch (Exception ex) when (ex is HostOperationException
                                       or HostRuntimeNotReadyException
                                       or HostRuntimeUnavailableException
                                       or ObjectDisposedException)
        {
            ReportHostFailure(operation, ex);
            value = default!;
            return false;
        }
    }

    private bool TryHost(string operation, Action call)
    {
        try
        {
            call();
            return true;
        }
        catch (Exception ex) when (ex is HostOperationException
                                       or HostRuntimeNotReadyException
                                       or HostRuntimeUnavailableException
                                       or ObjectDisposedException)
        {
            ReportHostFailure(operation, ex);
            return false;
        }
    }

    /// <summary>A2/F2-05：结构化失败在 UI 上的唯一呈现点（Console + 状态栏 + ScriptError）。</summary>
    public void ReportHostFailure(string operation, Exception error)
    {
        string text = error is HostOperationException typed
            ? $"{operation} failed: {HostOperationException.StatusText(typed.Status)}"
              + $" / {HostOperationException.StageText(typed.Stage)}"
              + (typed.Diagnostic.Length > 0 ? $" — {typed.Diagnostic}" : string.Empty)
            : $"{operation} failed: {error.Message}";
        LogToConsole(text);
        StatusText = $"{operation} failed (see console)";
        ScriptError = text;
    }

    /// <summary>pump/runtime 故障：runtime 已不可用，UI 必须停止假装在运行。</summary>
    private void OnRuntimeFaulted(HostOperationException failure)
    {
        LogToConsole("RUNTIME FAULTED: " + failure.Message);
        ScriptError = failure.Message;      // 诊断必须保留（D1：清投影 ≠ 吞诊断）
        IsRunning = false;
        // A3/D1：Fault → 投影清空，不得把已失效 project 呈现为"当前"。
        ReconcileWithHostState("runtime faulted");
    }

    /// <summary>会触发 host 查询的事件类型；project 未打开时必须跳过。</summary>
    private static bool RequiresOpenProject(int type) => type switch
    {
        EditorBridgeApi.EvProjectLoaded => false,
        EditorBridgeApi.EvPlayStarted => false,
        EditorBridgeApi.EvPlayStopped => false,
        _ => true,
    };

    private void OnNativeEvent(int type, string payload)
    {
        if (!Dispatcher.UIThread.CheckAccess())
        {
            OffUiThreadEventCount++;
            LogToConsole("EVENT OFF UI THREAD (A7-C04 violation): " + type);
            return;
        }
        // A7-C04：teardown / 未打开 project 时到达的刷新类事件不得触碰 runtime。
        // A2/F2-05：runtime 已 fault 时同样不得刷新（host 会显式拒绝，且会把 UI 清空成假状态）。
        // 这里显式记录而不是让三态 gate 的异常冒泡到 UI 线程（不得静默 no-op）。
        if (RequiresOpenProject(type) && (!_host.IsOpen || _host.State != HostRuntimeState.Ready))
        {
            LogToConsole($"EV {type} ignored: no usable project (host state={_host.State}, open={_host.IsOpen})");
            // A3/D1：忽略事件的同时保证投影已经是空的（幂等）——
            // 否则会出现"事件被挡住但旧 project 仍显示"的 stale state。
            // 终态 Dispose 之后除外：A2/F2-08 要求保留最后状态快照。
            if (!_host.IsDisposed)
                ClearProjectProjection($"event {type} ignored (host state={_host.State})");
            return;
        }
        ConsumedEventCount++;
        switch (type)
        {
            case EditorBridgeApi.EvProjectLoaded:
                LogToConsole("EV ProjectLoaded: " + payload);
                break;
            case EditorBridgeApi.EvEntityCreated:
                LogToConsole("EV EntityCreated: " + payload);
                ReloadCollectionsFromSession();          // 单一事实源刷新
                RefreshDirtyState();                     // 创建即脏：DIRTY 徽标必须随之点亮
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
            case EditorBridgeApi.EvEntityScriptAssigned:
                LogToConsole("EV EntityScriptAssigned: " + payload);
                RefreshInspectorFromSession();
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvAssetImported:
                LogToConsole("EV AssetImported: " + payload);
                ReloadCollectionsFromSession();
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvAssetRenamed:
                LogToConsole("EV AssetRenamed: " + payload);
                ReloadCollectionsFromSession();
                RefreshDirtyState();
                break;
            case EditorBridgeApi.EvPlayStarted:
                LogToConsole("EV PlayStarted: " + payload);
                IsRunning = true;
                break;
            case EditorBridgeApi.EvPlayStopped:
                LogToConsole("EV PlayStopped");
                IsRunning = false;
                break;
            case EditorBridgeApi.EvComponentChanged:
                LogToConsole("EV ComponentChanged: " + payload);
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

        // A2/F2-05：refresh 期间 host 可能已 fault/关闭；查询失败必须转成 Console 记录，
        // 而不是让异常从 View handler 逃出（旧行为是 XAML 反射派发包成 TargetInvocationException）。
        if (!TryHost("refresh collections", () => _host.GetCounts(), out var counts))
        {
            RebuildFilteredEntities();
            RebuildFilteredAssets();
            return;
        }
        var (entCount, assetCount) = counts;
        Entities.Clear();
        for (int i = 0; i < entCount; i++)
            Entities.Add(new EntityVm(i, _host.GetEntityName(i) ?? $"?{i}"));

        Assets.Clear();
        for (int i = 0; i < assetCount; i++)
            Assets.Add(new AssetVm(i, _host.GetAssetPath(i) ?? "",
                _host.GetAssetType(i) switch { 0 => "Texture", 1 => "Script", _ => "Unknown" },
                _host.GetAssetGuid(i)));

        // A3/D2：选择意图优先于"保持旧选择"。
        //
        // Commit C1：解析必须在**最终**过滤列表重建**之后**完成。控件（Hierarchy）
        // 绑定的是 FilteredEntities 里的实例，而刷新会重建这些实例；先解析再重建列表
        // 会让控件收到一个已经不在集合里的旧实例，OneWay 绑定随即回退到"无选中"。
        // 因此这里先重建列表（意图待解析时不保留旧选择），再在最终列表上解析。
        RebuildFilteredEntities(preserveSelection: _pendingSelectionIndex is null);
        RebuildFilteredAssets();     // P3-C：资产过滤视图同步

        if (_pendingSelectionIndex is not null)
        {
            // strict=true：集合此刻已是最新，目标不在其中即视为不可满足并丢弃
            // （不落到无关实体上）。
            ResolvePendingSelectionIntent(strict: true);
        }
        else if (keepIdx is int idx && _selectedEntity is null)
        {
            // 旧选择被过滤隐藏：模型层保留它（P3-A 契约），不伪造到其它实体上。
            LogToConsole($"selection #{idx} is filtered out; the model keeps it");
        }

        StatusText = $"[Avalonia] project loaded: {entCount} objects, {assetCount} assets";
        LogToConsole(StatusText);
        DerivedStateChanged?.Invoke();
    }

    /// P3-A：按 SearchText 重建过滤视图。
    /// 选择状态契约：过滤把选中项藏掉时模型层保留选择，
    /// 清空过滤后自动恢复；选中项仍在结果集则保持。
    ///
    /// <paramref name="preserveSelection"/>：列表重建后是否把"模型当前选择"重新
    /// 指向新实例。刷新集合时必须为 true（实例已换）；等待选择意图解析时必须为
    /// false（让意图决定最终选择，不要先落到旧选择上）。
    private void RebuildFilteredEntities(bool preserveSelection = true)
    {
        int? keepIdx = _selectedEntity?.Index;
        FilteredEntities.Clear();
        foreach (var e in Entities)
            if (MatchesFilter(e)) FilteredEntities.Add(e);

        if (preserveSelection && keepIdx is int idx)
        {
            var hit = FilteredEntities.FirstOrDefault(e => e.Index == idx);
            if (hit is not null)
            {
                // C1：实例已被重建，即使值相等也必须通知 —— 否则控件仍指着旧实例，
                // 而旧实例已不在 ItemsSource 里。
                SetSelectedEntityCore(hit, refreshInspector: false, forceNotify: true);
                RefreshInspectorFromSession();
            }
        }
        Raise(nameof(HasFilter));
        Raise(nameof(FilteredCount));
    }

    private void RefreshInspectorFromSession()
    {
        if (_selectedEntity is null)
        {
            SelectedName = "(none)";
            SelectedSprite = "(none)";
            SelectedScript = "(none)";
            RefreshCamera(null);
        }
        else if (!TryHost("refresh inspector",
                     () => _host.GetEntityPosition(_selectedEntity.Index), out var p))
        {
            // runtime 不可用：保留上一次回填值，不伪造新状态。
            RefreshCamera(null);
        }
        else
        {
            SelectedName = _selectedEntity.Name;
            PosX = Format(p.x); PosY = Format(p.y); PosZ = Format(p.z);
            SelectedSprite = _host.GetEntitySprite(_selectedEntity.Index);
            if (SelectedSprite.Length == 0) SelectedSprite = "(none)";
            SelectedScript = _host.GetEntityScript(_selectedEntity.Index);
            if (SelectedScript.Length == 0) SelectedScript = "(none)";
            RefreshCamera(_selectedEntity.Index);
        }
    }

    /// F1：从会话读取 Camera 反射属性（存在则回填 Inspector 字段）
    private void RefreshCamera(int? entityIndex)
    {
        if (entityIndex is not int idx
            || !TryHost("refresh camera", () => _host.HasComponent(idx, "Camera"), out bool has)
            || !has)
        {
            HasCamera = false;
            return;
        }
        HasCamera = true;
        CameraEnabled = _host.GetComponentProperty(idx, "Camera", "enabled") switch
        {
            "false" => "false", _ => "true"
        };
        var zoom = _host.GetComponentProperty(idx, "Camera", "zoom");
        CameraZoom = zoom.Length == 0 ? "1" : zoom;
        var vp = _host.GetComponentProperty(idx, "Camera", "viewport");
        CameraViewport = vp.Length == 0 ? "0 0 1280 720" : vp;
        var b = _host.GetComponentProperty(idx, "Camera", "bounds");
        CameraBounds = b.Length == 0 ? "" : b;
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

    public void Dispose()
    {
        _host.NativeEvent -= OnNativeEvent;
        _host.RuntimeFaulted -= OnRuntimeFaulted;
        if (!_host.IsDisposed)
            ResetSession();
    }
}

public sealed record EntityVm(int Index, string Name);

public sealed record AssetVm(int Index, string Path, string TypeName, string Guid);
