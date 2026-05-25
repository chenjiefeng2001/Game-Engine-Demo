cmake_minimum_required(VERSION 3.20)
project(imgui LANGUAGES CXX)

# Dear ImGui 构建配置
# 源文件来自子模块 third_party/imgui/，构建文件放在独立目录 imgui_build/ 中
# 这样不污染子模块的工作副本

set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../imgui")

# 核心源文件
set(IMGUI_SOURCES
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
)

# GLFW + OpenGL3 后端
set(IMGUI_BACKEND_SOURCES
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC
    ${IMGUI_SOURCES}
    ${IMGUI_BACKEND_SOURCES}
)

target_include_directories(imgui
    PUBLIC
        "${IMGUI_SOURCE_DIR}"
        "${IMGUI_SOURCE_DIR}/backends"
)

# 需要 GLFW 的头文件
target_include_directories(imgui
    PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/glfw/include"
)

# 需要 GLAD 头文件（OpenGL 加载器）
target_include_directories(imgui
    PRIVATE
        "${CMAKE_SOURCE_DIR}/third_party/glad/include"
)

# 定义 IMGUI_IMPL_OPENGL_LOADER_GLAD 让 imgui_impl_opengl3 使用 GLAD
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD)

# 链接 opengl32 (Windows)
if(WIN32)
    target_link_libraries(imgui PRIVATE opengl32)
endif()
