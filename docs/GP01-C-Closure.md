# GP01-C Closure — Content Scale 封存 & 阶段转换声明

> 日期：2026-08-25 · 性质：**FREEZE**（阶段性终点，非迭代计划）
> 锚点：`docs/GP01-C-Content-Scale-Plan.md`（含 §8 裁决表）＋ commit **de00414**
> 效力：自本文件提交起，GP-P1 焦点由 **Runtime/Content Scale** 正式切换为
> **Editor Workflow Validation（GP1-D）**

---

## 一、冻结状态总表

| Gate | 状态 |
|------|------|
| M1–M4 冻结里程碑 | 🔒 FROZEN（维持，未触碰） |
| DF01–DF09 | ✅ PASS |
| Evidence Integrity I1/I2/I3 | 🔒 ALL GREEN（Gate v2 心跳模式） |
| 全仓测试 | ✅ **117/117**（基线 113 → 117） |
| test_gp01 | ✅ **17/17**（13 → 17） |
| GP1-C Runtime Spawn 实验 | ✅ **PASS**（见下） |
| RenderGraph | ❌ ISOLATED（维持隔离） |
| Query / M003 / M002 / Collision Event | ⏸ DEFER（维持） |
| Asset Browser v1 | 🔒 维持冻结（编辑器侧证据顺延 GP1-D） |
| Timer/Scheduler | 👁 OBSERVE（手写 timer 信号已现，非负担） |

## 二、封存的实验结果（C1–C10 全达成）

1. **规模目标全部兑现，且零引擎改动。**
   ```text
   场景敌人条目 5 → 0（瘦场景：Player/Walls×4/Pads×4/Director 共 10 对象）
   运行时实体 ≥30 → 实测 32 体全部经 Engine.entity.spawn 实体化
   五波编成逐波点亮：19G/6T/7S，每波组成精确断言
   注册表资产 9 → 33（32 tex + 1 script），100% 解析（GP01.Load）
   Gameplay C++ 增量 = 0 行
   ```
2. **全战役数字证据（`ContentScale_WavesProgression`）。**
   五波真实战斗清场：`spawned=32 · destroyed=32 · score=785`
   （=19×20+6×50+7×15，精确无漂移）；波间存档冷启 blob 全等
   （`ContentScale_SaveMidWaves`，含波中增援的非规则序列）。
3. **四项裁决全部无晋升证据。**
   - **Query（GP-006，L0→L1）**：动态 Spawn 后账簿由 game.lua 自维护
     （Materialize 单点 ~10 行 + Encode/Restore ~50 行），随字段线性；
     `find_all/tag` DEFER 维持。
   - **M003 Prefab（GP-005）**：ENEMY_TYPES 数据表一次修改天然传播全实例，
     结构重复传播需求未发生；DEFERRED 维持。
   - **M002（GP-004）/M004（GP-003）**：存档 v3 与 is_down+冷却继续成立；维持。
   - **Collision Event（GP-010）**：手写圆碰撞三处共 ~20 行，33 实体帧成本
     不可见；DEFER 维持。

## 三、本轮最重要的原则性证据

**6 个 Tank 在旧导航假设下必然失败，但修正发生在 game.lua 的类型感知
standoff，而非任何引擎侧改动。**

```text
旧假设：导航 standoff 固定 [0.90, 1.00]（对 grunt 成立）
现实：  tank 接触半径 0.65+0.40 = 1.05 > 1.05 攻击半径边界 → 站桩必死
修正：  CombatNav 按 ENEMY_TYPES 半径计算 holdLo/holdHi —— 游戏代码内一处
```

这确立了一条纪律先例：

> **Gameplay 问题首先由 Gameplay 解决；只有证明冻结 API 组合无法表达时，
> 才允许进入能力晋升管道。** GP1-C 六门全闭即是该原则的量化体现。

## 四、测试基础设施硬化（Gate v2）

随本阶段将 `tools/integrity_gate.ps1` 重构为**后台启动 + 日志心跳监听**
模式（长命令不再前台阻塞：逐测试实时回显 / 静默 >240s 判死杀进程树 /
1800s 绝对上限）。过程中定位并修复三个工具链缺陷，永久留档：

| # | 缺陷 | 根因与修法 |
|---|------|-----------|
| 1 | PS5.1 下进程退出后 `ExitCode` 读不到 | `Start-Process -PassThru` 后必须立即缓存 `$p.Handle`，否则句柄早释放 |
| 2 | `WaitForExit(Int32)` 返回 true 后 ExitCode 仍可能为空 | 需再调无参 `WaitForExit()` 收尾收割 |
| 3 | 无 BOM 脚本中的中文注释被按 ANSI/GBK 解码，UTF-8 尾字节（如 `0x9A`）吞掉行尾换行，**把下一行代码合并进注释**（`$exitOk` 赋值整行失效且无报错） | 门禁脚本保持纯 ASCII；中文论述一律放 docs |

> 第 3 条对本仓库所有 PowerShell 工具具有普遍约束力。

## 五、阶段转换声明

```text
【已回答】内容规模扩大后，冻结 API 是否仍经济？
          → GP1-C：是。32 动态实体/五波/33 资产/存档重启全链零引擎改动。
【新问题】开发者能否仅靠现有 Editor Workflow 舒服地持续生产与修改游戏？
          → 待回答（GP1-D）
```

操作含义：
- GP1-D 开工前不引入任何新引擎能力；Asset Browser 等 Editor 侧复核顺延至
  GP1-D 内以真实点击流/工作流证据裁决（GP-001、GP-009 的编辑器侧部分）;
- 观察重点从 runtime 摩擦转向 **DX 成本**：上下文切换次数、手改 JSON 的
  必要性、Rename 安全性、Import/Reload 自然度、错误恢复时间；
- 晋升纪律不变：Result A（足够）/ B（登记 GP-DX-xxx 观察，不做功能）/
  C（走唯一晋升管道）。

---

*本文件与 `docs/GP01-C-Content-Scale-Plan.md` §8、`docs/GP01-Ledger.md`
GP-004~010 共同构成 GP1-C 阶段的最终交付。*
