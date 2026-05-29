# ==============================================================================
# 1. 定义各个沙盒可执行文件（各入各文件夹）
# ==============================================================================

# 将每个可执行文件放入独立子目录的辅助宏
macro(add_sandbox_executable targetName)
    add_executable(${targetName} ${ARGN})
    target_link_libraries(${targetName} PRIVATE EngineCore)
    target_precompile_headers(${targetName} REUSE_FROM EngineCore)
    add_dependencies(${targetName} SyncSandboxResources)
    set_target_properties(${targetName} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${targetName}"
    )
endmacro()

add_sandbox_executable(Sandbox src/main.cpp)
add_sandbox_executable(InputTest src/InputTest/main.cpp src/InputTest/InputTestApp.cpp)
add_sandbox_executable(SpriteBatchTest src/SpriteBatchTest/main.cpp src/SpriteBatchTest/SpriteBatchTest.cpp)
add_sandbox_executable(GameObjectTest src/GameObjectTest/main.cpp src/GameObjectTest/GameObjectTest.cpp)
add_sandbox_executable(TextureManagerTest src/TextureManagerTest/main.cpp src/TextureManagerTest/TextureManagerTest.cpp)
add_sandbox_executable(PhysicsTest src/PhysicsTest/main.cpp src/PhysicsTest/PhysicsTestApp.cpp)
add_sandbox_executable(AudioTest src/AudioTest/main.cpp src/AudioTest/AudioTestApp.cpp)
add_sandbox_executable(ImGuiDemo src/ImGuiDemo/main.cpp)
add_sandbox_executable(CollisionAudioTest src/CollisionAudioTest/main.cpp src/CollisionAudioTest/CollisionAudioTestApp.cpp)
add_sandbox_executable(ImGuiTest src/ImGuiTest/main.cpp)
add_sandbox_executable(AudioPhysicsSandbox src/AudioPhysicsSandbox/main.cpp src/AudioPhysicsSandbox/AudioPhysicsSandboxApp.cpp)
add_sandbox_executable(SerializationTest src/SerializationTest/main.cpp src/SerializationTest/SerializationTest.cpp)
add_sandbox_executable(MarioDemo src/MarioDemo/main.cpp src/MarioDemo/MarioDemoApp.cpp)
add_sandbox_executable(EditorDemo src/EditorDemo/main.cpp)
add_sandbox_executable(SystemTest src/SystemTest/main.cpp src/SystemTest/SystemTestApp.cpp)

# ==============================================================================
# 2. 资源同步 Target — 将 assets 和 OpenAL32.dll 拷贝到每个输出子目录
# ==============================================================================
set(SANDBOX_TARGETS
    Sandbox InputTest SpriteBatchTest GameObjectTest
    TextureManagerTest PhysicsTest AudioTest ImGuiDemo
    CollisionAudioTest ImGuiTest AudioPhysicsSandbox SerializationTest MarioDemo EditorDemo
    SystemTest
)

add_custom_target(SyncSandboxResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL>" "${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_NAME:OpenAL>"
)

foreach(target ${SANDBOX_TARGETS})
    add_custom_command(TARGET SyncSandboxResources POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
                    "${CMAKE_CURRENT_BINARY_DIR}/${target}/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL>"
                    "${CMAKE_CURRENT_BINARY_DIR}/${target}/$<TARGET_FILE_NAME:OpenAL>"
        COMMENT "  -> syncing resources to ${target}/"
    )
endforeach()