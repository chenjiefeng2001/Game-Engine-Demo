/**
 * @file gl_capability_probe.cpp
 * @brief Standalone OpenGL capability probe for CI environment selection
 *
 * Answers one question by measurement, not by documentation: can this machine
 * create the OpenGL context the production render path requires?
 *
 * It deliberately depends on nothing but GLFW and the system GL loader, so it
 * can run on a CI runner even when the engine itself does not build there. That
 * matters: the engine build is currently blocked on an unrelated Linux
 * portability defect, and that defect must not be worked around just to obtain
 * a green GPU job.
 *
 * Two facts are recorded and deliberately not conflated:
 *
 *   1. what a default context (no version hint) reports, and
 *   2. whether an explicit core context can be created at each version.
 *
 * A default context advertising 4.6 does NOT prove that a 4.6 core context can
 * be created. The production render window asks for 4.6 core explicitly, so
 * only the second fact settles whether the environment is usable.
 *
 * Exit status is 0 whenever the probe ran to completion. Capability is reported,
 * not asserted: the caller decides what a given answer means.
 */

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace {

// Resolves the entry points we need by name so the probe does not need glad.
struct GL {
    const unsigned char* (*GetString)(unsigned int) = nullptr;
    void (*GetIntegerv)(unsigned int, int*) = nullptr;
};

const char* SafeGetString(GL& gl, unsigned int name, const char* label)
{
    if (!gl.GetString) {
        std::printf("[GLPROBE] %-28s <no glGetString>\n", label);
        return "<unavailable>";
    }
    const unsigned char* s = gl.GetString(name);
    const char* v = s ? reinterpret_cast<const char*>(s) : "<null>";
    std::printf("[GLPROBE] %-28s = %s\n", label, v);
    return v;
}

void LoadGL(GL& gl)
{
    gl.GetString = reinterpret_cast<const unsigned char* (*)(unsigned int)>(
        glfwGetProcAddress("glGetString"));
    gl.GetIntegerv = reinterpret_cast<void (*)(unsigned int, int*)>(
        glfwGetProcAddress("glGetIntegerv"));
}

// Returns true only if a core context of the requested version was created.
bool TryCreateCoreContext(int major, int minor)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* w = glfwCreateWindow(64, 64, "GL capability probe", nullptr, nullptr);
    if (!w) {
        return false;
    }
    glfwMakeContextCurrent(w);
    GL gl;
    LoadGL(gl);
    SafeGetString(gl, 0x1F02 /* GL_VERSION */, "requested context reports");
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(w);
    return true;
}

void ReportContext(GL& gl)
{
    SafeGetString(gl, 0x1F02 /* GL_VERSION */, "GL_VERSION");
    SafeGetString(gl, 0x1F00 /* GL_VENDOR */, "GL_VENDOR");
    SafeGetString(gl, 0x1F01 /* GL_RENDERER */, "GL_RENDERER");
    SafeGetString(gl, 0x8B8C /* GL_SHADING_LANGUAGE_VERSION */, "GL_SHADING_LANGUAGE_VERSION");

    if (gl.GetIntegerv) {
        int mask = 0;
        gl.GetIntegerv(0x9126 /* GL_CONTEXT_PROFILE_MASK */, &mask);
        if (mask != 0) {
            std::printf("[GLPROBE] %-28s = 0x%x (%s%s)\n", "GL_CONTEXT_PROFILE_MASK", mask,
                        (mask & 0x1) ? "core" : "", (mask & 0x2) ? " compatibility" : "");
        } else {
            std::printf("[GLPROBE] %-28s <not queryable>\n", "GL_CONTEXT_PROFILE_MASK");
        }
    }
}

}  // namespace

int main()
{
    std::printf("[GLPROBE] === OpenGL capability probe ===\n");

    if (!glfwInit()) {
        std::printf("[GLPROBE] glfwInit FAILED\n");
        return 2;
    }
    std::printf("[GLPROBE] glfwInit OK\n");

    // --- Fact 1: what a default context reports ------------------------
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* defWindow = glfwCreateWindow(64, 64, "GL default probe", nullptr, nullptr);
    if (!defWindow) {
        std::printf("[GLPROBE] default context: CREATE FAILED\n");
    } else {
        glfwMakeContextCurrent(defWindow);
        GL gl;
        LoadGL(gl);
        std::printf("[GLPROBE] --- default context report ---\n");
        ReportContext(gl);
        glfwMakeContextCurrent(nullptr);
        glfwDestroyWindow(defWindow);
    }

    // --- Fact 2: can explicit core contexts be created? ---------------
    // 4.6 is what the production render window requests, so that is the
    // version that decides whether this environment can host the product
    // path. 4.5 is the highest the engine actually calls into (DSA entry
    // points on the buffer path), so it is reported as the floor.
    std::printf("[GLPROBE] --- explicit core context creation ---\n");
    for (const auto& v : { std::pair<int, int>{ 4, 5 }, { 4, 6 } }) {
        if (TryCreateCoreContext(v.first, v.second)) {
            std::printf("[GLPROBE] %d.%d core context: CREATED\n", v.first, v.second);
        } else {
            std::printf("[GLPROBE] %d.%d core context: CREATE FAILED\n", v.first, v.second);
        }
    }

    std::fflush(stdout);
    glfwTerminate();
    std::printf("[GLPROBE] === probe complete ===\n");
    return 0;
}
