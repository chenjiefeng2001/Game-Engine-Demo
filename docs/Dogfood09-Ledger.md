# Dogfood-09 Ledger — Multi-Scene / Reference Integrity

> 日期：2026-08-24 · 场景：双场景共享资产 + rename/move 传播 + 跨进程恢复 + 失败局部性
> 载体：`Dogfood09.MultiScene_ReferenceIntegrity` + `Dogfood09.FailureContract_LocalAndDiagnosable`
> 定位：M5 前的规模边界压力测试——攻击 Content/Scene v1 冻结契约的引用传播面

---

## 总评

**八道门禁（R1–R8）一次通过，零引擎缺陷暴露。**
Content Pipeline v1 的"场景只存 GUID、路径是注册表解析信息"身份模型在
多场景规模下成立。**DL-02 确认为 Editor 实现细节，未随规模爆炸。**

```text
96/96 PASS · Gate I1(21 files)+I2+I3(DF01-09) ALL GREEN
```

## 门禁结果

| Gate | 验证内容 | 结果 |
|------|---------|------|
| R1 | 双场景共享同一 Texture/Script，GUID 唯一稳定，registry 恰好 2 条目 | ✅ |
| R2 | `Unregister`+`RegisterExplicit` 构成重命名原语；同 GUID 解析到新路径；**场景文件字节级不变** | ✅ |
| R3 | 场景 JSON 无 `.png`/`.lua` 路径泄漏，以 GUID hex 引用 | ✅ |
| R4 | 改 A 并重存 → B 文件字节级不变、内容不受影响 | ✅ |
| R5 | 全新 registry 冷启动 → 双场景实体数/位置/绑定全部恢复 | ✅ |
| R6a | 场景文件损坏 → Load 干净失败 + err 非空 + 注册表无恙 + 同会话 good 场景照常加载 | ✅ |
| R6b | 注册表缺 sprite+script → ok=true、实体存活、2 条 warning 点名实体、binding 保持 Null 可见 | ✅ |
| R7 | GetAllEntries ↔ ResolvePath/TypeOf ↔ Instantiate bindings 三方逐项一致 | ✅ |
| R8 | rename 后旧路径 Import 得新 GUID（不复活旧身份）、新路径 Import 幂等原 GUID，无漂移 | ✅ |

## DL-02 规模探针结论

mid-session 向 Scene A 追加实体：
- A 保存后含 3 实体，B 保持 2 实体——**增员不跨场景扩散**
- 新实体的 spriteGuid 为 Null（调用方未提供 binding 行）→ 行为可预期、可见

**判定**：DL-02 在多场景下行为局部且确定，是编辑器维护 binding 表的实现责任，
不构成引擎架构缺口。维持观察，不升级。

## 发现的非缺陷但值得记录的行为

1. **rename 后 manifest 需显式重存**——`SaveManifest` 是快照式的，注册表内变更
   不会自动落盘。真实编辑器需在 rename 动作后触发保存。属 Editor Workflow
   职责（与 DL-02 同类），非引擎缺口。
2. **旧路径 rename 后重新 Import 会生成全新 GUID**——语义正确（那是"另一个资产"），
   但若用户误操作（rename 后又把文件改回原名并 Import），会得到两个条目指向
   两份历史。当前由 Browser 幂等提示承载即可。

## Deferred Ledger 复核

无新增信号。M002/M003/Undo-Redo/CWD 维持原状。
Content Pipeline v1 与 Editor Workflow v1 的边界在本次测试中进一步清晰：
**引擎侧身份/引用契约完备；风险集中在编辑器侧的状态同步责任**（binding 表、
manifest 落盘时机）——这两项已登记为 Vertical Slice 前的编辑器待办观察。

## 下一步：Vertical Slice 准备就绪

DF01–DF09 已覆盖：API/资源链/复杂度/DX 审计/AI/战斗/可维护性/长会话/规模引用。
按既定路线，下一道门是一款**有完整内容生产周期的中小型游戏 Vertical Slice**——
验证对象从"引擎有没有能力"切换为"冻结契约能否支撑真实产品"。
