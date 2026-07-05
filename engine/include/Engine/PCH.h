#pragma once

/**
 * @file PCH.h
 * @brief ����Ԥ����ͷ�ļ� �� ����Ƶʹ�õı�׼���������������Ԥ����
 *
 * �� target_precompile_headers() ���ã��������Զ�ע�롣
 * Դ�ļ�������ʽ #include ����ͷ�ļ�������������������
 * ����������������Ԥ����Ĳ��֣���������ظ�����������
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

// Profiler（零开销宏，所有源文件可用 PROFILE_ZONE()）
#include "Engine/Profiler.h"

