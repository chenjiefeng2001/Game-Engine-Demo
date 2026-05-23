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