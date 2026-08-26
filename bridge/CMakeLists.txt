# EditorBridge —— Avalonia ↔ C++ 唯一边界（AV-002 / AV-005）
# 防腐层：链接静态 EngineCore，导出纯 C ABI；禁止 include imgui/面板头。
cmake_minimum_required(VERSION 3.20)
project(EditorBridge LANGUAGES CXX)

add_library(EditorBridge SHARED
    src/capi.cpp
    src/EditorSession.cpp
    src/EditorSession.h
)

target_include_directories(EditorBridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(EditorBridge PRIVATE EDITOR_BRIDGE_EXPORTS)
target_link_libraries(EditorBridge PRIVATE EngineCore)
