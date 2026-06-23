add_library(imguizmo STATIC
    src/ImGuizmo.cpp
    src/ImGuizmo.h
)

target_include_directories(imguizmo
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(imguizmo PUBLIC imgui)