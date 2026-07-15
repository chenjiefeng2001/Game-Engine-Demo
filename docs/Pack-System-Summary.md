# 打包系统与附属组件总结报告 v2

> **生成日期**: 2026-07-15 (v2 迭代)  
> **分析范围**: Pak 文件格式、VFS 挂载系统、资源管理层（ResourceManager/Resource/ResourceRegistry）、异步流、文件监视、内存池分配器

---

## 一、架构总览

打包与资源管理系统分为四层：

```
┌──────────────────────────────────────────────────────────────────────┐
│  资源管理层 (Resource Management Layer)                               │
│  ResourceManager ─ Resource ─ ResourceRegistry ─ ResourceRef          │
│  模板加载 / 异步加载 / 热重载 / 缓存 / 交叉引用 / 内存预算              │
├──────────────────────────────────────────────────────────────────────┤
│  存储抽象层 (Storage Abstraction Layer)                               │
│  FileSystem (VFS) ─ IMountBackend ─ IFile                             │
│  挂载点解析 / 路径标准化 / 主线程派发器 / 异步 I/O                     │
├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
│  ⚠️ 抽象断裂层 — ResourceManager 和 AsyncStream 直接 fopen            │
│     不走 FileSystem::OpenFile，使整个 VFS 层形同虚设                   │
├─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┤
│  后端实现层 (Backend Implementation Layer)                            │
│  OsDirectoryMount ─── PakArchiveMount ─── AsyncStream                 │
│  OS目录直读          Pak文件TOC索引      ⚠️ 未接入 VFS                 │
├──────────────────────────────────────────────────────────────────────┤
│  打包工具层 (Pack Tool Layer) ⚠️ 缺失                                  │
│  无 Pak 打包工具 / 无资源烹饪管线 / 无构建集成                          │
└──────────────────────────────────────────────────────────────────────┘
```

### **最严重的架构坏味道：VFS 抽象被绕过**

当前 `ResourceManager::Load<T>(path)` 内部直接执行 `fopen` → `fread` → `fclose`，而不是通过 `FileSystem::OpenFile` → `IFile::Read`。

```cpp
// ❌ 当前行为：绕过 VFS，直接 OS 文件读取
FILE* fp = fopen(path, "rb");
fread(buffer, 1, size, fp);
fclose(fp);

// ✅ 正确行为：通过 VFS 解析，不关心底层是 OS 目录还是 Pak
auto file = FileSystem::OpenFile(path);  // 内部经 ResolvePath → 查找挂载点
if (!file) return nullptr;
file->Read(buffer, size);                // IFile 抽象，透明处理压缩/偏移
```

**后果**：即使 `PakBuilder` 工具就绪、`PakArchiveMount` 挂载成功，资源加载依然走 OS 文件系统。**VFS 层沦为摆设。**

### 各子组件一览

| 类别 | 组件 | 源文件 | 行数 | 完成度 | 关键缺口 |
|------|------|--------|------|--------|---------|
| **打包格式** | PakFile（格式定义 + TOC 解析） | `Core/PakFile.h` + `src/Core/PakFile.cpp` | ~442 | 🟡 60% | 压缩/加密/内存映射未实现 |
| **VFS 核心** | IFile / IMountBackend | `Core/IFile.h` | ~117 | 🟢 90% | 缺少 Write 语义 |
| **VFS 核心** | FileSystem（挂载/路径/异步I/O） | `Core/FileSystem.h` + 实现 | ~231 (声明) | 🟢 85% | **无挂载优先级**（Mod/DLC 需要） |
| **OS 后端** | OsDirectoryMount | `Core/OsFileMount.h` | ~119 | 🟢 100% | — |
| **异步流** | AsyncStream | `Core/AsyncStream.h` + 实现 | ~152 (声明) | 🟡 60% | **未接入 VFS / 未接入 Pak** |
| **资源基类** | Resource | `Resources/Resource.h` | ~268 | 🟢 95% | 内存估算默认 0 |
| **资源管理** | ResourceManager | `Resources/ResourceManager.h` + 实现 | ~578 (声明) | 🟡 70% | **不感知 VFS / 无流式加载** |
| **注册表** | ResourceRegistry | `Resources/ResourceRegistry.h` + 实现 | ~387 (声明) | 🟢 90% | — |
| **GUID** | ResourceGUID | `Resources/ResourceGUID.h` + 实现 | ~101 | 🟢 100% | — |
| **内存池** | ResourcePoolAllocator | `Resources/ResourcePoolAllocator.h` + 实现 | ~221 (声明) | 🟢 90% | — |
| **文件监视** | FileWatcher | `Resources/FileWatcher.h` + 实现 | ~122 (声明) | 🟡 60% | **不适用于 Pak 模式** |
| **打包工具** | ❌ 不存在 | — | 0 | 🔴 0% | **无 PakBuilder** |

