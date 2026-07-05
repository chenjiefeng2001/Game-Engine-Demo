#pragma once

/**
 * @file ECS.h
 * @brief ECS 系统统一入口头文件
 *
 * 包含所有 ECS 核心模块：
 *   - ECS.fwd.h          — 前向声明与公共类型（64-bit EntityHandle, ComponentTypeID, ...）
 *   - SparseSet.h        — 稀疏集（O(1) 实体分配/释放）
 *   - ComponentRegistry.h — 组件元信息全局注册表
 *   - Chunk.h            — 16KB 固定大小内存块
 *   - Archetype.h        — 组件签名分类集合
 *   - EntityManager.h    — 核心管理器（实体生命周期 + 组件操作 + 查询）
 *   - EntityCommandBuffer.h — 命令缓冲（延迟结构性变更）
 *   - System.h           — System 基类
 *
 * v2.0 架构：
 *   64-bit Generational ID | 16KB Cache-aligned Chunks | Archetype-based ECS
 *   EntityCommandBuffer 确保遍历安全性 | ComponentMeta 处理非 POD 类型
 */

#include "Engine/Core/ECS/ECS.fwd.h"
#include "Engine/Core/ECS/SparseSet.h"
#include "Engine/Core/ECS/ComponentRegistry.h"
#include "Engine/Core/ECS/Chunk.h"
#include "Engine/Core/ECS/Archetype.h"
#include "Engine/Core/ECS/EntityManager.h"
#include "Engine/Core/ECS/EntityCommandBuffer.h"
#include "Engine/Core/ECS/System.h"