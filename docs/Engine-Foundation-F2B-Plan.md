# Engine Foundation v1 — F2-B Plan（Physics Binding / 生命周期所有权验证）

> 建立日期：2026-08-28 · 上游：`Engine-Foundation-F2-Charter.md` §F2-B + `Engine-Foundation-F2-Closure.md`（含 F2-B 交接 §7）
> + `Component-Ledger.md` F2 Ledger
> 基线：F2-A tag `engine-foundation-v1-f2-a-collider`（`f674331`）
> 状态：**B0 ✅ / B1 ✅ / B2 ✅（20/20）/ B3 ✅（Result A，28/28）/ B4 ✅（Result A，37/37，
> 全 9 套回归绿）/ B5 ✅（Result A，43/43，真实 SceneSerializerV1 冷启）/ B6 ✅（Result A，54/54，
> 动态探针真交互对拍，见 EF-F2B-009）/ B7 双模型边界待实现**

---

## B0 — 基线冻结

| 项 | 值 |
|----|----|
| F2-A tag | `engine-foundation-v1-f2-a-collider`（commit `f674331`） |
| Integrity Gate | ALL GREEN（I1+I2+I3）**174/174** |
| test_bridge / test_scripting | 28/28 · 27/27（F2-A 契约已冻结） |
| 工作区 | 仅 `Application.cpp`（GP1-DX 遗留，F2 全程排除）+ spike 目录（未跟踪 scratch） |
| F2-B 起点代码 | **零 F2-B 代码**（无 PhysicsColliderAdapter / 无 Physics ECS/Jolt 改动） |

**Engine/Physics 现状快照（B1 审计输入）**：
- 2D 物理：`IPhysicsWorld`（抽象）→ `Box2DPhysicsWorld`（Box2D v3，沙箱主用）/ `Bare2DPhysicsWorld`（自制实现）。
- 3D 物理：`IPhysicsWorld3D` → `JoltPhysicsWorld`，由 ECS `PhysicsSyncSystem` 驱动。
- 9 套测试基线 **零覆盖** 2D Box2D/Bare 世界路径（test_physics 16/16 只测 GPUParticle CPU/GPU ring）——
  **F2-B 的绑定测试将是 2D 物理绑定路径的第一批自动化测试。**

---

## B1 — Physics Architecture Audit（源码证据）

### B1-1 · Body 的真正 owner

| 层面 | owner | 源码证据 |
|------|-------|---------|
| 原生 Body（b2 body / Jolt Body） | **Physics World**（b2 world / JPH PhysicsSystem） | `Box2DPhysicsWorld.cpp:148` dtor `b2DestroyWorld` 连带销毁全部 body；`JoltPhysicsWorld.cpp` `m_PhysicsSystem` 值成员持有 |
| Body 包装（IPhysicsBody） | **共享**：world 注册表（`m_Bodies` / `m_BodyMap`）+ 组件 `m_Body` 各持一份 `shared_ptr` | `Box2DPhysicsWorld.cpp:216` `m_Bodies.insert(physicsBody)`；`PhysicsComponent.cpp:26-27` `m_World`+`m_Body` |
| Create | 显式调用 `PhysicsComponent::CreateBody(world, def)`（游戏代码驱动，无自动系统） | `PhysicsComponent.cpp:19-27` |
| Destroy | 组件析构 → `DestroyBody()` → `world->DestroyBody()`；`RemoveComponent`/`GameObject` 析构都会触发组件 `OnDestroy`→析构 | `PhysicsComponent.cpp:15-16,35-40`；`GameObject.cpp:21-25` 析构遍历组件 |
| Recreate | **无任何自动重建机制**（序列化只存 `hasBody` 标记，Deserialize 为空实现） | `PhysicsComponent.cpp:65-80`（`Serialize` 仅 `hasBody`，`Deserialize` 注释"物理体重建依赖外部逻辑"） |

**关键结论**：Body 生命周期与 World 强绑定；组件→body 的销毁路径**存在且安全**（b2 API 全量 `b2Body_IsValid` 守卫，销毁后 stale wrapper 静默 no-op）。但 **Recreate 是空白**——现有引擎没有任何"World 重建 → 从组件重建 Body"的机制，这正是 F2-B 要补的硬门。

