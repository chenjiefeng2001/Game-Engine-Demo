# GP1-DX UI 链路完整性审计报告

> 日期：2026-08-25 · 基线：`f8cbfc6`（GP-DX-007 修复后）
> 方法：三路全文源码审计（Production 宿主 / 菜单栏·工具栏 / Hierarchy·Inspector·
> Viewport·Console），关键判定逐条人工复核源码。
> 目的：回答"每个按钮的每个链路是否通顺、能否被正确使用"，为 GP1-D Human Run
> 与 P1 修复排序提供依据。

---

## 0. 判定口径

| 判定 | 含义 |
|------|------|
| WIRED | 链路完整：交互 → 引擎 API → 用户可见反馈，逻辑自洽 |
| PARTIAL | 主链路通，但存在明确缺口（文中说明） |
| DEAD | 控件存在但回调为空 / 无消费者 / 永不可达 |
| BROKEN | 链路存在但行为错误（假成功、状态漂移、弹不出等） |

标注 † 的条目为本报告直接复核过源码的判定。

## 1. 总览

| 区域 | 控件数 | WIRED | PARTIAL | DEAD | BROKEN |
|------|--------|-------|---------|------|--------|
| GP01 Production / Content / Script Editor | 14 | 7 | 5 | 0 | 2† |
| 主菜单栏（MainMenuBar） | 17 | 11 | 3 | 2 | 1† |
| 工具栏（Toolbar） | 12 | 2 | 0 | 9 | 1 |
| Hierarchy / Inspector | 18 | 8 | 6 | 3 | 1 |
| Viewport | 12 | 7 | 4 | 1 | 0 |
| Console | 8 | 3 | 3 | 2 | 0* |
| EditorDemoTest 测试面板 + StatusBar | 15 | 7 | 4 | 4 | 2 |

\* Console 命令执行归 BROKEN 级问题，见 §3.4。
**结构性结论：Human Run 主线（编辑态）基本可用；Toolbar 是整条视觉摆设；
死代码簇庞大；存在 4 个会直接污染 Human Run 结果的缺陷（§2）。**

## 2. Human Golden Run 主线判定（最关键）

按 `docs/GP1-D-Human-Run.md` 三条 Run 的操作序列逐项对照：

| # | Run 步骤 | 判定 | 说明 |
|---|----------|------|------|
| 1 | 启动自动加载工程 | **WIRED** | manifest 存在即 LoadProject，四路场景重接完整（18:04 实机日志佐证） |
| 2 | ＋Entity ×3 | **WIRED** | 重名规避 + 尾插索引稳定 + 绑定表补 Null + 自动选中；限制：零组件实体视口不可见，反馈仅 Console+Hierarchy |
| 3 | Content 搜索 → Assign Sprite（编辑态） | **WIRED** | AddComponent/SetTexture + FindIndex 写绑定表 GUID（DL-02 纪律落实） |
| 3' | Assign Sprite（**Play 态误触**） | **BROKEN†** | 选中对象属 runtime 克隆，FindIndex 在编辑态查不到 → GUID 不落盘、纹理改动随 Stop 蒸发，**但日志仍报 "sprite assigned"（假成功）**（GP01ProductionSession.h:182-194,175-180） |
| 4 | F2 重命名 / Inspector 改 Position / 视口点选 / gizmo 拖拽 | **WIRED** | inline rename 双同步；Transform 写回即时渲染；MRT 拾取→EventBus→三面板联动闭环；gizmo decompose 写回 TRS（限根对象） |
| 5 | 右键 Delete | **PARTIAL** | 根对象删除干净（延迟删除防迭代器失效）；**子对象不从父 children 解除引用——删除后仍显示/渲染**（Scene.cpp RemoveObject）；且删除不重排 GP01 绑定表（当前 app 内无删除入口时无害，经 Hierarchy 删除即触发） |
| 6 | Script Editor 打开/编辑/保存 | **BROKEN†** | "Save" 日志在 `if (f.good())` 之外——**写盘失败也打 "[GP01] script saved"**；Load/切脚本无 Dirty 保护静默丢弃未存修改；64KB 缓冲上限可截断长脚本并经 Save 写坏真实文件（GP01ProductionSession.h:287-299,303） |
| 7 | F5 热重载 | **WIRED**（有盲区） | _PERSIST 暂存/恢复正确、句柄表保留、失败安全跳过；盲区：Console 输入框捕获键盘期间 F5 静默失效；reload 失败无回滚 |
| 8 | ▶ Play / ■ Stop | **WIRED** | 编辑态快照→克隆→director 驱动；Stop 整体丢弃克隆回切编辑态（与 GP-DX-004 先行裁决一致）；小缺口：Play 中选中的运行态实体在 Stop 后 weak_ptr 过期，选中静默清空 |
| 9 | 💾 Save Project | **WIRED**（带风险） | 真实写 scene+manifest、路径记忆成立；**失败分支完全静默**；CaptureScene 仅存 Name+Position+sprite/script GUID（SceneSerializerV1.cpp:83-101），旋转/缩放/Mesh 有损转换无警告（v1 冻结契约内，属诚实登记范畴，但对不知情者是陷阱） |
| 10 | 重启 → Load 复核 | **WIRED** | 冷启自动加载路径同 #1 |

