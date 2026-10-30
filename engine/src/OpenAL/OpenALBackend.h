#pragma once

#include <memory>

namespace Engine {

    class IOpenALBackend;

    /// Production backend factory; defined in OpenALBackend.cpp.
    std::shared_ptr<IOpenALBackend> CreateOpenALBackend();

} // namespace Engine