### B1-2 · Physics World 生命周期

```text
Create World   = 应用代码 make_shared<Box2DPhysicsWorld>(gravity)（沙箱/游戏自建）
Register       = PhysicsComponent::CreateBody(world, def) 显式注册
Simulation     = world->Step(dt)（每帧）
Destroy World  = shared_ptr 归零 → Box2DPhysicsWorld dtor → b2DestroyWorld
```

源码证据：
- **无集中式世界生命周期**：`PhysicsSystemManager::CreateWorld2D` 是桩，返回 nullptr
  （`PhysicsSystemManager.cpp:16-19`），2D 世界不进管理器；Scene/Application 也不持有世界。
- **销毁后谁还活着**：`b2DestroyWorld`（`:148`）连带销毁所有 b2 body；世界持有的 body 包装随
  `m_Bodies` 清空。**唯一存活物 = 组件侧 `m_Body` 的 shared_ptr 包装**，其 b2BodyId 已失效，
  任何调用走 `b2Body_IsValid` 守卫静默 no-op（无悬垂原生指针，安全降级）。
- **⚠️ 所有权纠缠**：`PhysicsComponent` 用 shared_ptr 持有世界（`PhysicsComponent.cpp:26`），
  **组件存活期间世界无法被真正销毁**（引用计数不为零）。"Destroy World" 只有在所有组件先释放
  引用后才生效 → 顺序约束：**组件先行销毁，世界后销毁**，否则世界被钉住。

### B1-3 · GameObject / ECS Entity / Physics Body / ColliderComponent 关系

**现状（源码证据）**：

```text
GameObject（组件模型，Editor/内容管线/2D 物理）
   ├── ColliderComponent（F2-A 契约，纯声明式，零物理引用）★ 新增
   ├── PhysicsComponent（2D 物理载体：m_World + m_Body）※ 非契约组件
   └── TransformComponent

ECS Entity（数据导向模型，仅 3D 物理）
   └── RigidBody3DComponent + PhysicsRuntimeComponent(runtimeBodyID) + PhysicsSyncSystem
       └── JoltPhysicsWorld

桥：ECSBridge（dormant —— 全引擎无调用者，仅自身文件实现）
```

- **GameObject ↔ ECS Entity：当前未连接**。`ECSBridge` 除自身文件外零调用
  （`grep ECSBridge::` 仅命中 `ECSBridge.cpp`），F0 记录的双模型分裂依然成立
  （`Engine-Foundation-v1.md:23-24`：Editor/内容管线用 GameObject 模型，3D 物理走 ECS）。
- **Physics Body（2D）与 GameObject 1:1 可成立**：`PhysicsComponent` 即"一个 GameObject 一个 body"。
- **ColliderComponent 的天然映射目标 = 2D 路径**（BodyDef/ShapeDef 都是 2D 结构，category/mask
  uint16 与 b2 filter 一致），**不需要 ECS**。

**目标链（F2-B，与 F2-A Closure §7 一致）**：

```text
GameObject
   └── ColliderComponent        = 唯一声明事实源（零物理引用，F2-A 已冻结）
          │
          ▼
   PhysicsColliderAdapter       = 唯一 Runtime 映射层（持有 world 引用 + body 注册表）
          │
          ▼
   ShapeDef / BodyDef           = 纯数据结构（PhysicsDefs.h，无物理库依赖）
          │
          ▼
   Physics Body（Box2DPhysicsWorld） = Runtime 派生对象
```

### B1-4 · ShapeDef 对 F2-A 七字段的无损映射核查