### 会直接污染 Run 结果的缺陷清单（建议修复后再跑 Run）

| 编号 | 缺陷 | 影响的 Run |
|------|------|-----------|
| AUD-1 | Script Save 假成功日志（写盘失败无感知） | Run3 全部 |
| AUD-2 | Play 态 Assign Sprite 假成功（改动蒸发仍报成功） | Run2 若在 Play 中操作 |
| AUD-3 | 子对象 Delete 不解除父引用（删不掉） | Run1 步骤5（若删子对象） |
| AUD-4 | Save Project 失败静默（磁盘/权限问题不可感知） | Run1 步骤6 |

四者均为小时级修复。AUD-1/2/4 属反馈层补日志即可；AUD-3 需 Scene::RemoveObject 补父子解除。

## 3. 分面板明细

### 3.1 GP01 Production 窗口（sandbox/src/EditorDemo/GP01ProductionSession.h）

| 控件 | 位置 | 判定 | 一句话问题 |
|------|------|------|-----------|
| Open Project | :214 | PARTIAL | 硬编码 assets/gp01/*，无 FileDialog；"记忆路径"只服务 Save |
| Save Project | :218 | WIRED(风险) | 见主线 #9：失败静默 + v1 有损转换无警告 |
| ▶ Play | :222 | WIRED | 多脚本实体只取第一个作 director（v1 设计内） |
| ■ Stop | :226 | WIRED | Stop 后选中静默清空（weak_ptr 过期） |
| Reload (F5) 按钮 | :229 | WIRED | 同全局 F5 |
| ＋Entity | :232 | WIRED | 新实体视口不可见 |
| Content 过滤框 | :241 | WIRED | — |
| Assign Sprite | :252 | PARTIAL | 编辑态完整；Play 态假成功（AUD-2） |
| 脚本下拉框 | :276 | WIRED | 切换不查 Dirty，未存修改静默丢 |
| Script Load | :287 | PARTIAL | 无 Dirty 保护直接覆盖缓冲 |
| Script Save | :289 | **BROKEN†** | 假成功日志（AUD-1）+ 截断风险 |
| Script Reload(F5) | :295 | PARTIAL | 热更对象是运行实例而非编辑缓冲，语义易误解 |
| InputTextMultiline | :297 | WIRED | 64KB 上限 |
| F5 快捷键 | :149 | PARTIAL | Console 抢占键盘时静默失效；与菜单栏装饰性 "F5"(Scene>Play) 文案冲突 |

### 3.2 主菜单栏（engine/src/Editor/MainMenuBar.cpp）

| 控件 | 位置 | 判定 | 说明 |
|------|------|------|------|
| File>New/Open Scene | :59/:63 | WIRED | 本体闭环；Ctrl+N/O 快捷键文本纯装饰 |
| File>Save Scene (Ctrl+S) | :74 | PARTIAL | 名为 Save 实为每次弹 Save As，与下一项代码重复 |
| File>Save As | :78 | WIRED | 与上重复实现 |
| File>Exit | :84 | WIRED | — |
| Edit>Undo/Redo | :102/:106 | **DEAD** | 回调从未注册 + canUndo/canRedo 永 false，菜单项永久灰 |
| Edit>Preferences | :112 | PARTIAL | 只写 JSON 不热应用；X 关闭后残留暂存值 |
| Scene>Play/Pause/Stop/Step | :125-135 | WIRED | 引擎 PIE 本体完整；F5/F6/F10 键位均不存在或被抢占 |
| View>各面板开关 ×14 | :151-183 | WIRED | 除 Reset Layout 外全部正常 |
| View>Reset Layout | :183 | **DEAD** | 信号唯一消费者只在 Init 执行一次；InitDockingLayout 未定义 |
| Tools>Shader/VFX/Anim | :211-213 | WIRED/PARTIAL | Animation Editor 每帧强制可见，窗口 X 失效 |
| Help>About | :226 | **BROKEN†** | OpenPopup 在菜单子窗口 ID 域、BeginPopupModal 在根域——经典 ImGui popup 陷阱，弹窗永不开 |

### 3.3 工具栏（engine/src/Editor/Toolbar.cpp）——整条摆设

真实 Gizmo 由 ViewportPanel 私有状态驱动（ViewportPanel.cpp:443-494）；Toolbar 状态唯一消费者 `EngineEditor::DrawGizmo`（EngineEditor.cpp:598）全仓无调用者。

| 控件 | 判定 | 说明 |
|------|------|------|
| W/E/R 全局键 + T/R/S 按钮 | **DEAD** | 只改 Toolbar 高亮，不影响真实 gizmo |
| Local/World、Snap 磁铁、SnapValue | **DEAD** | 八个 Set*Callback 注册接口从未被调用 |
| Overlays Show Grid/Gizmos/Colliders | **DEAD×3** | 三个 bool 全仓零消费者 |
| Camera Fly Speed 滑条 | **DEAD** | GetCameraSpeed 无消费者 |
| Reset Layout 按钮 | **DEAD** | 同 View>Reset Layout 黑洞 |
| Render Mode Combo | **WIRED** | 正确直达 SetViewMode |
| Play/Pause/Stop 状态机 | **BROKEN** | 乐观更新无失败回滚，Play 失败时卡假 Playing 态 |

### 3.4 Console（engine/src/Core/ConsolePanel.cpp）

| 控件 | 判定 | 说明 |
|------|------|------|
| 日志显示/级别过滤/AutoScroll/Clear/Collapse | WIRED | — |
| 搜索框 regex | WIRED | — |
| Channels 频道过滤 | PARTIAL | bridge sink 只产生 Engine/Console 两频道，六开关形同虚设 |
| 命令输入执行 | **BROKEN†** | 硬编码只认 clear/cls/help（:359-370）；ConsoleCommandRegistry 完整体系（含 stats/gc 及全部内置命令）未接入，help 文案虚假宣传 |
| Tab 补全 | **DEAD** | m_CompletionCandidates 无填充代码 |
| ↑/↓ 历史 | WIRED | — |
| `~` 呼出 | PARTIAL | 依赖 Application::SetConsolePanel 注册，EditorDemo 宿主未注册 → 键无效 |
| 日志缓冲线程安全 | PARTIAL | s_LogBuffer 被 spdlog 后台线程无锁写、主线程读写——数据竞争（shutdown UAF 已修，运行期竞态未修） |

### 3.5 Hierarchy / Inspector / Viewport

Hierarchy：树点选、F2 重命名、＋创建 **WIRED**（新建 Cube 视口不可见记 PARTIAL）；
搜索框 **DEAD**（PassesFilter 恒 true）；右键 Hide **DEAD**（空函数体）；拖拽 reparent
**DEAD**（接收端 placeholder 空指针自认简化）；Delete 见 AUD-3。

Inspector：Lock/Debug、Active、Name、Transform 三组 DragFloat、MeshRenderer
enable/remove/BaseColor、Physics Damping **WIRED**（recordUndo 因回调无人注册而空转）；
Body Type Combo **BROKEN**（选择不落盘，下帧弹回）；Add Component 弹窗 **BROKEN**
（Selectable 点击无任何挂载逻辑，注释自认占位）；Mesh/Material DND 指派 **PARTUAL/BROKEN**
（AcceptDragDropPayload 后不写 TargetMesh）；SpriteComponent drawer 为宿主只读文案
（符合 P0 定位）；Script 组件不存在（ScriptInstance 是会话级而非组件）。

Viewport：MRT 点选、gizmo 写回、Q/W/E/R/Tab/F/G/Del 快捷键、浮层工具栏五按钮、
Local/World/Snap/Grid、右键菜单 **WIRED**（根对象范围）；Billboard 可拾取但无高亮
uniform 记 PARTIAL；状态栏 DC 字段恒 0（`m_GL ? 0u : 0u` 假数据）；Add...>Point Light
被宿主回调覆盖成建 Cube（名字误导）；父子对象 gizmo 存在世界/本地矩阵混用跳变 bug。

### 3.6 EditorDemoTest 测试面板与 StatusBar（非生产链路）

Undo/Redo 相关按钮机制通但效果 DEAD（引擎侧 PropertyChangeCommand::FindTarget()
桩返回 nullptr，UndoSystem 对真实编辑器整体断线：记录端/执行端/UI 端三重断线）；
Warning/Error 日志菜单实际调用 Log::Info（文案不符）；Register Test Menu Item DEAD
（插件菜单无渲染端）；Wire/Mute QuickToggle DEAD（bool 零消费者）；Test Background
Task 无 PopTask 永挂；Toggle Console 操作的是测试私有实例（与 ~ 键目标不同）。
StatusBar 主实例：FPS/DC/Mem WIRED；任务区/Git/QuickToggles 无数据源显示硬编码默认值；
通知系统 push 即泄漏（OnImGui 不渲染通知）。

## 4. 结构性发现

1. **双 PIE 并存互不知情**（已知架构债的具体表现）：EngineEditor.m_SceneManager
   不随 GP01 onSceneReplaced 更新 → 拾取回调仍查旧场景；主菜单 Scene>Play 与
   GP01 Play 语义冲突（前者 JSON 克隆不跑脚本、渲染注入器仍画 GP01 场景）。
   P0 以 Production 窗口为准的约定目前靠纪律维持，UI 上无任何隔离（菜单入口裸露）。
2. **F5 三重语义**：GP01 热重载（真实现）/ 菜单装饰文本（Scene>Play）/ UserSettings
   默认键位 play=F5（未接线）。
3. **死代码簇规模可观**：PropertyDrawer.cpp 整库孤儿、EngineEditor::DrawGizmo 第二套
   gizmo 实现、EditorSceneManager::DrawPlayToolbar 完整实现却无调用者、InitDockingLayout
   只有声明、reparent/搜索框/Tab 补全/Hide/通知系统等——合计拉高维护成本与"看起来有
   这功能"的假阳性风险（GP-DX-007 的教训正是此类）。
4. **反馈层系统性薄弱**：多处"操作成功"日志先于/独立于真实结果校验
   （Script Save、Assign in Play、Toolbar Play 态），与 GP-DX-007 假阳性同构。
   建议 P1 立一条硬规矩：**成功日志必须以 IO/引擎返回值为准**。

## 5. 修复排序建议

| 级 | 项 | 成本 |
|----|----|------|
| P1-a（阻塞 Human Run 有效性） | AUD-1 Script Save 返回值校验+失败日志；AUD-2 Play 态 Assign 拒绝或提示；AUD-4 Save Project 失败日志；AUD-3 RemoveObject 父引用解除 | 各 <1h |
| P1-b（低成本高价值） | About 弹窗作用域修正；Body Type Combo 落盘；Add Component 假入口摘除或实现；菜单快捷键文本清理（避免 F5 误导）；Console 接入 ConsoleCommandRegistry | 各 0.5-2h |
| P1-c（结构债，可延后） | Toolbar 与 ViewportPanel 状态桥接或整条摘除；双 PIE 入口隔离（Play 期间禁用菜单 PIE）；UndoSystem 接线或显式冻结声明；死代码簇清扫 | 专项评估 |

---

## 附：复核记录

本报告中标 † 条目由主会话直接读源码二次确认：
GP01ProductionSession.h:175-194,211-234,261-304（Assign/Save/按钮）、
MainMenuBar.cpp:224-233（About popup）、ConsolePanel.cpp:359-370（命令白名单）、
SceneSerializerV1.cpp:83-101,147-152（CaptureScene 字段集/写盘返回值）。
其余判定来自两轮全文阅读交叉验证，置信度高但未经实机点击验证——Human Run
本身即为最终实测环节。

## 6. Remediation 记录（2026-08-25，AUD 修复完成）

| 编号 | 状态 | 修复内容 |
|------|------|----------|
| AUD-1 | ✅ 已修 | SaveScriptBuffer：打开/写入失败均 Error 日志并返回 false；成功日志移入成功分支 |
| AUD-2 | ✅ 已修 | AssignSpriteToSelected Play 态直接拒绝（Warn 提示先 Stop）；FindIndex 落空兜底 Error |
| AUD-3 | ✅ 已修 | Scene::RemoveObject 从父 children 摘除子对象；连带修复 GameObject::RemoveChild 双重 erase（先持有引用再 erase 再 SetParent）；契约 `GP01.EditorWorkflow_DeleteSemantics` 固化 |
| AUD-4 | ✅ 已修 | SaveProject 场景/manifest 写盘失败分别 Error 日志；未加载工程时 Warn |

基线 test_gp01 21→22（D7），`integrity_gate.ps1` **ALL GREEN (I1+I2+I3), 122/122**。
审计期间环境插曲：上 session gate 残留的半死 `test_content.exe`（05:06 启动，
ASan interception 死锁态）锁死 exe/pdb —— 以 rename 绕过解锁，重启后可删
`build\tests\Debug\test_content.zombie_locked*`。

### P1-b Remediation（2026-08-25 第二批）

| 项 | 状态 | 修复内容 |
|----|------|----------|
| About 弹窗作用域 | ✅ 已修 | OpenPopup 经 `m_AboutOpenRequested` 标志延迟到 EndMenu 后同一 ID 域触发（MainMenuBar.cpp DrawHelpMenu） |
| Body Type Combo 落盘 | ✅ 已修 | 选择即调 `body->SetType(static_cast<BodyType>)`，不再弹回（InspectorPanel.cpp） |
| Add Component 假入口 | ✅ 摘除 | 按钮/弹窗/DrawAddComponentMenu/m_AddComponentSearch 全链移除；通用组件工厂如成生产需求走 GP ledger 晋升（引擎无 type-erased 组件工厂，不为此扩 API） |
| 菜单快捷键文案 | ✅ 清理 | File/Scene/Edit 全部装饰性键位文本移除（Ctrl+N/O/S/Z/Y、F5/F6/Shift+F5/F10 均未注册）；Alt+F4 保留（OS 层真实生效）；消除 F5 与 GP01 热重载的语义冲突 |
| Console 接入注册表 | ✅ 已修 | SubmitCommand 路由至 ConsoleCommandRegistry（60+ 内置命令激活：help/cmdlist/set/get/r_*/phys_*/s_*/profiler/screenshot 等）；clear/cls 保留面板本地语义；Quake ^N 色码剥离；Tab 补全以 GetCompletions 实装（唯一命中补全、多重命中列候选） |

