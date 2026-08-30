#pragma once

#include "Core/IDRpch.hpp"
#include "Resource/Sampler/SamplerCreateInfo.hpp"

#ifdef IDRENDERER_USE_OPENGL

#include <glad/glad.h>

namespace ID
{
    class Sampler
    {
    public:
        Sampler() = delete;
        Sampler(const SamplerCreateInfo& create_info);
        ~Sampler() { destroy(); }

        Sampler(const Sampler&) = delete;
        Sampler& operator=(const Sampler&) = delete;

        Sampler(Sampler&&) noexcept;
        Sampler& operator=(Sampler&&) noexcept;

        void destroy();

    public:
        GLuint get_sampler() const { return m_sampler; }

    private:
        GLuint m_sampler = 0;
    };
} // namespace ID

#endif // IDRENDERER_USE_OPENGL
