#pragma once

#include <stdint.h>

namespace Engine {

	class Shader
	{
	public:
		virtual ~Shader() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;
	};

} 