第二批完成后 `integrity_gate.ps1` 复跑 **ALL GREEN, 122/122**；EditorDemo 实机冒烟通过
（自动加载工程 → 干净退出）。

P1-c 结构债维持开放（Toolbar 桥接或摘除 / 双 PIE 入口隔离 / UndoSystem 接线或冻结声明 /
死代码簇清扫 / PropertyDrawer 孤儿库处置），见 §5。

---

## 7. UI 层级/布局专项审计（2026-08-26，三准则）

> 准则来源：人工评审要求 —— ① 类别不混（不该同组的组件不得放一起）；
> ② 菜单不过深；③ 字体显示完整、位置正确。
> 方法：全 UI 源码走查 + **图标码位 × 真实字体 cmap 机器比对**（Python 解析
> OTF cmap format 4/12，对 `assets/fonts/fa-solid-900.otf` 与 FA7 子模块双验）
> + EditorDemo 实机冒烟。

### 7.1 准则①类别混淆

| 编号 | 发现 | 判定 | 处置 |
|------|------|------|------|
| CAT-1 | View > Scene Panels 子菜单内 "Hierarchy (Entity Tree)" 与核心组 "Scene Hierarchy" 绑定**同一 bool** —— 同一组件出现在两个类别，开关互相干扰且语义重复 | 混类 | ✅ 摘除子菜单内副本（MainMenuBar.cpp DrawViewMenu） |
| CAT-2 | Tools 菜单把 Dear ImGui 内置调试三件套（Demo/Metrics/StackTool）排在内容创作编辑器之前，两类别仅一条分隔线且顺序反直觉 | 混类(轻) | ✅ 平铺分组重排：创作工具在前、ImGui 调试组隔离置后；**未引入子菜单层级** |
| CAT-3 | GP01 Production 窗口单行混排三类控件：工程 IO（Open/Save）+ 播放控制（Play/Stop/Reload）+ 实体创建（+ Entity） | 混类 | ✅ 竖分隔线分三组；提示文案拆为 edit/play 两行各归其类（GP01ProductionSession.h DrawProductionWindow） |
| OBS-T1 | Toolbar W/E/R 全局键拦截（Toolbar.cpp:38-42）在 Play 态与游戏 WASD 输入语义冲突（当前回调为空无可见影响） | 登记 | P1-c Toolbar 桥接/摘除决策时强制一并处理 |

