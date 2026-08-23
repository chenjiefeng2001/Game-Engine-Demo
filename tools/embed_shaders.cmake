# ═══════════════════════════════════════════════════════════════
# embed_shaders.cmake — Shader 单一真理源嵌入生成器（v3.0 Phase 1）
#
# 用法（由 engine/CMakeLists.txt 的 add_custom_command 自动调用）:
#   cmake -DSHADER_DIR=<assets/shaders> -DOUT_FILE=<生成的.g.h> -P tools/embed_shaders.cmake
#
# 行为：
#   1. 扫描 SHADER_DIR 下全部 *.comp.glsl（文件名主干 = PSO 注册名）
#   2. 生成 constexpr string_view 注册表头文件（含每文件 MD5 完整性注释）
#   3. 内容无变化时跳过写入（避免无谓的工程重编译）
#   4. fail-fast：目录为空 / 原始串分隔符冲突 / 写入失败 → 致命错误，
#      绝不静默回退旧源码（v3.0 文档风险 R5 约定）
# ═══════════════════════════════════════════════════════════════

if(NOT DEFINED SHADER_DIR OR NOT DEFINED OUT_FILE)
    message(FATAL_ERROR "embed_shaders: SHADER_DIR and OUT_FILE are required")
endif()

file(GLOB shader_files "${SHADER_DIR}/*.comp.glsl")
list(SORT shader_files)
if(shader_files STREQUAL "")
    message(FATAL_ERROR "embed_shaders: no *.comp.glsl found in ${SHADER_DIR}")
endif()

set(delimiter "GLSL_EMBED_7F3A")
set(body "")
set(list_count 0)

foreach(f ${shader_files})
    file(READ "${f}" src ENCODING UTF-8)
    string(MD5 hash "${src}")

    # 原始串分隔符冲突检测（fail-fast，不做转义回退）
    if(src MATCHES "\\)${delimiter}\"")
        message(FATAL_ERROR "embed_shaders: ${f} contains raw-string terminator )${delimiter}\"")
    endif()

    get_filename_component(stem "${f}" NAME_WE)   # gpu_physics_integrate.comp -> 需要再去掉 .comp
    # NAME_WE 只去掉最后一个扩展名，此处手动截断 ".comp"
    string(REGEX REPLACE "\\.comp$" "" entry_name "${stem}")

    # 显示用相对路径（保证生成文件跨机器字节一致，可安全提交）
    set(disp "${f}")
    string(REGEX REPLACE "^.*/assets/shaders/" "assets/shaders/" disp "${disp}")

    string(APPEND body
        "    // ${disp}  md5:${hash}\n"
        "    { \"${entry_name}\",\n"
        "      R\"${delimiter}(${src})${delimiter}\" },\n")
    math(EXPR list_count "${list_count} + 1")
endforeach()

set(header "// ═════════════════════════════════════════════════════════\n")
string(APPEND header
    "// GENERATED FILE — DO NOT EDIT\n"
    "// Source of truth : assets/shaders/*.comp.glsl (${list_count} files)\n"
    "// Regenerator     : tools/embed_shaders.cmake (invoked by engine/CMakeLists.txt)\n"
    "// Contract        : docs/GPU-Physics-v3.0-Simulation-Domain-Architecture.md · Phase 1\n"
    "// ═════════════════════════════════════════════════════════\n\n"
    "#pragma once\n"
    "#include <string_view>\n\n"
    "namespace Engine {\n"
    "namespace RHI {\n"
    "namespace ShaderSources {\n\n"
    "struct Entry {\n"
    "    std::string_view name;    ///< PSO 注册名\n"
    "    std::string_view source;  ///< 完整 GLSL 源码（含 #version）\n"
    "};\n\n"
    "inline constexpr Entry kCompute[] = {\n"
    "${body}"
    "};\n\n"
    "/** 按 PSO 注册名查找源码；未命中返回 nullptr */\n"
    "inline const Entry* Find(std::string_view name) {\n"
    "    for (const Entry& e : kCompute)\n"
    "        if (e.name == name) return &e;\n"
    "    return nullptr;\n"
    "}\n\n"
    "} // namespace ShaderSources\n"
    "} // namespace RHI\n"
    "} // namespace Engine\n")

# 幂等写：内容不变则跳过，防止触发全量重编译
set(need_write 1)
if(EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" existing)
    if(existing STREQUAL header)
        set(need_write 0)
    endif()
endif()

if(need_write)
    file(WRITE "${OUT_FILE}" "${header}")
    message(STATUS "embed_shaders: wrote ${OUT_FILE} (${list_count} shaders)")
endif()
