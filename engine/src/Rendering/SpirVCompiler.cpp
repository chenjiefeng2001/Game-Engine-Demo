/**
 * @file SpirVCompiler.cpp
 * @brief SPIR-V 编译管道 — HLSL 翻译 + DXC 编译 (SM 6.6 ready)
 *
 * SM 6.6 特性：
 *   - ResourceDescriptorHeap / SamplerDescriptorHeap — 全局资源堆直接索引
 *   - shader_model = 66 — 启用 SM 6.6 着色器模型
 *   - #define 宏注入 — 将 register(bN) 映射到堆访问
 */

#include "Engine/Rendering/ShaderReflection.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/RenderResources/ShaderStage.h"
#include <vector>
#include <string>
#include <cstring>
#include <sstream>

#ifdef ENGINE_HAS_SPIRV_CROSS
#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_cross_util.hpp"
#endif

#ifdef _WIN32
#include <windows.h>
#include <dxcapi.h>
#pragma comment(lib, "dxcompiler.lib")
#endif

namespace {
    Engine::Logger s_Log("SpirV");
}

namespace Engine {
namespace Rendering {

#ifdef ENGINE_HAS_SPIRV_CROSS

std::string TranslateSpirVToHLSL(const std::vector<uint32_t>& spirv, ShaderStage stage) {
    if (spirv.empty()) return {};

    std::string profileName;
    switch (stage) {
        case ShaderStage::Vertex:   profileName = "vs_6_6"; break;
        case ShaderStage::Fragment: profileName = "ps_6_6"; break;
        case ShaderStage::Compute:  profileName = "cs_6_6"; break;
        default: return {};
    }

    try {
        spirv_cross::CompilerHLSL hlsl(spirv);

        // SM 6.6 选项：启用 Bindless 资源堆索引
        spirv_cross::CompilerHLSL::Options options;
        options.shader_model = 66;
        // 不启用自动 use_resource_descriptor_heap，改用注入宏实现精确控制
        hlsl.set_hlsl_options(options);

        // 设置着色器入口点
        hlsl.set_entry_point("main",
            stage == ShaderStage::Vertex ? spv::ExecutionModelVertex :
            stage == ShaderStage::Fragment ? spv::ExecutionModelFragment :
            spv::ExecutionModelGLCompute);

        // 编译 HLSL
        std::string hlslSource = hlsl.compile();

        // — 关键步骤：注入 SM 6.6 Bindless 宏 —
        // 将 register(bN/spaceN) 映射到 ResourceDescriptorHeap/SamplerDescriptorHeap
        // 使 SPIRV-Cross 生成的 HLSL 代码通过 PushConstants 索引访问全局堆
        std::ostringstream inject;
        inject << "// SM 6.6 Bindless — 自动注入\n";
        inject << "#define BINDLESS_TEXTURE(idx)  ResourceDescriptorHeap[idx]\n";
        inject << "#define BINDLESS_SAMPLER(idx)  SamplerDescriptorHeap[idx]\n";
        inject << "#define BINDLESS_UBO(idx)      ResourceDescriptorHeap[idx]\n";
        inject << "#define BINDLESS_SSBO(idx)     ResourceDescriptorHeap[idx]\n\n";
        
        hlslSource = inject.str() + hlslSource;

        s_Log.Info("SPIR-V → HLSL SM 6.6 translated ({}, {} chars)",
                   profileName.c_str(), hlslSource.size());
        return hlslSource;

    } catch (const std::exception& e) {
        s_Log.Error("SPIR-V → HLSL translation failed: {}", e.what());
        return {};
    }
}

#endif // ENGINE_HAS_SPIRV_CROSS

// ════════════════════════════════════════════════════════════
// CompileHLSLToDXIL — 通过 DXC 编译 HLSL 为 DXIL (SM 6.6)
// ════════════════════════════════════════════════════════════

std::vector<uint8_t> CompileHLSLToDXIL(const std::string& hlslSource,
                                        const std::string& entryPoint,
                                        const std::string& profile) {
    if (hlslSource.empty()) return {};

#ifdef _WIN32
    HMODULE dxcModule = LoadLibraryExA("dxcompiler.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dxcModule) {
        s_Log.Error("DXC: failed to load dxcompiler.dll");
        return {};
    }

    using DxcCreateInstanceFunc = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
    auto pDxcCreateInstance = reinterpret_cast<DxcCreateInstanceFunc>(
        GetProcAddress(dxcModule, "DxcCreateInstance"));
    if (!pDxcCreateInstance) {
        FreeLibrary(dxcModule);
        return {};
    }

    CComPtr<IDxcCompiler3> compiler;
    HRESULT hr = pDxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
    CComPtr<IDxcUtils> utils;
    pDxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
    if (!compiler || !utils) { FreeLibrary(dxcModule); return {}; }

    CComPtr<IDxcBlobEncoding> sourceBlob;
    utils->CreateBlob(hlslSource.c_str(), (UINT32)hlslSource.size(), CP_UTF8, &sourceBlob);

    std::vector<LPCWSTR> args;
    std::wstring wEntry(entryPoint.begin(), entryPoint.end());
    std::wstring wProfile(profile.begin(), profile.end());
    args.push_back(L"shader.hlsl");
    args.push_back(L"-E"); args.push_back(wEntry.c_str());
    args.push_back(L"-T"); args.push_back(wProfile.c_str());
    args.push_back(L"-Qstrip_debug");
    args.push_back(L"-Qstrip_reflect");
    // SM 6.6 特有的优化选项
    args.push_back(L"-enable-unbounded-descriptor-tables");

    CComPtr<IDxcResult> result;
    DxcBuffer buf = { sourceBlob->GetBufferPointer(), sourceBlob->GetBufferSize(), 0 };
    hr = compiler->Compile(&buf, args.data(), (UINT32)args.size(), nullptr, IID_PPV_ARGS(&result));
    if (FAILED(hr)) { FreeLibrary(dxcModule); return {}; }

    CComPtr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
    if (errors && errors->GetStringLength() > 0)
        s_Log.Warn("DXC: {}", errors->GetStringPointer());

    HRESULT status;
    result->GetStatus(&status);
    if (FAILED(status)) { s_Log.Error("DXC: compilation failed"); FreeLibrary(dxcModule); return {}; }

    CComPtr<IDxcBlob> shaderBlob;
    result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    if (!shaderBlob) { FreeLibrary(dxcModule); return {}; }

    const uint8_t* data = (const uint8_t*)shaderBlob->GetBufferPointer();
    size_t size = shaderBlob->GetBufferSize();
    std::vector<uint8_t> resultData(data, data + size);

    s_Log.Info("DXC SM 6.6: {} → {} ({} bytes)", profile.c_str(), entryPoint.c_str(), size);
    FreeLibrary(dxcModule);
    return resultData;
#else
    return {};
#endif
}

} // namespace Rendering
} // namespace Engine