#pragma once

/**
 * @file PCH.h
 * @brief 引擎预编译头文件 — 将高频使用的标准库与引擎核心类型预编译
 *
 * 被 target_precompile_headers() 引用，编译器自动注入。
 * 源文件仍需显式 #include 所需头文件（保持依赖清晰），
 * 但编译器会跳过已预编译的部分，大幅减少重复解析开销。
 */

#include <memory>         // shared_ptr, unique_ptr
#include <string>         // std::string
#include <vector>         // std::vector
#include <functional>     // std::function
#include <unordered_map>  // std::unordered_map
#include <utility>        // std::move, std::forward
#include <algorithm>      // std::sort, std::find
#include <iostream>       // std::cout, std::cerr
#include <fstream>        // std::ifstream, std::ofstream
#include <sstream>        // std::stringstream
#include <cstring>        // memcpy, memset
#include <cmath>          // std::sqrt, std::sin, etc.
#include <cstdint>        // int32_t, uint32_t, etc.
#include <cstddef>        // size_t, nullptr_t
#include <cassert>        // assert
#include <limits>         // std::numeric_limits
#include <array>          // std::array
#include <chrono>         // std::chrono
#include <thread>         // std::thread
#include <mutex>          // std::mutex
#include <atomic>         // std::atomic
#include <type_traits>    // std::is_same, etc.
#include <initializer_list>

