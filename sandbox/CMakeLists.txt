# ── 原有的 Sandbox ──
add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE EngineCore)
add_custom_command(
    TARGET Sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:Sandbox>/assets"
    COMMENT "Syncing assets to build directory..."
)

# ── 新增：InputTest（单独的可执行文件）──
add_executable(InputTest
    src/InputTest/main.cpp
    src/InputTest/InputTestApp.cpp
)
target_link_libraries(InputTest PRIVATE EngineCore)
add_custom_command(
    TARGET InputTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:InputTest>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:InputTest>/assets"
    COMMENT "Syncing assets to build directory..."
)
add_executable(SpriteBatchTest
    src/SpriteBatchTest/main.cpp
    src/SpriteBatchTest/SpriteBatchTest.cpp
)
target_link_libraries(SpriteBatchTest PRIVATE EngineCore)
add_custom_command(
    TARGET SpriteBatchTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:SpriteBatchTest>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:SpriteBatchTest>/assets"
    COMMENT "Syncing assets to build directory..."
)