### 7.2 准则②菜单深度

全 UI 实测最深层级 = **2**（View > Scene Panels / Browsers；Viewport 右键
Add...；AssetBrowser 右键 Create）—— 合规，无需收敛。本轮全部修复均为平铺
操作，零新增子菜单。结论：该准则当前无违规项，作为后续改动约束记录。

### 7.3 准则③字体显示完整性 / 位置

| 编号 | 发现 | 后果 | 处置 |
|------|------|------|------|
| FONT-1 | `IconsFontAwesome6.h`（自定义 70 宏子集）与真实 FA7 Solid 字体脱节：**4 码位不在 cmap**（ROTATE_LEFT f3e2 / ROTATE_RIGHT f3e3 / MAP_LOCATION_DOT f620 / WATER f777）；**2 个字节↔注释不符**（ARROWS 字节实为 U+F071=警告三角，注释写 f07b=folder；WATER 字节 f777 注释写 f77b） | Toolbar/Viewport 浮层 Rotate 按钮、SceneManager 窗口标题、流式分组图标渲染为 "?"；**移动工具按钮显示警告三角**（图标语义错误） | ✅ 5 宏替换为经 cmap 验证的等义码位：ROTATE_LEFT→f0e2、ROTATE_RIGHT→f01e、MAP_LOCATION_DOT→f5a0、WATER→f773、ARROWS→f0b2(move)；复验 **70/70 全部存在于字体** |
| FONT-2 | `SetScale` 运行期静默 no-op：Begin() 经 LoadFont(null) 被 `m_CjkFontAttempted` 短路，ApplyEngineStyle 也未调用 —— 字体与样式均不变，**日志却打印 "Scale set to ..."**（GP-DX-007 假成功家族第 5 例） | 缩放功能整体失效且排障被假日志误导（ImGuiTest/ImGuiDemo 宿主实际触发路径） | ✅ Begin() 帧间（NewFrame 前）`RebuildFontAtlas(newSize)` + `ApplyEngineStyle(pending)`；1.92 动态纹理系统按 WantCreate/WantDestroy 自动重建 GPU 纹理；过时的"禁用 Clear()"注释更正为官方口径（Clear 仅禁止帧中调用） |
| POS-1 | MainMenuBar 右侧版本文本固定 `SameLine(Width−160)`，窗口偏窄时与左侧菜单重叠 | 重叠遮挡 | ✅ 实测文本宽 + 余量守卫，空间不足跳过绘制 |
| OBS-P1 | Viewport 浮层工具条固定 400px 宽，snap 展开时内容估宽 ~360px 接近上限（窄视口折行裁切风险） | 观察 | 低危，P1-c 一并评估 |
| OBS-P2 | StatusBar 右侧簇按硬编码 ~300px 预留，长分支名可左侵任务区 | 观察 | 低危，P1-c 一并评估 |
| OBS-P3 | ViewportPanel 浮层水平流内用 `Separator()`（短横线）而 Toolbar 用自绘竖线 —— 分隔符语言不统一 | 观察 | 纯视觉一致性，P1-c |

