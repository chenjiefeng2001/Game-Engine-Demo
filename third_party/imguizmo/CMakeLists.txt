add_library(imguizmo STATIC
    src/ImGuizmo.cpp
    src/GraphEditor.cpp
    src/ImCurveEdit.cpp
    src/ImGradient.cpp
    src/ImSequencer.cpp
    src/ImVectorEditor.cpp
)

target_include_directories(imguizmo
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(imguizmo PUBLIC imgui)