| F2-A 字段 | 映射 | 状态 | 证据 |
|-----------|------|------|------|
| shape（Circle） | `ShapeDef{type=Circle, circleRadius}` → b2Circle | ✅ | `Box2DPhysicsWorld.cpp:75-115` CreateShapesFromBodyDef |
| shape（Box） | `ShapeDef{type=Box, boxSize}` → b2MakeOffsetBox | ✅ | 同上 |
| radius | `circleRadius` | ✅ | 同上 |
| halfX / halfY | `boxSize={hx,hy}` | ✅ | 同上 |
| isSensor | `ShapeDef.isSensor` → `b2ShapeDef.isSensor` | ⚠️ **GAP-1** | **初始形状路径（CreateShapesFromBodyDef）未赋值 isSensor**（默认 false，静默丢失）；`Box2DPhysicsBody::CreateShapeFromDef`（AddFixture 路径）有赋值（`:175`）→ Adapter 必须走 fixture 路径或修复 |
| category / mask | `BodyDef.categoryBits/maskBits` → b2Filter | ✅ 创建时；⚠️ **GAP-2** 运行时 | `Box2DPhysicsBody::SetFilterData` 是 no-op（`:283-287`，注释"v3 不支持运行时修改"）→ **category/mask 变更只能 Destroy/Recreate** |
| enabled | `IPhysicsBody::SetActive` → b2Body_Enable/Disable | ✅ 可实时 | `Box2DPhysicsBody.cpp` SetActive |

**GAP 汇总（登记，不偷改 Collider）**：
- **GAP-1**：BodyDef 初始形状的 sensor 标记丢失（Box2D 适配层缺陷）。
- **GAP-2**：运行时 category/mask 更新不支持（SetFilterData no-op）。

---

## B1 裁决：可承载 → 进入 B2（**非 Result C**）

**为什么不是 Result C**：

1. **Ownership 可以唯一**：F2-A 冻结的模型（Component 零物理引用）恰好绕开了现状最大的纠缠点
   （PhysicsComponent 用 shared_ptr 钉住世界）。Adapter 持有 world + body 注册表，ColliderComponent
   完全不碰物理 → 不存在"Body 反向成为事实源"的可能。
2. **Destroy/Rebuild 可行**：世界销毁 → b2 原生 body 连带销毁、包装失效静默降级；重建时
   **参数可完全来自 Component**（F2-A 已证明组件序列化/冷重启全保真）。
3. **无强制反向同步**：引擎里不存在"Body → Component"写回的代码路径（2D 物理只读 Transform）。

**三个待定契约决策（B2 前必须落定，作为 F2-B 明确契约）**：

| # | 决策 | 建议方向（B2 实现时确认） |
|---|------|--------------------------|
| D1 | World 所有权 | **Adapter 拥有世界引用 + body 注册表**；组件零物理引用。销毁顺序：先清 body，再放世界（组件不受影响） |
| D2 | Mutation 策略 | 形状 / sensor / category / mask 变更 = **Destroy/Recreate**（GAP-1/GAP-2 决定）；enabled = **实时 SetActive**（唯一实时通道）。**不引入隐式实时同步机制** |
| D3 | Rebuild 触发 | 冷重启后 Adapter **显式重建**（场景加载 → 遍历 Collider → 重建 body）。无现有自动机制，F2-B 建立"Adapter::RebuildAll"入口 |

> 若 B3 破坏性测试（Destroy World → Component 存活 → 重建 → 参数一致 → 行为一致）失败，
> 按 Charter 直接登记 **Result C**，不修补。

---

## B2 — 最小 Adapter（✅ 已完成，2026-08-28，见 EF-F2B-003）

`PhysicsColliderAdapter`（`engine/include/Engine/Core/Physics/PhysicsColliderAdapter.{h,cpp}`）：
- `BuildBodyDef(const ColliderComponent&)` → `BodyDef`（public 静态，单向，Component 是事实源）。
- 运行时注册表：`const ColliderComponent*（observer）→ shared_ptr<IPhysicsBody>` + 世界引用
  （`SetWorld/ReleaseWorld` 显式掌控，未复制 PhysicsComponent 钉世界缺陷）。
- 生命周期：`Bind（Create）/ Unbind（Destroy）/ Rebuild / RebuildAll / Clear` + `IsBound/GetBody`。
- GAP-1 规避：Bind 经 Fixture 路径重建形状（sensor + filter 落到真实 shape），不修旧路径。
- v1 Mutation 契约（B2-4 表）内建：enabled → 即时 SetActive；其余 → Rebuild。
- **绝不出现** `ColliderComponent ← PhysicsBody` 反向同步。
- **测试**：`tests/test_physics/ColliderBindingTest.cpp` +4，test_physics 20/20，晋升门全绿。

