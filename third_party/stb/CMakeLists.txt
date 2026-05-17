add_library(stb STATIC stb_image_impl.c)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