### 7.4 验证记录

- 构建：EngineCore + EditorDebug(EditorDemo) Debug 编译通过（零新告警）。
- Gate：`integrity_gate.ps1` **ALL GREEN (I1+I2+I3), 122/122**。
- 实机冒烟：EditorDemo 启动 15s 存活、`Merged CJK font: msyh.ttc (16px)` +
  `Merged FontAwesome: assets/fonts/fa-solid-900.otf (16px)`、GP01 工程
  10 objects/33 assets 自动加载、engine.log 零 error。
- 字体比对脚本结论留存于本节方法注记（cmap 解析对两份字体文件结果一致：
  3235 codepoints）。

---

## 8. P1-c 第一批：Toolbar 摘除手术 + Reset Layout 实装（2026-08-26）

> 承接 §5 P1-c 与 §7 OBS-T1。裁决：**摘除而非桥接** —— 真实 gizmo 状态以
> ViewportPanel 浮层 + Q/W/E/R 为单一真相源；桥接等于为摆设控件扩 API 面，
> 违反 GP-DX-007/GP-DX-008 一脉的"不提供虚假可供性"纪律。

| 编号 | 内容 | 处置 |
|------|------|------|
| TLB-1 | Toolbar 死控件整簇摘除：T/R/S gizmo 按钮、W/E/R 全局键拦截（OBS-T1 冲突源）、Local/World、Snap 磁铁+数值、Overlays 三开关（Show Grid/Gizmos/Colliders 全仓零消费者）、Camera Fly Speed 滑条 | ✅ 删除（Toolbar.h/.cpp 收敛为播放传送带 + Render Mode 平铺 Combo） |
| TLB-2 | Toolbar Reset 按钮（与 View>Reset Layout 同类双入口） | ✅ 摘除，布局重置唯一入口 = View 菜单 |
| TLB-3 | `EngineEditor::DrawGizmo` 第二套无调用者 gizmo 实现（死状态唯一消费者） | ✅ 连同声明、ImGuizmo include 一并移除 |
| TLB-4 | `InitDockingLayout()` 孤儿声明（只有声明无定义） | ✅ 摘除 |
| RST-1 | **View>Reset Layout 自审计以来一直 DEAD**：信号在 Init 末尾被 `ConsumeResetLayoutSignal()` 读后丢弃（§3.2 判定复核成立） | ✅ 双修：删除 Init 期吞信号行 + OnImGui 每帧消费，以 docking 分支 DockBuilder 重建四区规范布局（左 Hierarchy 22% / 中 Viewport / 右 Inspector 24% / 底 Console+Performance Tab 组 25%） |