---

## 二、各模块详解与评估

### 2.1 Pak 打包文件格式

**文件**: `Core/PakFile.h`（格式定义 + TOC 加载）+ `src/Core/PakFile.cpp`（~219 行实现）

#### 格式定义

```
┌─────────────────────────────────────┐
│ Header (32 bytes)                   │
│   Magic: "EGNP" (4 bytes)           │
│   Version: uint32                   │
│   TocOffset: uint64                 │
│   TocSize:   uint64                 │
│   Reserved:  [8 bytes]              │
├─────────────────────────────────────┤
│ File Entry Data (按写入顺序排列)      │
│   [chunk0 data][chunk1 data][...]   │
│   （每个文件可能被分为多个 64KB 块）   │
├─────────────────────────────────────┤
│ TOC (Table of Contents)             │
│   EntryCount: uint32                │
│   [ each entry:                     │
│     hash: uint64                    │
│     pathHash: uint64 (debug)        │
│     offset: uint64         ← 数据区  │
│     originalSize: uint64   ← 未压缩  │
│     storedSize: uint64     ← 存储后  │
│     flags: uint32 (压缩/加密/分块)   │
│     reserved: uint32                │
│     path: [...null-terminated]      │
│     chunkTable: [                   │
│       {chunkOffset, chunkSize} ...  │
│     ]  ← 分块索引（仅分块文件有）    │
│   ]                                 │
├─────────────────────────────────────┤
│ Footer (8 bytes)                    │
│   Magic: "EOPK"                     │
│   Version: uint32                   │
└─────────────────────────────────────┘
```

**格式设计的优点**：TOC 放在文件末尾（类似 ZIP 的 Central Directory），这意味着：
- 流式写入时无需预先知道 TOC 大小
- 热更新时可以向文件尾部追加新资源，覆写 TOC 即可——无需重构整个包
- 启动时只需加载 Footer→TOC，无需扫描整个文件

#### 核心功能

| 功能 | 状态 | 说明 |
|------|------|------|
| **TOC 加载** | 🟢 完整 | Footer → Header → TOC 数据 → 哈希索引，流程完整 |
| **条目查找** | 🟢 完整 | `FileExists` / `OpenFile` / `GetFileSize` 通过 `StringID(HashString64)` 哈希查找 |
| **条目读取** | 🟢 完整 | `PakEntryFile` 适配 `IFile` 接口，基于 `FILE*` + `fseek` |
| **TOC 枚举** | 🟢 完整 | `ListPaths()` 返回所有路径（排序后） |
| **LZ4 压缩** | 🟡 预留 | `PakFlags::Compressed` 标志位已定义，但 `PakEntryFile::Read()` 中无解压逻辑 |
| **Zstd 压缩** | 🔴 预留 | `PakFlags::Zstd` 标志位已定义，未实现 |
| **分块压缩** | 🔴 未实现 | 当前整个文件作为一个单位，不支持 64KB Chunked 分块 |
| **AES 加密** | 🔴 预留 | `PakFlags::Encrypted` 标志位已定义，未实现 |
| **内存映射** | 🔴 未实现 | 当前使用 `FILE*` + `fread`，无 `mmap` / `MapViewOfFile` |

#### 代码质量

| 维度 | 评估 | 说明 |
|------|------|------|
| **RAII** | 🟢 良好 | 构造 `fopen`，析构 `fclose` |
| **边界检查** | 🟢 良好 | TOC 解析时有截断检查、路径长度检查、魔数校验、版本校验 |
| **跨平台** | 🟡 中等 | `_WIN32` 条件编译 `fopen_s`/`_fseeki64` vs `fopen`/`fseeko` |
| **哈希碰撞** | 🔴 未处理 | `HashString64` 无碰撞检测/二次探测——**需要升级哈希算法** |

#### 🔴 重点隐患：哈希碰撞

当前使用 64 位 `HashString64`（FNV-1a 变体）作为 TOC 索引键。

