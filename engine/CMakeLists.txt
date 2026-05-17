
file(GLOB_RECURSE ENGINE_SOURCES
    "src/Core/*.cpp"
    "src/OpenGL/*.cpp"
    "src/Platform/*.cpp"
)

file(GLOB_RECURSE ENGINE_HEADERS
    "include/Engine/Core/*.h"
    "include/Engine/OpenGL/*.h"
    "include/Engine/Platform/*.h"
    "include/Engine/*.h"
)

add_library(EngineCore STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

target_include_directories(EngineCore
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(EngineCore PUBLIC glad2 glfw stb)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${ENGINE_SOURCES} ${ENGINE_HEADERS})