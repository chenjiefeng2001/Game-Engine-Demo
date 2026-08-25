# GP1-DX 统一状态层缺失分析报告

> 日期：2026-08-25 · 基线：`a4d6842`（UI 审计 P1-a/b 修复后）
> 触发：人工 Run 前架构观察——"各组件之间的状态和数据没有完整统一的层"
> 方法：三路全文源码调查（场景所有权 / 选择与 UI 镜像态 / 内容工程数据），
> 关键论断逐条源码复核。前置证据：`docs/GP1-DX-UI-Audit.md`。

---

## 0. 结论摘要

**该判断成立，且比表象更严重。** 当前编辑器不存在任何形式的"编辑器状态层"：

```text
7   个独立的"当前场景"持有点
4   个实体选择持有者（10 组写入点）
3   套互不相通的每帧数据通路（渲染源 / Update 驱动 / UI 数据源）
2   套并存的 PIE 播放器（可叠加成三层场景）
3   套日志缓冲（其一只写不读）
2   个 StatusBar 实例同绘同一窗口 ID
0   个统一的不变量或仲裁机制
```

唯一的同步手段是 GP01 会话的一个**四路手工重接 lambda**（其中 SceneViewerPanel
一路实效），加上一条双通道冗余的 EventBus。任何交叉操作序列（GP01 Play ×
引擎 PIE × New/Open Scene × 旁路增删）都会使至少一个持有点与其余持有点指向
不同的 Scene 对象——这不是潜在风险，是当前代码的结构性属性。

---

## 1. 状态域盘点地图

### 1.1 场景所有权域（最严重）

**7 个场景级持有点**：

| # | 持有者.成员 | 类型 | 写入时机 |
|---|------------|------|---------|
| 1 | `EditorDemoApp::m_Scene`（EditorDemoApp.h:524） | shared_ptr | BuildTestScene / onSceneReplaced |
| 2 | `GP01ProductionSession::m_EditScene`（:351） | shared_ptr | LoadProject |
| 3 | `GP01ProductionSession::m_Runtime`（:352） | shared_ptr | Play 克隆 / Stop reset |
| 4 | `EditorSceneManager::m_EditorScene`（EditorSceneManager.h:111） | **裸指针** | 宿主重接（被 GP01 劫持） |
| 5 | `EditorSceneManager::m_RuntimeScene`（:112） | shared_ptr | 引擎 PIE JSON 克隆 |
| 6 | `SceneHierarchyPanel::m_Scene`（SceneHierarchyPanel.h:71） | **裸指针** | 四路重接第 2 路 / PIE 切换 |
| 7 | `GameplayAPI::BridgeState::scene`（GameplayAPI.cpp:34） | **全局静态裸指针** | GP01 Reset/SetScene |

**三条互不相通的每帧通路**：

| 通路 | 数据源 | 归谁管 |
|------|--------|--------|
| 视口渲染像素 | `m_GP01.RenderScene()`（EditorDemoApp.h:245/451） | GP01 会话 |
| 每帧 `Scene::Update(dt)` | 仅引擎 PIE 克隆被驱动（EngineEditor.cpp:407-414）；GP01 运行态**从不经过 Scene::Update**，仅 Lua director Tick | 两套各半 |
| 拾取解析/右键创建删除/New-Open-Save | `EditorSceneManager.GetScene()`（EngineEditor.cpp:130-245） | EditorSceneManager |

渲染源与交互源仅在"GP01 正在 Play"这一偶然条件下恰好一致；其余时刻是两个
不同的 Scene 对象。

### 1.2 实体选择域

**4 个持有者**：HierarchyPanel.m_Selected（裸）/ InspectorPanel.m_Target（裸）/
ViewportPanel.m_SelectedObject（weak）/ EngineEditor.m_SelectedObject（weak 中枢）。
生命周期语义三种并存：对象销毁时裸指针悬垂、weak 静默过期、无任何失效通知。

**同步 = 双通道冗余**：EventBus `EntitySelectedEvent`（8 发布点 × 2 订阅点）+
`RegisterSceneHierarchy` 直调回调。一次 Hierarchy 点击，中枢逻辑跑两遍；
New/Open Scene 清理漏掉 Inspector/Viewport 两路；Inspector 的 m_Locked 在
订阅端吞事件且不回放；PIE 切换还有第三套按 ID 手写重建（因克隆不保留 ID 而
必然失败，见 R3）。

### 1.3 UI 镜像态域