### 8.1 启动链路真因调查（2026-08-26 第二批，实机取证）

> 触发：评审指出此前把启动异常归因"环境抖动"滑过去了。本轮以
> 受控实验（多次受控启动 + Win32 窗口枚举 + 精确句柄 WM_CLOSE 探针 +
> 新增关闭源取证日志）完成根因闭环。四个真 bug，全部修复：

| # | 真因 | 修复 | 验证 |
|---|------|------|------|
| INV-1 | **日志假卡死**：logger 为同步实现（注释误标 async），仅 `flush_on(warn)`、无周期刷盘 —— 纯 Info 启动序列滞留 CRT 缓冲数分钟，制造"启动停滞在纹理加载"假象（此前多轮误判根源） | `spdlog::flush_every(1s)`（Log.cpp） | 启动时间线秒级可信；0.7s 完成加载实测 |
| INV-2 | **布局撕裂**：默认停靠从未引导（`InitDockingLayout` 死条目的真正含义）—— 实测编辑器被拆成 **14 个漂浮 OS 窗口**；且 11 处 `Begin()` 标题带 FontAwesome 图标，原生标题栏渲染为 "? Inspector" 等 | ① 引导式规范布局：Viewport 未停靠即 DockBuilder 重建（判定延后至窗口存在帧，否则绕过 ini 持久化——已复现并修正）；② 全部标题去图标；③ 布局映射对齐真实标题（"Hierarchy" 非 "Scene Hierarchy"）；④ GP01 三件套入右下 Tab 组 | 窗口数 14→1（主窗）；干净退出后重启零重建（持久化成立） |
| INV-3 | **关闭链路不可观测**：`WindowClose` 事件零消费者、无来源日志 —— 8 秒自退无法归因 | OnClose Warn 取证 + 主循环退出点带 uptime 日志 | WM_CLOSE→告警→干净退出全链路实测通过 |
| INV-4 | **测试面板污染**：EditorDemoTest 无条件绘制私有 Console/Profiler/AssetBrowser，启动即生成与生产面板同类冲突的漂浮窗口 | 测试 Init 默认 SetVisible(false)（菜单通路保留） | 启动不再产生重复面板窗口 |

