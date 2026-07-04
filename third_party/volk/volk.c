/**
 * @file volk.c
 * @brief Volk 元加载器实现 — 单文件模式
 *
 * 定义 VOLK_IMPLEMENTATION 以生成函数体。
 * 由 third_party/volk/volk.h 和 CMake 集成。
 */

// Volk 元加载器要求在 .c 中定义 VOLK_IMPLEMENTATION
#define VOLK_IMPLEMENTATION
#include "volk.h"