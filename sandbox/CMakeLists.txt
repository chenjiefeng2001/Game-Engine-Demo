# ���� ԭ�е� Sandbox ����
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

# ���� ������InputTest�������Ŀ�ִ���ļ�������
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

# GameObjectTest — 测试 TransformComponent / SpriteComponent / GameObject 基类
add_executable(GameObjectTest
    src/GameObjectTest/main.cpp
    src/GameObjectTest/GameObjectTest.cpp
)
target_link_libraries(GameObjectTest PRIVATE EngineCore)
add_custom_command(
    TARGET GameObjectTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:GameObjectTest>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:GameObjectTest>/assets"
    COMMENT "Syncing assets to build directory..."
)

# TextureManagerTest — 验证纹理缓存功能
add_executable(TextureManagerTest
    src/TextureManagerTest/main.cpp
    src/TextureManagerTest/TextureManagerTest.cpp
)
target_link_libraries(TextureManagerTest PRIVATE EngineCore)
add_custom_command(
    TARGET TextureManagerTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:TextureManagerTest>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:TextureManagerTest>/assets"
    COMMENT "Syncing assets to build directory..."
)

# PhysicsTest — 测试 Box2D 物理集成（解耦后的物理系统）
add_executable(PhysicsTest
    src/PhysicsTest/main.cpp
    src/PhysicsTest/PhysicsTestApp.cpp
)
target_link_libraries(PhysicsTest PRIVATE EngineCore)
add_custom_command(
    TARGET PhysicsTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:PhysicsTest>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:PhysicsTest>/assets"
    COMMENT "Syncing assets to build directory..."
)

# AudioTest — 测试 OpenAL Soft 音频集成
add_executable(AudioTest
    src/AudioTest/main.cpp
    src/AudioTest/AudioTestApp.cpp
)
target_link_libraries(AudioTest PRIVATE EngineCore)

# 编译后自动复制 OpenAL32.dll 到可执行文件目录
add_custom_command(TARGET AudioTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE_DIR:OpenAL>/$<TARGET_FILE_NAME:OpenAL>"
        "$<TARGET_FILE_DIR:AudioTest>/$<TARGET_FILE_NAME:OpenAL>"
    COMMENT "Copying OpenAL32.dll..."
)