# GP01-D Editor Workflow Validation — 阶段计划与测量框架

> 日期：2026-08-25 · 章程：`docs/GP-P1-Charter.md` §7 GP1-D · 前序：GP1-C
> Closure（commit `297dd26`）
> 核心问题：**一个开发者能否仅靠现有 Editor Workflow，完成一轮真实内容生产
> 与修改，而不离开 Editor、不手改 JSON、不依赖外部 IDE？**

---

## 0. 基线锁定（D0）

- 基线：117/117，I1/I2/I3 GREEN @ `de00414`
- GP1-C 已封存：`docs/GP01-C-Closure.md`（独立提交，未混入本阶段内容）
- 本阶段纪律：**不为改善编辑体验而实现任何引擎功能**
  （Undo/Redo、Entity UUID UX、Import 对话框等一律只记录不实施）

## 1. 真实开发任务（D1）

不做抽象 Editor benchmark。任务命题：

> "把当前 Arena Survival 改造成第二版"

```text
Wave1 Grunt → Wave2 Grunt+Scout → Wave3 Tank → Wave4 混合 → Wave5 Boss
```

要求覆盖的操作面（全部经编辑器工作流等价 API 完成，禁止手改 JSON）：
增删实体 · 替换纹理 · 改敌人参数(ENEMY_TYPES/WAVES Lua) · 改出生位置 ·
改脚本 · Save · Play · Reload · 冷启 Restart · 再次 Load。

## 2. 测量协议（D2）

每个操作记录七元组：

```text
动作 / 打开哪个面板 / 手动输入什么 / 改动几个文件 /
是否离开 Editor / 是否重新 Import / 是否重新 Save
```

历史条目复核（docs/Dogfood04-DX-Baseline.md）：

| 编号 | 复核问题 |
|------|----------|
| DX02 | 输入绑定是否仍需手动？（预期不变：仅 headless 相关） |
| DX03 | 导入新资产是否已自然？（Import API 幂等存在，看流程摩擦） |
| DX05 | 同名实体歧义是否仍触发？（Rename 实验重点） |
| DX07 | Import-before-attach 是否已消失？（先 attach 后 import 的反序实验） |

新增摩擦登记为 `GP-DX-xxx`（Ledger 新分区），默认 OBSERVE 不做功能。

## 3. 关键既有陷阱（来自 DF08 Ledger，本阶段正面验证）

- **DL-01**：CaptureScene 存运行时位置而非 authored 值 —— 迭代循环中
  Save 前是否需要"回位"步骤？
- **DL-02**：mid-session 新增实体的 sprite/script GUID 依赖调用方维护的
  bindings 表，越界静默置 Null —— 编辑器工作流下由谁维护、何时丢失？

## 4. 测试契约（D3/D4/D6，tests/test_gp01/GP01EditorWorkflowTest.cpp）

- **GoldenPath（D6 黄金路径）**：
  Clean Start → Create Scene → Import Assets → Create Entities →
  Assign Assets → Edit Transform → Edit Lua → Save → Play → Reload →
  Modify Again → Save → Cold Restart → Load → Play；
  全链断言：场景对象数/GUID 解析/脚本有效/runtime state 一致，
  且**零手写 JSON 步骤**（所有变更经 SetName/SetPosition/
  SetTexture/Import/Capture 完成）。
- **IterationLoops（D3 三次迭代）**：
  - It.1 Create → Save → Play
  - It.2 改实体 + 改 Lua → Save → Reload → Play（_PERSIST 保持）
  - It.3 改资产绑定 + 改场景 → Save → Restart → Load → Play
  每轮验证 Scene/GUID/Script/Runtime state/Save/Restart 六点。
- **RenameSafety（D4）**：Enemy_Grunt_A → Guard_Left 式改名后
  Hierarchy 正确 · Script binding 保持 · Scene Save 正确 · Restart 正确 ·
  无同名歧义；再制造同名冲突观察 DX05 行为。
- **WrongEditRecovery（D5 观察）**：一次错误编辑（错绑纹理/误移实体）的
  恢复成本——几步操作？是否丢数据？是否必须重启？只记录，不做 Undo。

## 5. 核心度量（本阶段的"产品化问题"）

> **修改一个已存在的游戏需要多少上下文切换？**

30 秒级小修改的理想路径 = Editor 内 N 次点击；若实际需要
Editor → Explorer → IDE → JSON → Editor → Console → Restart 才能完成，
即为 GP1-D 的核心负面证据（逐次计数进入 Ledger）。

## 6. 裁决框架（与章程 §14 对齐）

| 结果 | 判定 | 动作 |
|------|------|------|
| A Workflow 足够 | 全链无 P0/P1 阻塞 | PASS，继续做游戏 |
| B 明确 DX 缺口 | 某项(Rename/Assign/Organization/Script editing/Play-Reload)反复产生真实成本 | GP-DX-xxx → L2 Observe → 第二次真实复现，**不立即做功能** |
| C 能力真空 | 冻结 API 组合无法表达某真实需求 | 走唯一晋升管道(Ledger→Contract Test→Implementation) |

## 7. 完成标准

D0 锁定 ✅ · D1 任务定义 · D2 七元组测量 · D3 三次迭代全过 ·
D4 Rename 安全 · D5 恢复成本记录 · D6 Golden Gate GREEN ·
基线更新且 Gate GREEN —— 达成后出裁决表并定夺 GP1-E。
