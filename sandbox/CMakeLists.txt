# ==============================================================================
# 1. 统一的资源同步 Target (仅执行一次)
# ==============================================================================
# 专门负责把 assets 文件夹和 OpenAL32.dll 拷贝到当前输出目录
add_custom_target(SyncSandboxResources ALL
    # 删除旧的 assets 并拷贝新的 assets
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets"
    
    # 拷贝 OpenAL32.dll (使用 copy_if_different 避免重复覆盖)
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:OpenAL>" "${CMAKE_CURRENT_BINARY_DIR}/$<TARGET_FILE_NAME:OpenAL>"
    
    COMMENT "Syncing assets and OpenAL32.dll to sandbox directory (Single thread safe)..."
)

# ==============================================================================
# 2. 定义各个测试可执行文件
# ==============================================================================

# Sandbox
add_executable(Sandbox src/main.cpp)
target_link_libraries(Sandbox PRIVATE EngineCore)
add_dependencies(Sandbox SyncSandboxResources) # 添加依赖，确保编译前资源已准备好

# InputTest
add_executable(InputTest src/InputTest/main.cpp src/InputTest/InputTestApp.cpp)
target_link_libraries(InputTest PRIVATE EngineCore)
add_dependencies(InputTest SyncSandboxResources)

# SpriteBatchTest
add_executable(SpriteBatchTest src/SpriteBatchTest/main.cpp src/SpriteBatchTest/SpriteBatchTest.cpp)
target_link_libraries(SpriteBatchTest PRIVATE EngineCore)
add_dependencies(SpriteBatchTest SyncSandboxResources)

# GameObjectTest
add_executable(GameObjectTest src/GameObjectTest/main.cpp src/GameObjectTest/GameObjectTest.cpp)
target_link_libraries(GameObjectTest PRIVATE EngineCore)
add_dependencies(GameObjectTest SyncSandboxResources)

# TextureManagerTest
add_executable(TextureManagerTest src/TextureManagerTest/main.cpp src/TextureManagerTest/TextureManagerTest.cpp)
target_link_libraries(TextureManagerTest PRIVATE EngineCore)
add_dependencies(TextureManagerTest SyncSandboxResources)

# PhysicsTest
add_executable(PhysicsTest src/PhysicsTest/main.cpp src/PhysicsTest/PhysicsTestApp.cpp)
target_link_libraries(PhysicsTest PRIVATE EngineCore)
add_dependencies(PhysicsTest SyncSandboxResources)

# AudioTest
add_executable(AudioTest src/AudioTest/main.cpp src/AudioTest/AudioTestApp.cpp)
target_link_libraries(AudioTest PRIVATE EngineCore)
add_dependencies(AudioTest SyncSandboxResources)

# ImGuiDemo
add_executable(ImGuiDemo src/ImGuiDemo/main.cpp)
target_link_libraries(ImGuiDemo PRIVATE EngineCore)
add_dependencies(ImGuiDemo SyncSandboxResources)

# CollisionAudioTest
add_executable(CollisionAudioTest src/CollisionAudioTest/main.cpp src/CollisionAudioTest/CollisionAudioTestApp.cpp)
target_link_libraries(CollisionAudioTest PRIVATE EngineCore)
add_dependencies(CollisionAudioTest SyncSandboxResources)

# ImGuiTest
add_executable(ImGuiTest src/ImGuiTest/main.cpp)
target_link_libraries(ImGuiTest PRIVATE EngineCore)
add_dependencies(ImGuiTest SyncSandboxResources)

# AudioPhysicsSandbox
add_executable(AudioPhysicsSandbox src/AudioPhysicsSandbox/main.cpp src/AudioPhysicsSandbox/AudioPhysicsSandboxApp.cpp)
target_link_libraries(AudioPhysicsSandbox PRIVATE EngineCore)
add_dependencies(AudioPhysicsSandbox SyncSandboxResources)