**问题**：随着项目资产量增长到数万级别，64 位哈希发生碰撞的概率不可忽略。一旦碰撞：
- `OpenFile("assets/textures/hero.png")` 可能错误地返回另一个文件的 `IFile`
- 错误资源加载后可能在运行时触发 GPU 异常、物理异常等难以诊断的 Bug
- **无法通过运行时检查发现**，因为引擎认为"查找成功"

**修正方案**：

| 方案 | 说明 |
|------|------|
| **128 位哈希** | 改用 `MurmurHash3-128` 或 `CityHash-128`，碰撞概率降低到可以忽略不计（128 位空间足够容纳宇宙中所有文件） |
| **构建时碰撞检测** | `PakBuilder` 在打包阶段对所有文件计算哈希并检查唯一性，若碰撞则**构建失败**并报错冲突路径 |
| **双重验证** | TOC 中保留完整路径字符串，哈希查找后 `strcmp` 确认路径一致再返回（当前代码已存储 path 字段但未做二次验证） |

**推荐路径**：方案 1 + 方案 2（即使用 128 位哈希 + 构建时碰撞检查），既消除运行时碰撞概率，又将错误提前到构建期捕获。

---

### 2.2 VFS 核心层

#### 2.2.1 IFile / IMountBackend（`Core/IFile.h`）

```cpp
class IFile {
    virtual Read(void*, size_t);     // 读取
    virtual Seek(size_t);            // 定位
    virtual Tell() const;            // 当前位置
    virtual GetSize() const;         // 文件大小
    virtual IsEOF() const;           // 是否末尾
};

class IMountBackend {
    virtual GetName() const;
    virtual FileExists(path);        // 文件存在性
    virtual OpenFile(path) → IFilePtr; // 打开文件
    virtual GetFileSize(path);
    virtual SupportsEnumeration();   // 是否支持目录扫描
};
```

| 评估 | 说明 |
|------|------|
| 🟢 良好 | 接口简洁（4 + 4 方法），无冗余抽象 |
| 🟡 待补 | `IFile` 缺少 `ReadLine` / `ReadAll` 便捷方法；`IMountBackend` 缺少 `WriteFile`（Pak 后端可以只读，但 OS 后端需要） |

#### 2.2.2 FileSystem（`Core/FileSystem.h` + 实现）

| 功能域 | 完成度 | 说明 |
|--------|--------|------|
| **挂载点管理** | 🟢 完整 | `Mount(name, backend)` / `Unmount` / `FindMount` |
| **路径解析** | 🟢 完整 | `mountName:path` 语法 + 相对/绝对路径 |
| **路径工具** | 🟢 完整 | 10+ 方法（GetFileName/GetStem/GetExtension/Combine/Normalize...） |
| **同步 I/O** | 🟢 完整 | ReadFile/ReadTextFile/WriteFile/WriteTextFile |
| **异步 I/O** | 🟢 完整 | `ReadFileAsync` 基于 `JobSystem`，优先级 + 主线程派发 |
| **目录扫描** | 🟢 完整 | `ScanDirectory` / `ScanFiles` 支持 glob 和递归 |
| **主线程派发** | 🟢 完整 | `SubmitToMainThread` + `PollMainThreadTasks` |
| **🔴 挂载优先级** | 🔴 缺失 | 当前 `Mount(name, backend)` 没有优先级参数 |

#### 🔴 重点缺失：挂载优先级（Mod / DLC 支持）

当前 `Mount` 按添加顺序查找，后挂载的无法覆盖先挂载的同名文件。

**工业参考（Godot .pck 优先级）**：Godot 允许运行时动态挂载多个 `.pck`，高优先级的 PCK 覆盖低优先级的同名文件。这在 Mod 和 DLC 场景中至关重要。

**修正方案**：

```cpp
// 当前 API
static void Mount(std::string_view name, MountBackendPtr backend);

// 修正后 API（加入优先级）
static void Mount(std::string_view name, MountBackendPtr backend,
                  int32 priority = 0);   // 优先级：越大越优先

// 使用示例
FileSystem::Mount("base", std::make_unique<PakArchiveMount>("game.pak"), 0);
FileSystem::Mount("dlc1", std::make_unique<PakArchiveMount>("dlc1.pak"), 10);  // 覆盖 base
FileSystem::Mount("mod",  std::make_unique<PakArchiveMount>("mod.pak"),  20);  // 覆盖 base + dlc1

// ResolvePath 行为：按优先级降序遍历，返回第一个匹配的
```

