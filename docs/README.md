# Game-Engine-Demo

## 最新更新

### 多线程渲染器架构重构

我们正在对整个渲染系统进行重大重构，目标是实现一个基于 RHI（渲染硬件接口）的多线程渲染架构。当前进度：

- [x] IRenderContext 抽象接口定义
- [x] 多线程 Command Buffer 录制原型
- [x] GPU 时间戳查询框架
- [ ] Vulkan RHI 后端实现（进行中）
- [ ] 资源流送系统（计划中）

#### 新的渲染管线架构

```
Application
  └─ RenderThread (独立渲染线程)
       ├─ Frame Begin
       ├─ GPU Timestamp Query Start
       ├─ Scene Render Passes
       │    ├─ Shadow Map Pass
       │    ├─ Depth Pre-Pass
       │    ├─ Geometry Pass (Deferred)
       │    ├─ Lighting Pass
       │    └─ Post Processing
       ├─ GPU Timestamp Query End
       └─ Frame End (Swap)
```

### 依赖项更新

- **spdlog**: 更新到 1.13.0
- **stb**: 更新到最新版本 (2024-07-01)
- **glad**: 更新到 2.0.6

### 新增功能

1. **日志系统增强**
   - 支持多日志级别过滤
   - 控制台彩色输出
   - 文件日志轮转

2. **内存分配器优化**
   - StackAllocator 性能提升 30%
   - 新增 MemoryTracker 调试工具

3. **编辑器框架改进**
   - 完整的 Docking 布局支持
   - ImGuizmo 集成
   - 多视图渲染

## 构建说明

### 前提条件

- CMake 3.20+
- C++20 编译器
- Visual Studio 2022 (Windows)
- Vulkan SDK (可选，用于 Vulkan 后端)

### 快速开始

```bash
# Windows
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# Linux
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## API 参考

详细的 API 文档请参考 [API Reference](docs/API.md)。

## 贡献指南

请参考 [CONTRIBUTING.md](CONTRIBUTING.md)。