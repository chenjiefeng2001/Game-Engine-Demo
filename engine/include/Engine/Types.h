#pragma once

#include <cstdint>
#include <cstddef>

// ============================================================
// 平台检测宏
// ============================================================
#if defined(_WIN32) || defined(_WIN64)
    #define ENGINE_PLATFORM_WINDOWS
#elif defined(__APPLE__) || defined(__MACH__)
    #define ENGINE_PLATFORM_MACOS
#elif defined(__linux__)
    #define ENGINE_PLATFORM_LINUX
#elif defined(__ANDROID__)
    #define ENGINE_PLATFORM_ANDROID
#endif

// ============================================================
// 编译器检测宏
// ============================================================
#if defined(__clang__)
    #define ENGINE_COMPILER_CLANG
#elif defined(__GNUC__) || defined(__GNUG__)
    #define ENGINE_COMPILER_GCC
#elif defined(_MSC_VER)
    #define ENGINE_COMPILER_MSVC
#endif

// ============================================================
// 基础数据类型别名（跨平台，与具体平台解耦）
// ============================================================

// ---- 有符号整数 ----
using  int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;

// ---- 无符号整数 ----
using  uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

// ---- 浮点类型 ----
using float32 = float;
using float64 = double;

// ---- 大小类型 ----
using SizeT = std::size_t;

// ---- 常用别名 ----
using uint = unsigned int;

// ------------------------------------------------------------
// 编译期断言：确保类型大小符合预期
// ------------------------------------------------------------
static_assert(sizeof(int8)   == 1, "int8 must be 1 byte");
static_assert(sizeof(int16)  == 2, "int16 must be 2 bytes");
static_assert(sizeof(int32)  == 4, "int32 must be 4 bytes");
static_assert(sizeof(int64)  == 8, "int64 must be 8 bytes");
static_assert(sizeof(uint8)  == 1, "uint8 must be 1 byte");
static_assert(sizeof(uint16) == 2, "uint16 must be 2 bytes");
static_assert(sizeof(uint32) == 4, "uint32 must be 4 bytes");
static_assert(sizeof(uint64) == 8, "uint64 must be 8 bytes");
static_assert(sizeof(float32) == 4, "float32 must be 4 bytes");
static_assert(sizeof(float64) == 8, "float64 must be 8 bytes");