| 状态域 | 持有者 | 断链 |
|--------|--------|------|
| gizmo 工具/吸附 | Toolbar 一套 + ViewportPanel 一套 | Toolbar 八个桥接口全仓零调用；真实状态在 ViewportPanel |
| 日志缓冲 | ① ConsoleLog 512 槽（只写不读，LOG_* 宏直写绕过 spdlog）② ConsolePanel 10000 条（事实正身）③ MemoryHistorySink 2048 条（崩溃 dump，合法） | 宏日志永不上屏；①应死 |
| Undo | UndoManager 单例 + Inspector recordUndo 回调 + 菜单 UndoState | 三重断线：记录端回调无人注册 / 执行端 FindTarget 桩返 nullptr / UI 端 canUndo 永灰 |
| 键位表 | UserSettings（默认 play=F5 等） | 整类零消费者；实际键位硬编码散落 6 处 |
| 输入阻塞 | Input::SetBlockInput 静态位 | ≥6 个写入方按帧内顺序竞态，最后写者胜，无仲裁 |
| StatusBar | EngineEditor 实例 + EditorDemoTest 实例 | 同绘 `##MainStatusBar` 同一 ID；TaskManager::Init 零调用死挂；通知 push 即泄漏 |
| PlayState | EditorSceneManager 权威 + Toolbar 乐观自写 | Play 失败卡假 Playing 态 |

### 1.4 内容/工程数据域

**全部生产文档数据活在 sandbox 会话类的私有成员里**：
m_Reg（ContentRegistry）/ m_Bindings（按索引对齐的绑定表）/ m_TexMgr +
m_Gfx（会话自有 TextureManager + OpenGLGraphicsFactory）/ m_ScenePath +
m_ManifestPath（路径记忆）。

后果：
- 引擎面板（ContentBrowserPanel / AssetBrowserPanel / Inspector）对它们零感知，
  各自另立数据源；
- 引擎另有平行资产体系 `EditorAssetDatabase`（AssetDB，Root assets//Library/），
  与 ContentRegistry 功能重叠、互不知情——两套 GUID 原语（Engine::GUID vs
  ResourceGUID）连剪贴板互拷都无法互相解析；
- TextureManager 进程内 2 个活实例（宿主 + 会话）：缓存互不可见（同文件跨实例
  重复上传 VRAM，现仅 1 张 test.png 侥幸踩线）；bindless 索引单调发放不回收；
  热重载跨实例不可见；**会话自有 OpenGLGraphicsFactory 析构时 glfwTerminate()
  会终止整个进程 GLFW**——成员析构序上的未爆弹；
- 绑定表按对象下标对齐，任何旁路增删（DND/Hierarchy 删除）即错位，
  RealignBindings 只补尾部 Null 不做身份校验（DL-02 家族结构性根源）。

---

## 2. 已证实失效案例索引

| 编号 | 失效 | 来源 |
|------|------|------|
| R1 | 双 PIE 叠加成三层场景（GP01 Play 中按工具栏 ▶） | 本调查（双方 Play 均无互斥检查） |
| R2 | 渲染源与拾取/树数据源永久分裂 | AUD §2 主线 + 本调查 |
| R3 | JSON 克隆不保留 GameObject ID → PIE 选中迁移必失败、拾取 ID 空间错位 | Serializer.cpp 无 id 字段（复核✓） |
| R4 | GP01 Play 中写操作蒸发不设防（仅 Assign 有拦截；Hierarchy 创建删除/Inspector/DND/gizmo 全部裸奔） | AUD-2 同族 |
| R5 | 绑定表索引对齐脆弱（DL-02 结构性根源） | DL-02 + 本调查 |
| R6 | 四路重接之一（SceneViewerPanel 快照）存活不到一帧 | RefreshSceneData 先 clear 后读空系统 |
| R7 | Stop 无条件回调 + LoadProject 双发 onSceneReplaced（GP01ProductionSession.h:154，复核✓） | 本调查 |
| R8 | 死机制虚假安全感：SceneSwitchedEvent 无发布者 / SetActiveScene 零调用致 Undo 静默丢弃 / Core SceneManager 空转 | 本调查 |
| R9 | 选择 stale 保留策略：OnSelectionChanged 无 else 分支，弱引用过期后 Gizmo 消失而 Inspector 残留旧目标 | 本调查 |
| R10 | Hierarchy pending 强引用延长已删对象寿命，SetScene 后可打到旧场景 | 本调查 |
| R11 | New/Open Scene 不通知 GP01 → Save Project 可能写出错误场景数据 | 本调查（双向不知情） |

---

## 3. 根因分析

**RC1 · 文档概念不存在。** "正在编辑的工程"（Registry+Bindings+EditScene+Paths）
没有任何 C++ 抽象，散落在 sandbox 会话私有成员。引擎面板想要数据只能自建
（AssetDatabase/浏览器自扫盘），于是平行体系必然出现。

