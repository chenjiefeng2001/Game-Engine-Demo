# VS01 Closure — 产品现实门封存 & 阶段转换声明

> 日期：2026-08-24 · 性质：**FREEZE**（阶段性终点，非迭代计划）
> 锚点：`docs/VS01-Report.md` ＋ commit **29f0e61**
> 效力：自本文件提交起，项目核心问题由 **Engine Validation** 正式切换为 **Game Production**

---

## 一、冻结状态总表

| Gate | 状态 |
|------|------|
| M1 Productization Core | 🔒 FROZEN |
| M2 Resource Lifecycle v1 | 🔒 FROZEN |
| M3 Asset Workflow v1 | 🔒 FROZEN |
| M4 Developer Experience v1 | 🔒 FROZEN |
| DF01–DF09 | ✅ PASS |
| Evidence Integrity I1/I2/I3 | 🔒 ALL GREEN |
| VS01 Product Reality Gate | ✅ **4/4 PASS** |
| 全仓测试 | ✅ **100/100** |
| RenderGraph | ❌ ISOLATED（维持隔离） |
| M002/M003/M004/M006 | ⏸ DEFERRED（维持） |

## 二、本次封存的三个结果

1. **F1 是真实运行时缺陷，不是测试修正。**
   VS01 找到了此前 DF01–DF09 均未覆盖的引擎缺陷（Reload 后标准库丢失，
   根因：`lua_call(1,0)` 丢弃 `luaopen_*` 返回的模块表）。已修复并永久化为
   回归守卫（`VS01Debug.MinimalReloadRepro`）——生产事故转化为长期契约保护。

2. **VS01 证明的是完整生产链，不是 API demo。**
   ```text
   空场景 → 资产导入/GUID → 场景生产 → Lua Gameplay → 编辑/Reload
     → Boss/Win-Lose → Save → Restart → Load → 继续 Play
   ```
   Product Reality Gate 的原始定义就此兑现。

3. **证据链成立。**
   Implementation → Registered Test → Actually Executed → Count Verified
   → Integrity Gate → 100/100。"测试文件存在"与"测试存在"的鸿沟由门禁机制持续封闭。

## 三、阶段转换声明

```text
【旧问题】引擎有没有能力？          → Dogfood 验证（DF01–DF09），已回答
【新问题】下一款真实游戏在哪里被当前契约卡住？  → 待回答
```

操作含义：
- 不再主动发起任何"能力证明"性质的工作（包括为刷数量而做的更多 Dogfood）；
- 引擎侧一切新工作的**唯一合法入口**是下节定义的晋升管道；
- 已冻结里程碑（M1–M4）与其契约测试保持只读，修改即视为破坏 FREEZE。

> **历史文件标注**：`docs/Next-Phase-Implementation-Plan.md`（2026-07-16，
> 能力优先路线图）自本声明起视为已被取代，仅作历史参考；因其为 GBK 编码
> 不便就地加注，特此记载。

## 四、能力晋升的唯一管道

```text
真实游戏阻塞
      ↓  ①
可复现            —— 最小复现案例（脚本/场景片段 + 期望 vs 实际）
      ↓  ②
最小能力定义      —— 能不能更小？能不能用现有冻结 API 组合表达？
      ↓  ③
Ledger 登记       —— 编号 GP-xx 进入 Game-Production-Ledger
      ↓  ④
实现              —— 允许动引擎；不允许顺手扩权
      ↓  ⑤
契约测试          —— 注册进基线，I1/I2/I3 必须保持 GREEN
      ↓  ⑥
真实游戏再次验证  —— 同一阻塞点在原游戏中解除，才算关闭
```

每一步都有明确产物；跳步视为无效晋升。② 是闸门：
**凡能用现有冻结 API 表达的，一律不晋升**（回归到内容侧解决）。

### GP-xx 登记模板（附录）

```markdown
### GP-xx — <一句话阻塞描述>
- 来源游戏：<哪一局、哪个环节>
- 契约触碰点：<冻结 API / 序列化格式 / 工作流的哪条边界>
- 复现：<最小脚本或场景 + 期望 vs 实际>
- 最小能力定义：<为什么现有 API 无法组合表达>
- 裁定：⏸ 观察 / ▶ 晋升实现（附 Ledger 行）
```

## 五、DEFERRED 维持清单（含再晋升条件）

| 项 | 状态 | 再晋升条件（不变，源自 DF01–DF09 Ledger） |
|----|------|------|
| M002 Component Serialization | ⏸ | Inspector 内实时调参成为某局游戏的实际需求 |
| M003 Prefab / Entity Template | ⏸ | 单场景 >30 相似实体，或跨场景复用行为包 |
| M004 Input.pressed() 边沿检测 | ⏸ | 出现跳跃/射击类需要边沿触发的真实 gameplay |
| M006 CWD 锚定 | ⏸ | 出现实际基础设施问题（非便利性问题） |
| RenderGraph | ❌ 隔离 | 维持隔离；渲染子系统工作不进入主线路径 |

## 六、Game Production 协议（下一局游戏怎么开）

1. **选题**：一款比 Arena Trials 更进一步的真实小游戏（允许更复杂 AI/关卡结构/
   更多实体规模），全程只用冻结 API + 内容资产；
2. **Ledger**：沿用 Dogfood Ledger 体例记录全过程；所有"卡住"瞬间按 GP-xx 模板登记；
3. **裁定纪律**：GP-xx 默认 ⏸；只有当同一类阻塞跨游戏复现（≥2 次）才自动获得晋升资格；
4. **完成判据**：该游戏可被第三人从仓库干净检出后按 README 跑通 Win 路径。

## 七、解冻条件

仅当出现以下情形之一，才允许重开引擎能力主线：
- 某真实游戏的 GP-xx 通过第 3 条裁定获得资格并走完管道；
- Integrity Gate 或冻结契约测试出现 RED（回归修复优先级最高，不受本 FREEZE 限制）；
- 渲染主线（Vulkan RHI 等）作为独立轨道另行立项，不占用本管道。

---

*本文件与 `docs/VS01-Report.md` 共同构成 VS01 阶段的最终交付。*
