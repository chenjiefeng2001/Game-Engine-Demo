# Game Engine Demo

<p align="center">
  <h1 align="center">🎮 Game Engine Demo</h1>
  <p align="center">
    一个实验性的游戏引擎演示项目，用于技术验证与快速游戏原型搭建。
    <br />
    <a href="#快速开始">快速开始</a>
    ·
    <a href="#项目结构">项目结构</a>
    ·
    <a href="#注意事项">注意事项</a>
    ·
    <a href="#贡献">贡献</a>
    ·
    <a href="#许可证">许可证</a>
    <br />
    <br />
    <img src="https://img.shields.io/badge/status-experimental-red" alt="status: experimental"/>
    <img src="https://img.shields.io/badge/stability-unstable-orange" alt="stability: unstable"/>
    <img src="https://img.shields.io/badge/license-MIT-blue" alt="license: MIT"/>
  </p>
</p>


## 📖 简介

**Game Engine Demo** 是一个正在开发中的轻量级游戏引擎，主要用于探索渲染、物理、脚本等系统的实现方式，并快速验证一些游戏想法。  
由于项目处于早期试验阶段，代码架构频繁调整，**尚未达到可用状态**，不建议直接用于正式项目。

## ✨ 已实现功能（持续更新中）

- 基础渲染管线（OpenGL / Vulkan 等）
- 简单场景管理与实体系统
- 输入处理与窗口管理
- 基本物理模拟集成
- 资源加载与缓存原型

> 更多特性请查看 [CHANGELOG.md](./CHANGELOG.md)

## 🚀 快速开始

### 环境要求

- CMake ≥ 3.15
- C++17 编译器
- [可选] Vulkan SDK / OpenGL 依赖库

### 克隆与构建

```bash
git clone https://github.com/chenjiefeng2001/game-engine-demo.git
cd game-engine-demo
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
```

### 运行演示

```bash
./bin/game_engine_demo
```

## 📁 项目结构

```
game-engine-demo/
├── assets/            # 示例资源（模型、纹理、配置文件）
├── include/           # 核心头文件
├── src/               # 引擎源码
│   ├── core/          # 基础组件（循环、内存等）
│   ├── render/        # 渲染后端
│   ├── physics/       # 物理系统
│   └── ...            # 其他模块
├── samples/           # 示例游戏场景
├── tests/             # 单元测试
├── CMakeLists.txt     # 构建系统
└── README.md
```

## ⚠️ 注意事项

本仓库**处于高度不稳定状态**，频繁进行破坏性修改，代码质量与接口随时可能发生巨大变化。  

- **请勿 Fork 或发起 Pull Request** —— 因为上游代码可能很快与您的分支无法合并，造成不必要的工作。  
- 如果您只想体验，可以直接克隆运行，但请做好遇到崩溃和不完整功能的心理准备。  
- 问题和讨论欢迎通过 [Issues](https://github.com/chenjiefeng2001/game-engine-demo/issues) 提交，但修复时间不定。

## 🤝 贡献

由于项目尚处于快速迭代期，暂时不接受代码贡献。  
如您有任何建议、报错或想法，欢迎在 Issues 中提出，帮助项目明确方向。  
未来项目稳定后，将制定详细的贡献指南并开放 PR。

## 📝 许可证

本项目采用 **MIT License** 开源（若实际使用其他协议请相应修改）。  
该项目仍处于试验阶段,不具有投入生产的可能,请慎重考虑使用