**遗留开放项**：间歇性外部 WM_CLOSE 在无人值守启动后 ~6s 送达（取证日志已两次捕获，来源未明——疑似宿主环境回收所派生 GUI 进程）；人工操作会话不受影响，再次出现可经日志直接溯源。

**环境登记**：僵尸 `test_content.exe` 第三次复发（昨日实例普通终止无效 + 今晨新实例），rename 绕过法再次生效（`.zomb` 文件留存）；**Human Run 会话前建议重启机器**，并在 gate 后例行 `Get-Process test_*` 检查。

验证：gate **ALL GREEN 122/122**（rename 绕过后）；受控双启动实验通过（引导一次→持久化→优雅关闭×2）。

P1-c 余项（维持开放）：双 PIE 入口隔离收尾观察、UndoSystem 接线效果实测
（S4b 已接线）、死代码簇清扫余量（PropertyDrawer 孤儿库处置需单独裁决
—— 与 PluginSystem 抽屉注册子系统耦合，涉插件 API 面；
DockspaceBuilder 经查为 Application 非 Editor 宿主在用，非孤儿）、
§7 OBS-P1/P2/P3 低危布局项。


---

## 8.2 �����᰸���ⲿ WM_CLOSE �����ڣ�2026-08-26 ���磩

> ��8.1 ��������"����ֵ������ ~6s �ⲿ WM_CLOSE����Դδ����"��
> 5 �����ʵ�� + Win32 ��ȡ֤������**�᰸��������ر���**��

