#include "Engine/ConsoleLog.h"
#include <algorithm>
#include <cstring>

namespace Engine {

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
        // 固定缓冲区拷贝（安全截断）
        strncpy_s(m_Buffer[index].message, sizeof(m_Buffer[index].message),
                  message.c_str(), _TRUNCATE);
        // 注意：不写 std::cout / std::cerr。终端输出由
        // spdlog 的 stdout_color_sink 统一管理，避免重复。
    }

    void ConsoleLog::Clear()
    {
        m_StartIndex = 0;
        m_Count = 0;
    }

} // namespace Engine