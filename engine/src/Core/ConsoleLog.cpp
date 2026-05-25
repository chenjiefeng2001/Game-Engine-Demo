#include "Engine/ConsoleLog.h"
#include <iostream>
#include <algorithm>

namespace Engine {

    ConsoleLog& ConsoleLog::Instance()
    {
        static ConsoleLog instance;
        return instance;
    }

    void ConsoleLog::Log(LogLevel level, const std::string& message)
    {
        // 获取时间戳（秒，相对于程序启动）
        static auto startTime = std::clock();
        double timestamp = static_cast<double>(std::clock() - startTime) / CLOCKS_PER_SEC;

        // 写入环形缓冲区
        uint32 index;
        if (m_Count < kBufferSize)
        {
            // 缓冲区未满：追加到末尾
            index = m_Count;
            ++m_Count;
        }
        else
        {
            // 缓冲区已满：覆盖最旧条目
            index = m_StartIndex;
            m_StartIndex = (m_StartIndex + 1) % kBufferSize;
        }

        m_Buffer[index].level     = level;
        m_Buffer[index].timestamp = timestamp;
        m_Buffer[index].message   = message;

        // 同步输出到 stderr（方便 IDE 控制台调试）
        switch (level)
        {
            case LogLevel::Info:
                std::cout << "[INFO] " << message << std::endl;
                break;
            case LogLevel::Warn:
                std::cout << "[WARN] " << message << std::endl;
                break;
            case LogLevel::Error:
                std::cerr << "[ERROR] " << message << std::endl;
                break;
        }
    }

    void ConsoleLog::Clear()
    {
        m_StartIndex = 0;
        m_Count = 0;
    }

} // namespace Engine
