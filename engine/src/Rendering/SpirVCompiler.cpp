/**
 * @file SpirVCompiler.cpp
 * @brief SPIR-V 缂栬瘧绠￠亾 鈥?HLSL 缈昏瘧 + DXC 缂栬瘧 (SM 6.6 ready)
 *
 * SM 6.6 鐗规€э細
 *   - ResourceDescriptorHeap / SamplerDescriptorHeap 鈥?鍏ㄥ眬璧勬簮鍫嗙洿鎺ョ储寮?
 *   - shader_model = 66 鈥?鍚敤 SM 6.6 鐫€鑹插櫒妯″瀷
 *   - #define 瀹忔敞鍏?鈥?灏?register(bN) 鏄犲皠鍒板爢璁块棶
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
#include <atlbase.h>
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
    switch (stage.type) {
        case ShaderStageType::Vertex:   profileName = "vs_6_6"; break;
        case ShaderStageType::Fragment: profileName = "ps_6_6"; break;
        case ShaderStageType::Compute:  profileName = "cs_6_6"; break;
        default: return {};
    }

    try {
        spirv_cross::CompilerHLSL hlsl(spirv);

        // SM 6.6 閫夐」锛氬惎鐢?Bindless 璧勬簮鍫嗙储寮?
        spirv_cross::CompilerHLSL::Options options;
        options.shader_model = 66;
        // 涓嶅惎鐢ㄨ嚜鍔?use_resource_descriptor_heap锛屾敼鐢ㄦ敞鍏ュ畯瀹炵幇绮剧‘鎺у埗
        hlsl.set_hlsl_options(options);

        // 璁剧疆鐫€鑹插櫒鍏ュ彛鐐?
        hlsl.set_entry_point("main",
            stage.type == ShaderStageType::Vertex ? spv::ExecutionModelVertex :
            stage.type == ShaderStageType::Fragment ? spv::ExecutionModelFragment :
            spv::ExecutionModelGLCompute);

        // 缂栬瘧 HLSL
        std::string hlslSource = hlsl.compile();

        // 鈥?鍏抽敭姝ラ锛氭敞鍏?SM 6.6 Bindless 瀹?鈥?
        // 灏?register(bN/spaceN) 鏄犲皠鍒?ResourceDescriptorHeap/SamplerDescriptorHeap
        // 浣?SPIRV-Cross 鐢熸垚鐨?HLSL 浠ｇ爜閫氳繃 PushConstants 绱㈠紩璁块棶鍏ㄥ眬鍫?
        std::ostringstream inject;
        inject << "// SM 6.6 Bindless 鈥?鑷姩娉ㄥ叆\n";
        inject << "#define BINDLESS_TEXTURE(idx)  ResourceDescriptorHeap[idx]\n";
        inject << "#define BINDLESS_SAMPLER(idx)  SamplerDescriptorHeap[idx]\n";
        inject << "#define BINDLESS_UBO(idx)      ResourceDescriptorHeap[idx]\n";
        inject << "#define BINDLESS_SSBO(idx)     ResourceDescriptorHeap[idx]\n\n";
        
        hlslSource = inject.str() + hlslSource;

        s_Log.Info("SPIR-V 鈫?HLSL SM 6.6 translated ({}, {} chars)",
                   profileName.c_str(), hlslSource.size());
        return hlslSource;

    } catch (const std::exception& e) {
        s_Log.Error("SPIR-V 鈫?HLSL translation failed: {}", e.what());
        return {};
    }
}

#endif // ENGINE_HAS_SPIRV_CROSS

// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲
// CompileHLSLToDXIL 鈥?閫氳繃 DXC 缂栬瘧 HLSL 涓?DXIL (SM 6.6)
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲

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
    // SM 6.6 鐗规湁鐨勪紭鍖栭€夐」
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

    s_Log.Info("DXC SM 6.6: {} 鈫?{} ({} bytes)", profile.c_str(), entryPoint.c_str(), size);
    FreeLibrary(dxcModule);
    return resultData;
#else
    return {};
#endif
}

} // namespace Rendering
} // namespace Engine