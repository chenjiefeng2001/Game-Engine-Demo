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

