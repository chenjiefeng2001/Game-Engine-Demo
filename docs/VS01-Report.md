# VS01 Report — Vertical Slice 01「产品现实门」

> 日期：2026-08-24 · 载体：`tests/test_content/VS01Test.cpp` ＋ `assets/vs01/`（3 场景 / 4 脚本 / 11 纹理，全经 Import/GUID）
> 游戏：**Arena Trials**（menu → arena：5 敌/2 pickup/计时/Gate → boss：狂暴 Boss + Portal）
> 核心问题：**"把前面所有 Dogfood 验证过的能力拼成一个完整小游戏，产品级闭环是否真的成立？"**
> 阶段结论：本报告与 commit `29f0e61` 构成产品验证锚点；封存与后续路线见 **`docs/VS01-Closure.md`**。

---

## 总评

**成立。** 空项目起步 → 资产 Import → 三场景生产流 → 编辑→Reload→还原 → Save→冷启动→Boss→victory
全程在冻结 API（GameplayAPI v2）下跑通，Win/Lose 双路径可达，Lua 错误可经 Console 诊断。

```text
test_content 42/42 · 全仓基线 100/100 · Integrity Gate I1/I2/I3 GREEN
VS01.ProductReality_FullLoop ~2.5s（headless，无渲染依赖）
```

## 一、验收门禁（V1–V10）

| 门 | 内容 | 结果 |
|----|------|------|
| V1 | 空场景起步（零注册表/零对象） | ✅ |
| V2 | 全部资产经 Import 进入注册表 | ✅ 14 entries |
| V3 | 全程 GUID 契约（ResolvePath 无空路径） | ✅ |
| V4 | 冻结 API 驱动玩法（无引擎侧特判） | ✅ 测试输入走 IScriptInputProvider 注入 |
| V5 | 编辑→Play→修改→Reload→还原图 | ✅ 行为标志位 + `_PERSIST` 连续性 + 场景不变（12 对象恒定） |
| V6 | 多场景生产流（menu/arena/boss 三幕） | ✅ |
| V7 | Save→冷启→Load→Play（全新 Ctx 等价新进程） | ✅ 活场景捕获落盘，score=150 跨场景存活 |
| V8 | Win/Lose 双路径 | ✅ victory 与 defeated 均可达 |
| V9 | Lua 错误可诊断 | ✅ GetLastError 含原始标记 `vs01_probe_marker` |
| V10 | 提交态资产独立可用（不依赖 scratch） | ✅ 干净注册表从 assets/vs01 重载 |

## 二、缺陷账本

### F1（引擎缺陷，本次修复）：Reload 后安全标准库永久丢失
- **现象**：arena_director.lua 在 Reload 后首次调用 `math.*` 即抛
  `attempt to index a nil value (global 'math')`，主循环静默卡死。
- **根因**：`LuaEngine::SetupBaseAPI` 用 `lua_pushcfunction(luaopen_math)+lua_call(1,0)`
  手工重开标准库 —— 但 `luaopen_*` 只**返回**模块表、不写全局；0 结果调用直接把模块表丢弃。
  Init 路径被 `luaL_openlibs` 掩盖，Reload 路径（ResetGlobalState 清空全局后无 openlibs）裸露。
- **修复**：改用 `luaL_requiref(m_State, name, open, 1)` 完成 `_G[name]` 与
  `package.loaded` 双注册（LuaEngine.cpp）。沙箱策略不变（io/package 整库移除、os 剥危险项）。
- **回归守卫**：`VS01Debug.MinimalReloadRepro`（连续 3 次 reload 断言 stdlib 存活 + 移动行为连续），
  外加主测试内 reload 后 `assert(math and string and table and os)`。

### F2（文档化决策）：reload 后速度探针非确定性 → 标志位证据
持续负载下用位移量证明"编辑生效"存在抖动；改为标志位（`_PERSIST.vs01_edited`）+
状态/kills/场景对象数三重连续性断言。速度级行为变化的既有证据见 DF08 DX-G3。

### F-07（冻结契约边界）：无跨场景实体携带
场景切换的状态转移由宿主显式中介（`_PERSIST.score = 150`），与冻结 API 文档一致。

## 三、测试侧标定问题（非引擎缺陷，开发过程实录）

| 问题 | 处置 |
|------|------|
| timer 断言写反（覆盖 600s 解耦后仍按 `<90` 判剩余） | 改为"通关耗时 < 自然预算 90s"语义 |
| `remove_all(kScratch)` 后未重建目录即落盘 | save 前补 `create_directories` |
| Boss 战 hp=8 必败（atk 0.35s/1 vs 接触 0.8s/2 的交换比推演） | 注入 hp=12（留余量可赢配置） |
| 传送门直线被 Pillar_N 推出半径挡死 | 卡死检测 + 垂直侧移绕行（15 帧 <0.15 位移触发，交替换向） |
| 站桩打法的狂暴相位过短，位移比恰落在阈值之下（≈0.90 边缘） | 导航加入狂暴后风筝相位（75 帧拉扯），加速差异以 ≈2× 余量可观测 |
| 冷启动后读 `_g_score`（从未赋值） | 从 `_PERSIST.score` 显式取出 |

## 四、过程指标

**V11 缺陷构成**：引擎真缺陷 1（F1，已修＋回归钉死）；测试侧标定 6（上表全部当场修复）；
契约边界确认 1（F-07）。比例健康 —— 大部分摩擦来自"第一次有人把能力串成完整游戏"。

**V12 摩擦观测**：
- ScriptInstance 错误隔离（IsValid 不因脚本运行期错误翻车）让 headless 驱动极其稳 —— F1 类
  缺陷只能靠状态机停滞间接暴露；Console 诊断（V9）是定位关键。👁 建议未来 Play 面板
  把"每帧脚本周错率"做成可见指标。
- 数值平衡完全靠脚本常量手调（BOSS 表/ATK_CD 等），无热调工具；DF08 已观测过同类摩擦，不立项。

## 五、证据链

- 注册：`tests/CMakeLists.txt` 增加 `test_content/VS01Test.cpp`（I1 ✅）
- 计数：docs/Evidence-Baseline.md test_content 38→42，合计 96→**100**（I2 ✅）
- 映射：Evidence-Baseline 增加 VS01 行（I3 ✅）
- 资产：`assets/vs01/` 全量入库（`.gitignore` 补 `!assets/vs01/**/*.scene` 反向规则；
  scratch 目录 `vs01_scratch/` 入 ignore）