#### 2.2.3 OsDirectoryMount（`Core/OsFileMount.h`）

| 功能 | 状态 |
|------|------|
| `IFile` 适配（`FILE*` 包装） | 🟢 完整 |
| 路径解析（`std::filesystem`） | 🟢 完整 |
| 跨平台 `fopen`/`fseeki64` | 🟢 完整 |

---

### 2.3 异步流层

#### AsyncStream（`Core/AsyncStream.h` + 实现）

| 功能 | 完成度 | 说明 |
|------|--------|------|
| 分块读取 | 🟢 完整 | 支持指定 offset + size |
| 后台线程 | 🟢 完整 | 独立工作线程串行处理 |
| LRU 块缓存 | 🟢 完整 | 最多 8 块（512KB），有 CacheHit/Miss 统计 |
| 取消/关闭 | 🟢 完整 | Cancel / Close |
| **🔴 接入 VFS** | 🔴 缺失 | 当前直接 `fopen`，不走 `FileSystem::OpenFile` |
| **🔴 分块解压** | 🔴 缺失 | 若 Pak 使用分块压缩，需支持按需解压单个块 |

**与 Pak 系统的关系**：当前 `AsyncStream` 的构造函数直接执行 `fopen`，获取 OS 文件句柄后在工作线程中读取。这意味着：

```cpp
// ❌ 当前
bool AsyncStream::OpenInternal(const std::string& path) {
    m_File = fopen(path.c_str(), "rb");  // 绕过 VFS
}

// ✅ 正确
bool AsyncStream::OpenInternal(const std::string& path) {
    m_IFile = FileSystem::OpenFile(path);  // 经 VFS 解析
    if (!m_IFile) return false;
    m_FileSize = m_IFile->GetSize();
}
```

---

### 2.4 资源管理层

#### 2.4.1 Resource 基类（`Resources/Resource.h`）

| 功能 | 状态 | 说明 |
|------|------|------|
| 路径即标识 | 🟢 | 所有资源通过唯一路径索引 |
| 状态机 | 🟢 | Unloaded → Loading → Loaded → Resolving → Ready / Failed |
| GUID 支持 | 🟢 | 128 位 UUID v4，全局唯一 |
| 依赖解析 | 🟢 | `GetDependencies()` + `ResolveDependencies()` 递归加载 |
| 后初始化 | 🟢 | `PostLoad(IGraphicsFactory*)` GPU 上传 |
| 热加载 | 🟢 | `Reload()` + `BumpReloadVersion()` |
| 引用计数 | 🟢 | `shared_from_this().use_count()` 诊断 |
| 内存估算 | 🟡 | `EstimatedMemoryBytes()` 默认 0，子类需重写 |

#### 2.4.2 ResourceManager（`Resources/ResourceManager.h` + 实现）

| 功能 | 状态 | 说明 |
|------|------|------|
| 模板加载 | 🟢 | `Load<Texture>(path)` 统一入口 |
| 双路径加载 | 🟢 | `Load<Shader>(vert, frag)` |
| 缓存 + 弱引用 | 🟢 | `unordered_map<string, weak_ptr>` 自动清理 |
| 异步加载 | 🟢 | 独立后台线程 + 队列 + 完成回调 |
| 热加载回调 | 🟢 | `BindReloadCallback` / `PollHotReload` |
| 内存预算 | 🟢 | `SetBudget` / `EnforceBudgets` / LRU 淘汰 |
| GUID 注册表 | 🟢 | `ResourceRegistry` + `ResourceRef<T>` |
| **🔴 不感知 VFS** | 🔴 缺失 | **内部直接 `fopen`，不走 `FileSystem::OpenFile`** |
| **🔴 无流式加载** | 🔴 缺失 | 大资源全量读取到内存再处理，不支持分块流式 |

#### 🔴 重点断裂：ResourceManager 与文件系统的耦合

`ResourceManager::Load<T>(path)` 当前加载流程：

```
Load<Texture>("assets/textures/hero.png")
    │
    ├── 检查 m_Cache["assets/textures/hero.png"]
    │      └── 命中 → 返回缓存
    │
    ├── 未命中 → LoadByType<Texture>(path)
    │      │
    │      └── Texture::LoadFromFile(path)
    │             │
    │             ├── ❌ FILE* fp = fopen(path, "rb")      ← 绕过 VFS!
    │             ├── ❌ fread(...)                        ← 直接 OS 读取
    │             └── ❌ fclose(fp)                        ← 不关心挂载后端
    │
    ├── ResolveDependencies → Load<Material>(depPath)  ← Same problem
    ├── PostLoad → GPU Upload
    └── 注册到 m_Cache
```

