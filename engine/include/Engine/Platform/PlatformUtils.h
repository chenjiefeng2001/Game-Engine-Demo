#pragma once

namespace Engine {
    class Time {
    public:
        // 静态接口：获取自启动以来的秒数
        // 这样 Application 只知道这个函数，不知道后面是 GLFW 还是 Win32
        static float GetTime();
    };
}