## B3 — Lifecycle Golden Gate（✅ 已完成，2026-08-28，裁决 Result A，见 EF-F2B-004）

```text
Create GameObject → Attach Collider → Start Physics → Body Created
→ Destroy Physics World → Body Gone → ColliderComponent STILL EXISTS
→ Rebuild Physics → Body Created Again → Parameters == Component → 行为一致
```

**结果**：真 World 销毁（非 mock）→ 组件存活（地址/状态不变）→ 新 World + RebuildAll →
Body 从组件当前状态重建（B3-2 证明非旧快照）→ 行为恢复（B3-4 对拍）。多实体（B3-3）与
四条失败路径（B3-5）全过。test_physics 20→**28/28**。

## B4 — Component Lifecycle Integration（✅ 已完成，2026-08-28，裁决 Result A，见 EF-F2B-006）

> 编号重排：本节对应方案执行中的「B4 Integration / Lifecycle Wiring」（原计划表内 B4 Cold
> Restart 顺延为 B5）。

GameObject 的 ColliderComponent 生命周期由 Adapter **自动、确定性跟随**（零轮询）：

```text
GameObject: AddComponent<Collider>() → ColliderComponent → Adapter::Bind() → Box2D Body
            SetComponentProperty(...)  → enabled → SetActive / 其余 → Rebuild
            RemoveComponent<Collider>() → Adapter::Unbind() → Body destroyed
```

- **Integration Hook**：`GameObject` 组件生命周期监听器（默认空，零行为变化）；通知点
  Attach/RemoveComponentByName/模板路径/~GameObject/OnDestroy。
- **变更钩子**：`ColliderComponent` 的 `ColliderMutationHook`（Physics-free）——直接 setter 与
  反射 `SetPropertyValue` 统一通知。
- **Adapter API**：`ObserveGameObject/UnobserveGameObject`。
- **结果**：Add→自动 Bind / Remove→自动 Unbind（已删除绝不复活）/ 属性变更→自动
  SetActive|Rebuild（BodyCount 恒 1 无泄漏）/ B4-4 顺序矩阵 CaseA-D 全过 / B4-5 负路径全过；
  全 9 套测试回归绿。

## B5 — Cold Restart（✅ 已完成，2026-08-28，裁决 Result A，见 EF-F2B-007）

```text
Author Collider → Save → Destroy runtime → Cold Start → Load Scene
→ Collider restored → Body rebuilt → Simulation works
```

**硬性断言**：保存的是 `ColliderComponent`（components[]），**绝无** Jolt BodyID / ShapeRef / World pointer 入盘。

**结果**：真实产品链路（CaptureScene→SaveSnapshotToFile→LoadSnapshotFromFile→InstantiateScene→
ObserveGameObject 自动 Bind）全过；**「Save 即快照」正式确立**——Serialized Component State =
Cold-Restart source of truth（未保存的 runtime mutate 不入盘、已删除 Collider 不复活、损坏数据
回退默认、未知类型实体存活）。test_physics 37→**43/43**。

## B6 — Physics Behavior Consistency（✅ 已完成，2026-08-28，裁决 Result A，见 EF-F2B-009）

> 编号与 Strategy Statement：B6 = “恢复出来的 Physics Body 行为上是否等价于从未冷启动的
> Body”。不再只证 “字段恢复”，而是证 “恢复的 Static Collider 在真实 Physics 层实际产生的行为”。

**观测机制**：经 `Box2DPhysicsWorld::CreateBody` 直接投一枚【动态探针】，落在 collider 上。
探针是否被挡 / 落点多高 / 是否穿透，由 collider 的 shape / sensor / enabled / category / mask 驱动
——即对**物理行为**（而非字段回声）的直接观测。A（连续运行）/ B（真冷重启：Save→destroy→全新
Scene/GameObject/World→自动 Bind→同输入）对拍 trace（t=0..2s，dt=1/60），要求等价（显式 epsilon）。