**修正后流程**：

```
Load<Texture>("textures/hero.png")
    │
    ├── FileSystem::OpenFile("textures/hero.png")
    │      │
    │      ├── ResolvePath → "assets: textures/hero.png"
    │      │      │
    │      │      ├── dev:  OsDirectoryMount::OpenFile → FILE* → IFile
    │      │      └── ship: PakArchiveMount::OpenFile → hash → offset → IFile
    │      │
    │      └── 返回 IFilePtr（不关心底层是 OS 文件还是 Pak 条目）
    │
    ├── IFile::Read(buffer, size)   ← 透明读取（压缩由 PakEntryFile 处理）
    │
    ├── Resource 状态机 + 依赖解析
    └── PostLoad → GPU 上传
```

**关键原则**：`IFile` 必须成为整个引擎**唯一的 I/O 货币**。`ResourceManager` 不应该知道什么是文件路径，它只应该向 `FileSystem` 索要一个 `IFilePtr`，然后调用 `IFile::Read()`。

#### 2.4.3 ResourceRegistry（`Resources/ResourceRegistry.h`）

| 功能 | 状态 |
|------|------|
| GUID → 资源映射 | 🟢 |
| 路径 → GUID 反向索引 | 🟢 |
| ResourceRef<T> 强引用 | 🟢 |
| ResourceWeakRef<T> 弱引用 | 🟢 |
| 生命周期钩子 | 🟢 |
| 内存池分配器 | 🟢 |

#### 2.4.4 ResourcePoolAllocator

| 功能 | 状态 |
|------|------|
| 每个 ResourceType 独立池 | 🟢 |
| O(1) 分配/释放（空闲链表） | 🟢 |
| 自动扩展（新 Block） | 🟢 |
| Trim() 缩减 | 🟢 |
| DumpState() 崩溃报告 | 🟢 |

#### 2.4.5 FileWatcher

| 功能 | 状态 | 说明 |
|------|------|------|
| 后台轮询线程 | 🟢 | 独立 `std::thread`，`stat()` 轮询 mtime |
| 路径管理 | 🟢 | `Watch` / `Unwatch` / `Clear` |
| 变更队列 | 🟢 | 后台写 → 主线程消费 |
| **🔴 不适用于 Pak** | 🔴 缺失 | Pak 中文件无独立 mtime，需改为监视 `.pak` 文件自身的 mtime |

---

## 三、工业界参考

### 3.1 Unreal Engine — IoStore (Zen 架构)

UE 早期的 `.pak` 格式与本项目类似（TOC 尾部追加）。UE5 演进出的 **IoStore** 将 TOC 索引单独拆分为 `.utoc` 文件，数据放在 `.ucas` 文件中。

**对本引擎的启发**：
- **索引与数据分离**：当 TOC 很大时，启动时加载整个 TOC 会导致耗时和内存毛刺。未来可选择将 TOC 外置为 `.paktoc` 文件，运行时按需加载部分 TOC
- **分块压缩 (Chunked Compression)**：IoStore 使用 64KB 分块 + Zstd 压缩，流式读取任意偏移时只需解压涉及到的 1-2 个块

### 3.2 Godot — .pck 优先级

Godot 的 PCK 格式极其简单，头部是文件数量和索引。关键设计是**支持运行时动态挂载多个 `.pck`，按优先级覆盖**。

**对本引擎的启发**：
- `FileSystem::Mount` 必须加入 `priority` 参数
- Mod 和 DLC 只需挂载高优先级的 `.pak`，同名文件自动覆盖

### 3.3 Unity — Addressables GUID 寻址

Unity 弱化了文件路径，强化了 **GUID + Catalog** 系统。资源依赖在打包时被完全解析并序列化到全局 Catalog 中。

**对本引擎的启发**：
- 本项目已有 `ResourceGUID` 和 `ResourceRegistry`，基础完善
- 打包时可生成全局 `ResourceMap.bin`，记录 `GUID → { pakFile, offset, size }`
- 运行时通过 GUID 直接寻址，跳过字符串路径解析，加载速度更快

