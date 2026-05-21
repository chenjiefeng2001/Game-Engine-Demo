#include "Engine/Platform/PlatformUtils.h"
#include <GLFW/glfw3.h> // 只有在这里才允许包含 GLFW

namespace Engine {
    float Time::GetTime() {
        return (float)glfwGetTime();
    }
}