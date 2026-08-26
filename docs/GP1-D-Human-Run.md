# GP1-D Human Golden Run 协议与记录

> 日期：2026-08-25 · 前置：GP1-DX P0（`9c6ef22`）· 执行者：开发者本人
> 铁律：只允许 Hierarchy / Inspector / Content 面板 / Script Editor /
> Play-Stop / Save-Load。**禁止** JSON / C++ / 内部 API / 手修 manifest。

启动方式：

```powershell
$env:ASAN_WIN_CONTINUE_ON_INTERCEPTION_FAILURE="1"
.\build\sandbox\EditorDemo\Debug\EditorDemo.exe
```

> 启动前建议例行检查残留进程（GP-DX 审计 §8 教训）：
> `Get-Process test_*,EditorDemo` —— 有半死 ASan 实例先清除（普通
> Stop-Process 无效时用 rename 绕过锁，或直接重启机器）。
> engine.log 已配置 1 秒周期刷盘（2026-08-26 修复），时间线可信。

启动即自动加载 GP01 工程（控制台应出现
`[GP01] project loaded: 10 objects, 33 assets`）。
每个 Run 结束把"实际"列填入本文件并提交。

### UI 现状对齐（2026-08-26，P1-c 批次后）

- Production 窗口单行已分组：[工程 Open/Save] | [播放 Play·Stop·Reload] | [+ Entity]；
- Toolbar 仅剩播放传送带 + Render Mode（gizmo 工具在视口浮层，Q/W/E/R）；
- View > Reset Layout 已实装可用（四区规范布局）；
- 图标码位表已与 FA7 字体对齐——若仍见 "?" 图标属环境字体问题，记录异常。

---

## Run 1 场景编辑能力

| # | 操作 | 预期 | 实际 | 异常 |
|---|------|------|------|------|
| 1 | ＋Entity ×3（Production 窗口） | 依次生成 Entity / Entity_2 / Entity_3，均被选中（场景内无同名对象，首个不跳号） | | |
| 2 | Content→Assign Sprite（给其一） | Console: sprite assigned | | |
| 3 | Hierarchy F2 重命名三者 | 树即时更新 | | |
| 4 | Inspector 拖改 Position | Viewport 图标移动 | | |
| 5 | 右键 Delete 其一 | 树移除；Console 无报错 | | |
| 6 | 💾 Save Project | Console: project saved | | |
| 7 | 关闭编辑器 → 重启 | 自动加载 | | |
| 8 | 核对 | 存活实体全部恢复、名字不变、Inspector 纹理仍显示 assigned | | |

观察项：
- **GP-DX-002**：故意把两个实体改成同名再分别 Assign——第二个的贴图是否落到第一个身上？
- 删除中间实体后，后续实体的绑定是否错位（索引对齐风险）？

## Run 2 资源生产循环

| # | 操作 | 预期 | 实际 | 异常 |
|---|------|------|------|------|
| 1 | Content 搜 "grunt" → Assign 到 Player | Player 贴图换 grunt | | |
| 2 | 再搜 "tank" → Assign 覆盖 | 替换成功，无残留 | | |
| 3 | Save → ▶ Play | 敌人追击正常 | | |
| 4 | ■ Stop → 再 Save → 重启 | Player 绑定=tank，无回退 | | |

检查：Sprite GUID 是否稳定；"看起来改了但存盘没变"是否出现；
**DL-02 是否消失**（Assign 后立即 Save 的实体重启后绑定仍在）。

## Run 3 Gameplay Iteration（只用 Script Editor）

| # | 操作 | 预期 | 实际 | 异常 |
|---|------|------|------|------|
| 1 | 选 game.lua：WAVES 第 1 波加 1 个 grunt | - | | |
| 2 | ENEMY_TYPES.grunt.value 20→25 | - | | |
| 3 | Save → ▶ Play → 杀 1 只 | 得分 = 25×N | | |
| 4 | ■ Stop → 再次 ▶ Play | 新波次生效；无实体复制 | | |
| 5 | Play 中按 F5（先改 value 30 并 Save） | 即时生效 | | |
| 6 | ■ Stop → 💾 Save → 重启 → Play | 改动持久；场景无污染 | | |

检查：_PERSIST 跨 Reload 连续；Reload 不产生重复实体；
Play 中改动在 Stop 后被丢弃（见 GP-DX-004）。

---

## Ledger 观察项（本次运行重点）

### GP-DX-004 Editor 状态一致性 —— 设计语义（先行裁决）

```text
Save Project 永远保存【编辑态场景】；Play 克隆中的任何修改在
Stop 时整体丢弃。即："Stop 后 Save = 你最后编辑的样子，而非你玩到的样子。"
```

Run 3 步骤 6 验证之。若此语义造成真实困惑/数据丢失感 → 升级条目。

### GP-DX-005 Viewport 反馈不足（OBSERVE）

Billboard 只表达位置。若 Run 期间无法判断"贴图绑对没有"，升级为
P1 渲染需求；能靠 Content 面板 + Inspector 文案判断则维持 OBSERVE。

### GP-DX-006 Project Session 生命周期（待验证）

连续按两次 Open Project / 反复 Play-Stop / Play 中 Open：
观察 Console 是否出现对象数翻倍、日志重复刷屏、句柄泄漏迹象。
（代码层预检：LoadProject 全量替换 Registry+Scene 并 Reset，
EventBus 仅启动时注册一次 —— 预期干净，以实测为准。）

---

## 结果汇总（运行后填写）

```text
Run1 场景编辑   [ PASS / FAIL ]  失败点：
Run2 资源循环   [ PASS / FAIL ]  失败点：
Run3 迭代闭环   [ PASS / FAIL ]  失败点：
新增摩擦        （逐条七元组，登记 GP-DX-00x）
GP1-D 裁决      Result [ A / B / C ]
```