---

## 四、整体评估

### 4.1 完成度矩阵

| 模块 | 完成度 | 关键强度 | 最大缺口 |
|------|--------|---------|---------|
| **Pak 格式定义** | 80% | 二进制格式完整，TOC 尾部追加设计优秀 | 分块压缩 / 加密 / 内存映射未实现 |
| **Pak TOC 解析** | 85% | 加载/解析/哈希查找完整，边界检查齐全 | **🔴 64位哈希碰撞无处理** |
| **VFS 核心** | 90% | Mount/Unmount/ResolvePath/异步 I/O | **🔴 无挂载优先级**（Mod 支持） |
| **OS 后端** | 100% | 完整实现 | — |
| **异步流** | 60% | 分块读取/LRU 缓存 | **🔴 未接入 VFS / 未接入 Pak** |
| **ResourceManager** | 70% | 模板加载/异步/缓存/热重载回调 | **🔴 不感知 VFS（最严重断裂）** |
| **ResourceRegistry** | 90% | GUID/交叉引用/生命周期钩子 | — |
| **FileWatcher** | 60% | 后台轮询/变更队列 | **🔴 不适用于 Pak 模式** |
| **PakBuilder 工具** | **0%** | ❌ 不存在 | **最严重缺失** |

### 4.2 代码质量

| 维度 | 评估 | 说明 |
|------|------|------|
| **RAII** | 🟢 良好 | 智能指针 + 构造/析构配对 |
| **线程安全** | 🟢 良好 | 挂载互斥锁 / 异步加载条件变量 / 变更队列互斥锁 |
| **错误处理** | 🟢 良好 | 魔数校验、版本校验、截断检查、日志记录 |
| **C++20** | 🟢 良好 | string_view、atomic、optional、if constexpr |
| **跨平台** | 🟡 中等 | PakFile 条件编译，FileSystem 部分平台相关 |

### 4.3 改进建议 — 四阶段路线图

#### Phase 1: VFS 强制接管（P0 — 2~3 天）

**目标**：打通现有系统，使 IFile 成为引擎唯一的 I/O 货币。

| 任务 | 工时 | 说明 |
|------|------|------|
| **1.1 ResourceManager → VFS** | 1天 | 将 `Load<T>(path)` 内部从 `fopen` 改为 `FileSystem::OpenFile` → `IFile::Read` |
| **1.2 AsyncStream → VFS** | 0.5天 | 将 `OpenInternal` 从 `fopen` 改为 `FileSystem::OpenFile`，内部持有 `IFilePtr` |
| **1.3 挂载优先级支持** | 0.5天 | `Mount` 增加 `priority` 参数，`ResolvePath` 按优先级降序查找 |
| **1.4 强制审计** | 0.5天 | 全局搜索 `fopen` / `std::ifstream` / `CreateFile`，确保所有文件 I/O 都经过 VFS |

**验收标准**：
- ✅ `ResourceManager::Load<Texture>(path)` 内部通过 `FileSystem::OpenFile` → `IFile::Read` 完成
- ✅ `AsyncStream::Open` 通过 `FileSystem::OpenFile` 获取 `IFilePtr`
- ✅ `OsDirectoryMount` 和 `PakArchiveMount` 在 ResolvePath 中按优先级排列
- ✅ 全引擎零 `fopen`/`CreateFileA` 直接调用（可通过 grep 验证）

#### Phase 2: PakBuilder MVP + 哈希升级（P0 — 2~3 天）

**目标**：实现完整的 `.pak` 生成与加载闭环。

| 任务 | 工时 | 说明 |
|------|------|------|
| **2.1 哈希算法升级** | 0.5天 | `HashString64` → `CityHash128` 或 `MurmurHash3-128`，更新 TOC 和 `PakFile.h` |
| **2.2 PakBuilder 工具** | 1.5天 | 实现 `tools/PakBuilder.cpp`：扫描目录 → Pass1 收集文件 + 碰撞检测 → Pass2 写入数据 → Pass3 序列化 TOC → Pass4 回写 Header |
| **2.3 TOC 二次验证** | 0.5天 | 哈希查找命中后 `strcmp(entry.path, requestedPath)` 二次确认 |
| **2.4 CMake 集成** | 0.5天 | `add_custom_target(PakAssets ...)` post-build 自动打包 `assets/ → test.pak` |