| ʵ�� | ��� | ���� |
|------|------|------|
| T1 WMI �ѹ��������� env�� | ~6s ��Ĭ�˳� exit=1������־ | **ASan ���� CHECK ʧ���Ի�**��interception_win.cpp:193�������ⲿ�رգ�Human-Run �� `ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE=1` ��Ӳǰ�� |
| T2 WMI �ѹ��������� env�� | 15.0s �յ� WM_CLOSE | ��ʱ�����������棬��Ϊ�ر� |
| T3 �༭�� vs charmap ���� | �༭�� 14.5s �� / charmap ��� | ��ȫ������ɨ�� |
| T4 "Engine" ������±��ն� | ���±����� / �༭����� >95s | �ޱ���ƥ����ɨ�������رվ�Ϊ��Ϊѡ�� |
| T5 ���˸�Ԥ 60s+372s | **ȫ�̴��**�����տ������ֶ��� X �ر� | ���� |

### ȡ֤����������GlfwWindow::OnClose��

CloseForensics ������ϵͳ����������б�`GetLastInputInfo`����

- `lastInputAgeMs < 750` �� **HUMAN-LIKE (recent input)**���˵� X/�������رգ�
- ��֮ �� **PROGRAMMATIC (no recent input)**�����ⲿͶ�ݣ���������
- ʵ�⣺�������ֶ��رձ���ȷ��� `HUMAN-LIKE`��via=SendMessage��������·������fgPid=explorer.exe

### �þ�

- ��8.1 "captured twice" �����ιر��뱾�� T2/T3 һ�£�**��Ϊ������������˳�ֹرյ���**��
  ���ϸ� session ��ʵ��ʱ�����Ǻϡ�����ֵ�����鲻���ڡ�
- ����ð��Э�飺����ֵ���ж���"���̴�� + ��Ⱦ CPU"Ϊ׼����8 ���أ�����־ CloseForensics
  �п���֤�ر���Դ����������ר�
