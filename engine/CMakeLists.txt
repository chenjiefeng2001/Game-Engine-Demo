
file(GLOB_RECURSE ENGINE_SOURCES "src/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS "include/*.h")

add_library(EngineCore STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
    include/Engine/Core/Scene/Serializer.h
)

target_include_directories(EngineCore

    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/third_party/glm"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"

)
target_link_libraries(EngineCore PUBLIC glad2 glfw stb box2d OpenAL::OpenAL imgui)

# 确保第三方 include 路径可用
target_include_directories(EngineCore PUBLIC
    "${PROJECT_SOURCE_DIR}/third_party/box2d/include"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})