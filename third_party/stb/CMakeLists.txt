add_library(stb STATIC
    stb_image_impl.c
    stb_vorbis_impl.c
    dr_wav_impl.c
)
target_include_directories(stb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