**RC2 · 同步靠手工接线，无不变量守护。** 四路 lambda、双通道 EventBus、PIE 第三套
ID 重建——每个都是点对点补丁，没有一个全局断言保证"各视图指向同一个场景"。
接线遗漏（R6/R9/清理漏两路）无人能发现。

**RC3 · 生命周期语义三态混用。** 裸指针/shared/weak 并存于同一逻辑状态
（选择、场景），对象销毁后行为各异：悬垂 UB / 静默清空 / 强引用续命。
无 generation 校验、无失效广播。

**RC4 · 模式（Edit/Play）不是一等概念。** 两套 PIE 各自定义播放，互不感知；
"当前活跃场景是什么"的答案取决于查询者走哪条通路。R1 三层叠加是其极端表现。

**RC5 · 能力层与宿主耦合倒置。** 生产闭环能力（Play/Save/Assign）本应是引擎层
服务，现在长在 demo 宿主头文件里；引擎反过来又被宿主重接裸指针（m_EditorScene
被劫持）。层级边界模糊使每次修复都在错误的一侧打补丁。

---

## 4. 提案：EditorContext 统一层（三层服务 + 两条纪律）

> 定位：不是大重构，是把已存在但离散的真相收编进三个小服务。
> 全部可在现有冻结契约之上实现，不改 GameplayAPI/序列化格式。

### 4.1 ProjectDocument（工程文档，内容数据唯一真相）

```text
class ProjectDocument {
    ContentRegistry      Registry;     // 收编 session.m_Reg（唯一实例）
    vector<Binding>      Bindings;     // 改为 GUID→身份映射，弃纯下标对齐（R5 根治）
    std::shared_ptr<Scene> EditScene;  // 唯一编辑态场景
    std::string          ManifestPath, ScenePath;
    TextureManager&      Tex();        // 单实例（复用宿主的，删会话自有 Gfx，拆 glfwTerminate 弹）
};
```
收编清单：GP01ProductionSession 五个数据成员；AssetDatabase 与 ContentRegistry
**暂不合并**（P2 再议），但 AssetBrowser 的 GUID 显示改走 Registry 解析。
受益面：AUD-2 类拦截从会话技巧变成文档层不变量（非编辑态拒绝写绑定）。

### 4.2 PlaybackController（播放状态机，模式一等化）

```text
enum class Mode { Edit, Playing };
class PlaybackController {          // 全局唯一 PIE，双轨合一或显式禁一
    Mode  Current();
    void  EnterPlay();   // 内部决定克隆策略；与 ProjectDocument 协商快照
    void  ExitPlay();    // 整体丢弃运行态 + 广播 ModeChanged
    Scene* ActiveScene(); // 渲染/Update/交互的唯一权威答案
};
```
收编清单：GP01.Play/Stop 与 EditorSceneManager.Play/Stop 二选一保留（建议保
GP01 内容管线，引擎 PIE 显式禁用于本宿主）；Toolbar PlayState 改为只读镜像；
onSceneReplaced 四路 lambda 由一个 ModeChanged 广播替代（R7 双发随之消失）。
**核心不变量（可写成 gate 断言）**：任意时刻
`RenderScene == ActiveScene == TreeScene`，除非处于显式过渡帧。

### 4.3 SelectionService（选择单入口）

```text
class SelectionService {
    void Select(uint64 objectId);   // 以 ID+generation 记账，内部换发 shared/weak
    void Clear();
    signal<> onChanged;             // 唯一通道，取代双通道 EventBus + 回调
};
```
收编清单：调查列出的全部 10 组写入点降级为调服务 API；四面板变纯订阅者；
Inspector Lock 上提为服务端策略（暂停分发、解锁回放）；New/Open/Play 切换的
清理走同一 API（消灭半残清理与第三套重建）。

### 4.4 两条横切纪律（低成本高确定性）

1. **日志单缓冲**：删除 ConsoleLog 512 槽缓冲与 LOG_*/ConsoleLogSink，LOG_*
   宏改走 spdlog → bridge sink → ConsolePanel（宏日志上屏分叉顺带根治）；
   保留 MemoryHistorySink。
2. **Undo 最小收敛**：ObjectResolverCallback 注入 FindTarget（~15 行）+
   RegisterInspector 时接 SetUndoCallback + OnImGui 每帧喂 SetCanUndo——
   三线打通，命令模型不动。

---

## 5. 迁移路径（每步独立可验证，gate 不回退）

