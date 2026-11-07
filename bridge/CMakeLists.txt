# EditorBridge —— Avalonia ↔ C++ 唯一边界（AV-002 / AV-005）
# 防腐层：链接静态 EngineCore，导出纯 C ABI；禁止 include imgui/面板头。
cmake_minimum_required(VERSION 3.20)
project(EditorBridge LANGUAGES CXX)

add_library(EditorBridge SHARED
    src/capi.cpp
    src/EditorSession.cpp
    src/EditorSession.h
    src/EngineHost.cpp
    src/EngineHost.h
)

target_include_directories(EditorBridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# P6-1B B3: AssetImport headers live under engine/src (bridge-only private surface)
target_include_directories(EditorBridge PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_compile_definitions(EditorBridge PRIVATE EDITOR_BRIDGE_EXPORTS)
find_package(Threads REQUIRED)
target_link_libraries(EditorBridge PRIVATE EngineCore Threads::Threads)