**覆盖（`tests/test_physics/ColliderBindingTest.cpp`，+11 → test_physics 43→54）**：
- **B6-1** A/B trace 对拍（Circle）：探针轨迹 ip 逐帧等价 + representation（pos/radius/sensor/filter/active）对齐。
- **B6-2** Circle / Box 双路径：两 ShapeDef 分支行为等价 + 形状参数回读一致。
- **B6-3** Sensor：sensor collider → 探针**穿透**（不产生实体物理响应）；normal → 阻挡；冷重启后行为一致。
- **B6-4** Filter：collider cat/mask=0x0002 只与 Player 碰撞；探针 cat=0x0002 → 被挡 / cat=0x0004 →
  穿透（即使几何重叠）——证明 Physics**实际使用**恢复的 category/mask，非字段回声。
- **B6-5** Mutation→Restart：radius=2 保存冷启 → 行为 == radius=2；enabled=false 保存冷启 → Body 保持 disabled（探针穿透）。
- **B6-6** 三路对拍：A 连续 / B 中途 Destroy→Rebuild / C 冷重启 → A≈B≈C。
- **B6-7** 稳定性：Cold Restart ×10 → BodyCount 恒 1、m_Tracked 恒 1、trace 不漂移、无 duplicate/无 ghost。
- **B6-8** 负路径：非法 radius/half≤0 干净失败（不 crash / 无 ghost）；World 销毁后查询旧 body 安全；重复 Observe 幂等。

**B6 暴露并修复的真实缺陷（+EF-F2B-009 登记）**：`Box2DPhysicsBody::ClearFixtures()` 曾只清
`m_Shapes` 追踪的 fixture，而未销毁 `CreateBody` 经 `CreateShapesFromBodyDef` 生成的**初始实心 shape**。
Adapter 的 GAP-1 规避（clear+重加 sensor/filter）因此残留一个实心 shape → sensor=1 的新 shape 与初始
实心 shape 并存，后者照常参与碰撞 → **sensor/filter 行为在 Physics 层失效**（字段看似正常）。已修复：
`ClearFixtures()` 现经 `b2Body_GetShapeCount/GetShapes/b2DestroyShape` 清空 body 上**全部** shape。
B2-B5 未暴露是因为此前只读 FirstShape（新 shape 在链首）断言字段，从未做真交互行为观测。

## B7 — 双模型压力测试（待实现）

Delete GameObject A → Collider A / Body A 全灭；新建 GameObject B → **不错误复用旧物理 identity**。
若出现生命周期无法唯一归属 → **直接 Result C，不在 Adapter 层打补丁**。

## B8 — Result 裁决（最终只允许三种）

- **A — Foundation Ready**：契约无污染 · 单向映射 · ownership 唯一 · Destroy/Rebuild · Cold Restart ·
  Remove · 行为一致 · 双模型无歧义 → PROMOTE。
- **B — Localized Gap**：架构基本成立，局部缺口（某属性不可动态更新 / 某 shape 暂缺 / Editor 行为缺失）→ 登记下阶段。
- **C — Architecture Gap**：ownership 无法唯一 / 生命周期无法对齐 / World 销毁后存在无法管理的对象 /
  Collider 必须依赖 Physics 内部结构 / Cold Restart 无法重建 → **停止 F2-B**。

## 本阶段明确不做

❌ Prefab · ❌ RigidBody · ❌ Collision Events API · ❌ Editor Inspector（F2-E） · ❌ UI ·
❌ RenderGraph · ❌ 扩展 Circle/Box 之外 shape · ❌ 为测试修改 Gameplay · ❌ 把 Physics 内部类型暴露进 Contract

---

## F2-B 证据包（最终形态）

```text
docs/Engine-Foundation-F2B-Plan.md     ← 本文件（B0+B1+B2-B8）
docs/Engine-Foundation-F2B-Closure.md  ← 结束时写（含 Result 裁决）
Component-Ledger.md §F2 Ledger         ← EF-F2B-001/002（已登记）
tests/                                ← F2ColliderPhysics*（B3-B7 各一条链）
```

**完成后**才更新：Component-Maturity-Matrix · Component-Ledger · Evidence-Baseline · Engine-Foundation-Phase-Plan。
