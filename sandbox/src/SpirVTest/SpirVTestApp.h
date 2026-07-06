#pragma once

#include <string>

namespace Engine {

class SpirVTestApp {
public:
    SpirVTestApp() = default;
    ~SpirVTestApp() = default;

    bool RunAllTests();

private:
    /** 测试 SPIRV-Cross 反射（使用预编译 .spv 文件） */
    bool TestSpirvCrossReflection();

    /** 测试 Shaderc 编译 + SPIRV-Cross 反射 */
    bool TestShaderCompileAndReflect();

    int m_Passed = 0;
    int m_Failed = 0;
    void ReportResult(const std::string& name, bool ok);
};

} // namespace Engine