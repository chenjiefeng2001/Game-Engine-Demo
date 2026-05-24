#include "Engine/Core/RHI/RenderQueue.h"

namespace Engine {

    void RenderQueue::Push(const RenderCommand& cmd) {
        m_Commands.push_back(cmd);
    }

    void RenderQueue::Clear() {
        m_Commands.clear();
    }

    size_t RenderQueue::GetCount() const {
        return m_Commands.size();
    }

    const std::vector<RenderCommand>& RenderQueue::GetCommands() const {
        return m_Commands;
    }

} // namespace Engine
