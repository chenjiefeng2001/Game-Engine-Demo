/**
 * @file VMAImpl.cpp
 * @brief VMA (Vulkan Memory Allocator) 实现 — 唯一实例化点
 *
 * 必须恰好在一个 .cpp 文件中 #define VMA_IMPLEMENTATION
 * 然后 #include <vk_mem_alloc.h>，以生成 VMA 的函数实现体。
 */

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>