| 步 | 内容 | 主要触点 | 状态 |
|----|------|----------|------|
| S1 | PlaybackController 合一双 PIE（GP01 为正，引擎 PIE 入口禁用/隐藏） | EngineEditor.cpp:247-268、Toolbar、MainMenuBar Scene 菜单 | ✅ 2026-08-25 执行 |
| S2 | ProjectDocument 抽取（session 成员迁入，宿主/面板改读文档） | GP01ProductionSession.h、EditorDemoApp.h | 开放（建议 GP1-E 首项） |
| S3 | SelectionService 收编 10 写入点 | EngineEditor.cpp:56-92 起、四面板 | 开放 |
| S4 | 日志单缓冲 + Undo 最小收敛 | ConsoleLog.h/.cpp、Log.cpp、UndoSystem.cpp:101 | ✅ 2026-08-25 执行 |
| S5 | 绑定表身份化（GUID map 替代下标）+ SceneSerializerV1 v2 预留 id 字段 | SceneSerializerV1、ContentAsset | 开放（涉冻结契约，需 L2 评审） |

### S1/S4 落地记录（2026-08-25）

**S1 双 PIE 合一**：
- `EngineEditor::SetExternalPlayback(play, stop)` 新 API：菜单/工具栏 Play/Stop
  重接线到宿主回调；Pause/Step 菜单隐藏、工具栏禁用 —— 引擎内置 PIE 从本宿主
  UI 不可达（R1 三层叠加风险消除）
- `Toolbar::PlayAction` 返回 bool：以真实结果置位，失败不再卡假 Playing 态
  （审计 §3.3 BROKEN 项闭环）；`EditorSceneManager::Play()` 同步改为返回 bool
- GP01 会话新增 `SetPlayStateCallback`：任何入口发起的 Play/Stop 都回写工具栏
  状态（非乐观化的状态回写通道）
- **R7 修复**：会话拆出 `TeardownRuntime()`（无回调拆卸）；Stop 仅在确有运行态
  时动作+广播一次；LoadProject 先拆卸再绑 GameplayAPI（顺带修复旧序缺陷：
  播放中加载工程会把编辑态场景绑定清空），单次广播经实机日志验证

**S4a 日志单缓冲**：
- 删除 ConsoleLog 512 槽环形缓冲（类/实例/静态断言/shutdown 补丁全链移除，
  `ConsoleLog.cpp` 删除）；ConsoleLog.h 转为兼容垫片，LOG_* 宏改道 spdlog
  默认 logger（"{}" 包裹防花括号注入）→ PanelBridgeSink → ConsolePanel。
  宏日志"永不上屏"分叉根治，且宏日志首次进入文件/崩溃 dump 通道
- Log.cpp 摘除 ConsoleLogSink；注册表 clear/con_log 命令诚实化（no-op 说明 /
  指向真实 rotating file sink）。MemoryHistorySink（崩溃 dump）保留
- 兼容性：ImGuiTest/ImGuiDemo/RenderTest 三目标以垫片编译通过，调用点零改动

**S4b Undo 三线打通**：
- 执行端：`UndoManager::SetObjectResolver` —— PropertyChangeCommand::FindTarget
  弃桩，按当前活跃场景 FindByID 解析持久 ID；Execute/Undo 经
  ApplyObjectSnapshot 真实写回 name/position/rotation/scale（json 值域约定）
- 记录端：InspectorPanel 帧首快照（name+TRT）vs 当前值对比 → 
  RecordPropertyChange；基线随修改推进，连续拖拽由 TryMerge 合并；
  EngineEditor::RegisterInspector 注入 SetUndoCallback
- UI 端：OnImGui 每帧以全局栈真实深度喂 SetCanUndo/SetCanRedo（菜单永灰问题
  根治）；菜单 Undo/Redo 回调驱动 GlobalStack

**回归证据**：integrity_gate ALL GREEN (I1+I2+I3) 122/122 ×2（改动后两轮）；
EditorDemo 实机冒烟 15s 干净运行、project loaded 单次广播、engine.log 正常滚动；
ImGuiTest/ImGuiDemo/RenderTest 编译通过。

## 6. 明确不做

不做 Unity 式全局 EditorState 单例池 / 不做反射式数据绑定 / 不合并 VFX 图编辑器
私有 Undo（合理域隔离）/ 不动 Core SceneManager-Level 休眠体系（本 app 不消费）/
不为多场景工作流预设计（无生产证据，章程防蔓延）。

## 7. Ledger 对齐建议

登记 **GP-DX-010**（OBSERVE，L1）：统一状态层缺失的结构性证据链 = R1-R11 +
AUD 全表；晋升条件 = Human Run 出现一次由状态不一致导致的真实数据事故
（如 R11 错误落盘），或进入 GP1-E 编辑器深化阶段时按 §5 S2 直接 PROMOTE。
