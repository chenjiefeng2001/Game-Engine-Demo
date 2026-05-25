#pragma once

#include <string>
#include <memory>
#include "Engine/Types.h"
#include "Engine/Core/Resources/Resource.h"

namespace Engine {

	class Texture : public Resource
	{
	public:
		Texture(std::string path) : Resource(std::move(path), ResourceType::Texture) {}
		virtual ~Texture() = default;

		virtual void Bind(uint32 slot = 0) const = 0;
		virtual uint32 GetWidth() const = 0;
		virtual uint32 GetHeight() const = 0;
	};

}
