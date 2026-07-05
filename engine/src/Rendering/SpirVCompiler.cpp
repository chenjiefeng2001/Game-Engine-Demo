/**
 * @file SpirVCompiler.cpp
 * @brief SPIR-V 编译管道 — 编译缓存 + 反射接口 + HLSL 翻译
 *
 * 功能：
 *   1. FNV-1a 编译缓存键生成
 *   2. ShaderReflectionData 提取 (SPIRV-Cross 反射)
 *   3. TranslateSpirVToHLSL() — 将 SPIR-V 翻译为 HLSL (SM 6.0)
 *   4. CompileHLSLToDXIL() — 通过 DXC 编译 HLSL 为 DXIL 字节码
 */

#include "Engine/Rendering/ShaderReflection.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/RenderResources/ShaderStage.h"

#include <vector>
#include <string>
#include <cstring>

#ifdef ENGINE_HAS_SPIRV_CROSS
#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_cross_util.hpp"
#endif

#ifdef _WIN32
#include <windows.h>
// DXC headers — 从 Windows SDK 或 dxc 包中获取
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#endif

namespace {
    Engine::Logger s_Log("SpirV");
}

namespace Engine {
namespace Rendering {

#ifdef ENGINE_HAS_SPIRV_CROSS

// ════════════════════════════════════════════════════════════
// ExtractShaderReflection — 已被 ShaderReflection.cpp 中的
// ReflectSPIRV() 替代。此处保留占位。
// ════════════════════════════════════════════════════════════

// 实际反射实现在 ShaderReflection.cpp 的 ReflectSPIRV() 中

// ════════════════════════════════════════════════════════════
// TranslateSpirVToHLSL — 将 SPIR-V 翻译为 HLSL (SM 6.0)
//
// 关键映射约定：
//   Vulkan Set=N → HLSL RegisterSpace=N
//   Vulkan Binding=M → HLSL register(bM, spaceN) 或 register(tM, spaceN)
//
// 此约定与 D3D12 Root Signature 的 DescriptorTable 映射一致。
// ════════════════════════════════════════════════════════════

std::string TranslateSpirVToHLSL(const std::vector<uint32_t>& spirv, ShaderStage stage) {
    if (spirv.empty()) return {};

    std::string profileName;
    switch (stage) {
        case ShaderStage::Vertex:   profileName = "vs_6_0"; break;
        case ShaderStage::Fragment: profileName = "ps_6_0"; break;
        case ShaderStage::Compute:  profileName = "cs_6_0"; break;
        default: return {};
    }

    try {
        spirv_cross::CompilerHLSL hlsl(spirv);

        // HLSL 选项：SM 6.0，与 Vulkan Set/Binding 映射对齐
        spirv_cross::CompilerHLSL::Options options;
        options.shader_model = 60;
        hlsl.set_hlsl_options(options);

        // 设置着色器入口点
        hlsl.set_entry_point("main", stage == ShaderStage::Vertex ? spv::ExecutionModelVertex :
                                      stage == ShaderStage::Fragment ? spv::ExecutionModelFragment :
                                      spv::ExecutionModelGLCompute);

        // 编译
        std::string hlslSource = hlsl.compile();

        s_Log.Info("SPIR-V → HLSL translated ({}, {} chars)",
                   profileName.c_str(), hlslSource.size());

        return hlslSource;
    } catch (const std::exception& e) {
        s_Log.Error("SPIR-V → HLSL translation failed: {}", e.what());
        return {};
    }
}

#endif // ENGINE_HAS_SPIRV_CROSS

// ════════════════════════════════════════════════════════════
// CompileHLSLToDXIL — 通过 DXC 编译 HLSL 为 DXIL 字节码
//
// 使用动态加载的 dxcompiler.dll 和 IDxcCompiler3 接口。
// 返回编译后的着色器字节码（可供 ID3D12Device::CreateGraphicsPipelineState 使用）
// ════════════════════════════════════════════════════════════

std::vector<uint8_t> CompileHLSLToDXIL(const std::string& hlslSource,
                                        const std::string& entryPoint,
                                        const std::string& profile) {
    if (hlslSource.empty()) return {};

#ifdef _WIN32
    // 动态加载 dxcompiler.dll
    HMODULE dxcModule = LoadLibraryExA("dxcompiler.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dxcModule) {
        s_Log.Error("DXC: failed to load dxcompiler.dll (not found or missing dependencies)");
        return {};
    }

    // 获取 DxcCreateInstance 函数指针
    using DxcCreateInstanceFunc = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto pDxcCreateInstance = reinterpret_cast<DxcCreateInstanceFunc>(
        GetProcAddress(dxcModule, "DxcCreateInstance"));

    if (!pDxcCreateInstance) {
        s_Log.Error("DXC: DxcCreateInstance not found in dxcompiler.dll");
        FreeLibrary(dxcModule);
        return {};
    }

    // 创建 IDxcCompiler3
    CComPtr<IDxcCompiler3> compiler;
    HRESULT hr = pDxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    if (FAILED(hr) || !compiler) {
        s_Log.Error("DXC: failed to create IDxcCompiler3");
        FreeLibrary(dxcModule);
        return {};
    }

    // 创建 IDxcUtils
    CComPtr<IDxcUtils> utils;
    pDxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (!utils) {
        s_Log.Error("DXC: failed to create IDxcUtils");
        FreeLibrary(dxcModule);
        return {};
    }

    // 创建 Blob Encoding 对象
    CComPtr<IDxcBlobEncoding> sourceBlob;
    hr = utils->CreateBlob(hlslSource.c_str(), (UINT32)hlslSource.size(),
                           CP_UTF8, &sourceBlob);
    if (FAILED(hr)) {
        s_Log.Error("DXC: failed to create source blob");
        FreeLibrary(dxcModule);
        return {};
    }

    // 准备编译参数
    std::vector<LPCWSTR> args;
    std::wstring wEntry = std::wstring(entryPoint.begin(), entryPoint.end());
    std::wstring wProfile = std::wstring(profile.begin(), profile.end());
    std::wstring wFile = L"shader.hlsl";

    args.push_back(wFile.c_str());
    args.push_back(L"-E");
    args.push_back(wEntry.c_str());
    args.push_back(L"-T");
    args.push_back(wProfile.c_str());
    args.push_back(L"-Qstrip_debug");   // 去掉调试信息
    args.push_back(L"-Qstrip_reflect"); // 去掉反射数据

    // 编译
    CComPtr<IDxcResult> compileResult;
    DxcBuffer sourceBuffer = {};
    sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
    sourceBuffer.Size = sourceBlob->GetBufferSize();
    sourceBuffer.Encoding = 0;

    hr = compiler->Compile(&sourceBuffer, args.data(), (UINT32)args.size(),
                           nullptr, IID_PPV_ARGS(&compileResult));

    if (FAILED(hr) || !compileResult) {
        s_Log.Error("DXC: Compile failed");
        FreeLibrary(dxcModule);
        return {};
    }

    // 检查编译错误
    CComPtr<IDxcBlobUtf8> errors;
    hr = compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (SUCCEEDED(hr) && errors && errors->GetStringLength() > 0) {
        s_Log.Error("DXC compilation errors:\n{}", errors->GetStringPointer());
    }

    // 获取编译结果
    HRESULT compileStatus;
    compileResult->GetStatus(&compileStatus);
    if (FAILED(compileStatus)) {
        s_Log.Error("DXC: Shader compilation failed");
        FreeLibrary(dxcModule);
        return {};
    }

    // 提取 DXIL 字节码
    CComPtr<IDxcBlob> shaderBlob;
    hr = compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    if (FAILED(hr) || !shaderBlob) {
        s_Log.Error("DXC: Failed to get compiled shader blob");
        FreeLibrary(dxcModule);
        return {};
    }

    // 复制到 vector
    const uint8_t* data = static_cast<const uint8_t*>(shaderBlob->GetBufferPointer());
    size_t size = shaderBlob->GetBufferSize();
    std::vector<uint8_t> result(data, data + size);

    s_Log.Info("DXC: compiled {} → {} ({} bytes)", profile.c_str(),
               entryPoint.c_str(), size);

    FreeLibrary(dxcModule);
    return result;

#else
    s_Log.Warn("DXC: Windows-only, skipping HLSL compilation");
    return {};
#endif
}

} // namespace Rendering
} // namespace Engine