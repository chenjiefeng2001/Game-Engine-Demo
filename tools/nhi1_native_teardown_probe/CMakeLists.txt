# ============================================================
# NHI-1.4 — native-only teardown probe（独立 CMake 工程）
#
# 刻意**不**被主 CMakeLists 引入：
#   - 不进 CTest、不进 tests/、不改变 Evidence-Baseline 的 I2=240；
#   - 只链接主工程产出的 EditorBridge.lib，复用现有 C ABI。
#
# 用法：
#   cmake -S tools/nhi1_native_teardown_probe -B build/nhi1 -G "Visual Studio 17 2022" -A x64
#   cmake --build build/nhi1 --config Debug
#   cd <repo root> && build\nhi1\Debug\nhi1_native_probe.exe B-tick-managed
# ============================================================
cmake_minimum_required(VERSION 3.20)
project(nhi1_native_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 允许 -DNHI1_BRIDGE_DIR=<repo>/build/bridge/Debug 覆盖 DLL 来源
set(NHI1_BRIDGE_DIR "" CACHE PATH "Directory containing EditorBridge.lib/.dll")

# 向上两级到仓库根（tools/nhi1_native_teardown_probe -> tools -> repo）
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
if(NHI1_BRIDGE_DIR)
    set(BRIDGE_DIR "${NHI1_BRIDGE_DIR}")
else()
    set(BRIDGE_DIR "${REPO_ROOT}/build/bridge/Debug")
endif()

set(BRIDGE_INC "${REPO_ROOT}/bridge/include")
set(BRIDGE_LIB "${BRIDGE_DIR}/EditorBridge.lib")

if(NOT EXISTS "${BRIDGE_LIB}")
    message(FATAL_ERROR
        "EditorBridge.lib not found at ${BRIDGE_LIB}\n"
        "Build the main project first, or pass -DNHI1_BRIDGE_DIR=<path>")
endif()

add_executable(nhi1_native_probe nhi1_native_probe.cpp)
# capi.h 是 UTF-8 无 BOM 且含中文注释：MSVC 默认按 ANSI 解码会破坏解析
# （与主工程 CMakeLists.txt:39 的 /utf-8 保持一致）。
target_compile_options(nhi1_native_probe PRIVATE /utf-8)
target_include_directories(nhi1_native_probe PRIVATE "${BRIDGE_INC}")
target_link_libraries(nhi1_native_probe PRIVATE "${BRIDGE_LIB}")

# 让 probe 与被测 DLL 同目录（避免 PATH 依赖）
if(EXISTS "${BRIDGE_DIR}/EditorBridge.dll")
    add_custom_command(TARGET nhi1_native_probe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BRIDGE_DIR}/EditorBridge.dll" "$<TARGET_FILE_DIR:nhi1_native_probe>")
endif()