**验收标准**：
- ✅ `PakBuilder --input assets/ --output game.pak` 成功生成可读 `.pak` 文件
- ✅ 构建时若检测到哈希碰撞则**构建失败**并报冲突路径
- ✅ `PakArchiveMount` 加载 `game.pak` 后 `OpenFile` 返回正确数据
- ✅ `ResourceManager` 从挂载的 Pak 中透明加载纹理/音频

#### Phase 3: 性能飞跃（P1~P2 — 3~5 天）

**目标**：加入压缩和内存映射，大幅降低磁盘占用和加载时间。

| 任务 | 工时 | 说明 |
|------|------|------|
| **3.1 Zstd 分块压缩** | 2天 | 在 `PakBuilder` 中添加 `--compress zstd` 和 `--chunk-size 64KB` 选项；`PakEntryFile::Read()` 实现按需解压单个块 |
| **3.2 内存映射** | 1天 | `PakArchiveMount::OpenFileMapped()` — Win32 `CreateFileMapping` / POSIX `mmap`；`IFileMapped` 接口：`Read` 返回指针而非拷贝 |
| **3.3 FileWatcher Pak 模式** | 0.5天 | 当 Pak 挂载时，监视 `.pak` 文件的 mtime 而非内部文件；检测到变更 → 触发整个 Pak 热重载 |
| **3.4 ResourceManager 流式加载** | 1.5天 | `LoadStreaming<T>(path)` 分块加载大资源，回调在块就绪时通知（如渐进式纹理加载） |

**验收标准**：
- ✅ Zstd 分块压缩后的 `.pak` 体积减小 40-60%（典型值）
- ✅ 流式加载大纹理时内存峰值从"全量 100MB"降至"分块 1MB"
- ✅ 内存映射模式下 `IFile::Read` 零拷贝（直接返回映射指针）
- ✅ Pak 热重载：修改 `.pak` 文件 → FileWatcher 触发 → `ResourceManager::Reload` 所有受影响资源

#### Phase 4: Asset Cooking 管线（远期）

**目标**：从"原样打包源文件"升级为"平台特定烘焙 + 依赖图预计算"。

```
[美术资产 .png / .fbx / .wav]
       │
       ▼
[Asset Cooker]
  ├── 贴图: .png → .dds (BC7) / .ktx (ASTC)
  ├── 模型: .fbx → 自定义二进制(预蒙皮/预LOD)
  ├── 音频: .wav → .ogg → 预解码 PCM
  └── 场景: .json → 预计算 GUID 依赖图
       │
       ▼
[Asset Database] (GUID → 输出文件映射)
  ├── 去重 (同一张贴图被多个材质引用只烘焙一次)
  ├── 增量构建 (只重烘焙变更的文件)
  └── 依赖追踪 (检测到贴图变更 → 重烘焙引用它的材质)
       │
       ▼
[PakBuilder]
  ├── 输入: Cooked 资产目录 + AssetDatabase
  ├── 输出: game.pak + ResourceCatalog.bin
  └── ResourceCatalog.bin: GUID → { pakFile, offset, size, deps }
```

**与本项目现有基础设施的关系**：
- `ResourceGUID` 已存在，可作为 Asset Database 的标识符
- `Resource::GetDependencies()` / `ResolveDependencies()` 已存在，可作为依赖图的基础
- `ResourcePoolAllocator` 可管理 Cooked 资产的运行时内存

---

## 五、总结

| 维度 | 评估 |
|------|------|
| **整体完成度** | **~60%** — VFS 核心 + 资源管理层架构优秀，但存在严重集成断裂 |
| **架构强度** | **强** — IMountBackend 可插拔、ResourceManager 模板加载/异步/缓存/热重载对齐工业标准 |
| **最严重断裂** | **VFS 被上层绕过** — ResourceManager 和 AsyncStream 直接 `fopen`，使 VFS 抽象层形同虚设 |
| **哈希安全** | **🔴 64 位哈希碰撞风险** — 13 亿资产量级下碰撞概率不可忽略，需升级 128 位 + 构建时检测 |
| **缺失元素** | PakBuilder 工具 / 挂载优先级 / Zstd 分块压缩 / 内存映射 / Asset Cooking |
| **修复路径** | Phase 1 (VFS 接管) → Phase 2 (PakBuilder MVP) → Phase 3 (压缩/映射/流式) → Phase 4 (Cooking) |
| **修复成本** | **~5~10 人天**（Phase 1~3 核心能力） |