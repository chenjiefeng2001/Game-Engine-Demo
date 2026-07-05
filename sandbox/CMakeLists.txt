# ==============================================================================
# 1. 定义各个沙盒可执行文件（各入各文件夹）
# ==============================================================================

# 将每个可执行文件放入独立子目录的辅助宏
# 辅助宏：为沙盒可执行文件添加 ASan 编译选项
macro(add_sandbox_asan targetName)
    if(MSVC AND ENABLE_ASAN)
        target_compile_options(${targetName} PRIVATE
            "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/fsanitize=address>"
        )
    endif()
endmacro()

macro(add_sandbox_executable targetName)
    add_executable(${targetName} ${ARGN})
    target_link_libraries(${targetName} PRIVATE EngineCore)
    target_precompile_headers(${targetName} REUSE_FROM EngineCore)
    add_dependencies(${targetName} SyncSandboxResources)
    set_target_properties(${targetName} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${targetName}"
    )
    add_sandbox_asan(${targetName})
endmacro()

add_sandbox_executable(Sandbox src/main.cpp src/EditorDemo/EditorDemoTest.cpp)
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
add_sandbox_executable(EditorDemo src/EditorDemo/main.cpp src/EditorDemo/EditorDemoTest.cpp)
add_sandbox_executable(SystemTest src/SystemTest/main.cpp src/SystemTest/SystemTestApp.cpp)
add_sandbox_executable(_3DTest src/_3DTest/main.cpp src/_3DTest/_3DTestApp.cpp)
add_sandbox_executable(CrashTest src/CrashTest/main.cpp)
add_sandbox_executable(ComplexSceneTest src/ComplexSceneTest/main.cpp src/ComplexSceneTest/ComplexSceneTestApp.cpp)
add_sandbox_executable(AnimationTest src/AnimationTest/main.cpp src/AnimationTest/AnimationTestApp.cpp)
add_sandbox_executable(AnimationDemo src/AnimationDemo/main.cpp src/AnimationDemo/AnimationDemoApp.cpp)
add_sandbox_executable(BarePhysicsTest src/BarePhysicsTest/main.cpp src/BarePhysicsTest/BarePhysicsTestApp.cpp)
add_sandbox_executable(AudioSystemTest src/AudioSystemTest/main.cpp)

# ==============================================================================
# 2. 资源同步 Target — 将 assets 和 OpenAL32.dll 拷贝到每个输出子目录
# ==============================================================================
set(SANDBOX_TARGETS
    Sandbox InputTest SpriteBatchTest GameObjectTest
    TextureManagerTest PhysicsTest AudioTest ImGuiDemo
    CollisionAudioTest ImGuiTest AudioPhysicsSandbox SerializationTest MarioDemo EditorDemo
    SystemTest _3DTest ComplexSceneTest AnimationTest AnimationDemo AudioSystemTest
)

add_custom_target(SyncSandboxResources ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
)

# 为每个沙盒目标复制资源 + DLL（到生成的可执行文件输出目录）
foreach(target ${SANDBOX_TARGETS})
    # 使用 $<TARGET_FILE_DIR:${target}> 获取当前配置下的实际输出目录
    # 例如：build/sandbox/EditorDemo/Debug/  （VS generator 会自动追加 $(Configuration)）
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:${target}>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL>" "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_NAME:OpenAL>"
        COMMENT "  -> syncing resources + OpenAL32.dll to $<TARGET_FILE_DIR:${target}>"
    )
endforeach()
# 移除旧的 ALL 目标同步命令（已被上面的 per-target POST_BUILD 替代）
# （保留 SyncSandboxResources 仅用于 assets 根目录拷贝，供其他测试使用）
