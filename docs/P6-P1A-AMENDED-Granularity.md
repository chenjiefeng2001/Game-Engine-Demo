# P6-P1A-AMENDED-Granularity — 资产粒度与幂等语义修订记录（C-1 / C-2）

**Status**: APPROVED — AMENDMENT EFFECTIVE FROM P6-1B（2026-11-05 用户裁决）  
**Amends**: `docs/P6-P1A-Asset-Import-Contract.md` §2（粒度）与 §6.3（hash 触发更新）  
**不变式**: 原始 P6-1A 文档保持不可变；本记录为独立修订，历史记录不追改。

---

## A. C-1 — Mesh 资产粒度修订（APPROVED 方案 (a)）

**原冻结**（P6-1A §2）：一个 glTF primitive / OBJ mesh 组 = 一个 Mesh asset。  
**修订后（自 P6-1B 生效）**：

1. **一个源文件 = 一个 Mesh asset**；Mesh asset 内部按源 primitive 划分 spans（锚点：生产渲染收集链为 `MeshComponent`，见 B0 地图 §1.5）。
2. 每个 primitive 对应一个**稳定顺序 span**（记录 firstIndex/indexCount/源材质槽关联）。
3. 单 primitive 资产使用一个默认 span；**程序生成 Mesh 与既有单 Mesh 行为不变**（默认 span 语义零差异）。
4. 多材质源文件：**全部源材质仍注册为 Material assets**；v1 渲染使用首材质；其余材质的描述与 span 关联必须保留，并发出显式 warning（列出未参与渲染的材质）；**禁止静默丢弃材质定义或 primitive→材质槽关系**。
5. Mesh 与其 Material assets 纳入**同一次 `RevisionStore::Publish`**；若 candidate 无法原子提交该组资源，B3 停止并报告，**不得拆分多次 Publish**。

被取代的注册表命名（B3 采用）：

```
assets/imported/model/<stem>/<stem>.mesh          （一源文件一 Mesh asset）
assets/imported/model/<stem>/<stem>.material_<r>  （每源材质一个 Material asset）
```

不对历史原合同 §2 内容做改写；合同冲突判定在 B0 地图 C-1 处已有双选建议，本记录即裁决落点。

## B. C-2 — Hash 幂等语义（APPROVED；B3 enforcement）

- 同路径重导 **hash 未变** → 返回既有 GUID，不产生新 revision。
- **hash 已变** → 保留既有 GUID，提交新 revision（旧 GUID 不复用、不换绑定）。
- Publish 失败 → 旧 current/旧内容保持可用，不暴露半提交状态。
- `ContentRegistry::Import` 同路径快路径不再无条件返回；必须先取得源内容 hash（FNV-1a 64，复用 `RevisionStore::HashBytes`）与已提交版本比较。

**B3 强制原子性判定（不早于其完成宣称）**：当前复制时序先于 Publish（B0 地图 §1.1）；对"同路径、内容已变化"的 reimport，必须使用独立暂存/版本化内容路径，或提供 Publish 失败时旧 current 所指旧字节仍完整的等效证据；**禁止先覆盖旧文件再依赖事务结果兜底**。

## C. 其余裁决（无修订）

- C-3 stb 全局 flip：v1 不改；B1 起 UV 翻转补偿在**解析阶段**完成（单一转换点，见 B1 记录）。
- C-4 ABI：暂不批准；B6 以 MeshComponent + scene JSON additive 字段完成并给出证据后再裁定。
- U-1/U-2 → B5；U-4/U-5 → B6（均不阻塞 B1）。
EOF
