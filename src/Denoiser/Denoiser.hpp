/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2021, Christoph Neuhauser
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CLOUDRENDERING_DENOISER_HPP
#define CLOUDRENDERING_DENOISER_HPP

#include <string>

#include <Graphics/Vulkan/Image/Image.hpp>

namespace sgl {
class PropertyEditor;
}

enum class DenoiserType {
    NONE,
    EAW,
#ifdef SUPPORT_OPTIX
    OPTIX
#endif
};
const char* const DENOISER_NAMES[] = {
        "None",
        "Edge-Avoiding À-Trous Wavelet Transform",
#ifdef SUPPORT_OPTIX
        "OptiX Denoiser"
#endif
};

class Denoiser {
public:
    virtual ~Denoiser() = default;
    virtual DenoiserType getDenoiserType() = 0;
    [[nodiscard]] virtual const char* getDenoiserName() const = 0;
    [[nodiscard]] virtual bool getIsEnabled() const { return true; }
    virtual void setOutputImage(sgl::vk::ImageViewPtr& outputImage)=0;
    virtual void setFeatureMap(const std::string& featureMapName, const sgl::vk::TexturePtr& featureTexture) = 0;
    virtual void denoise() = 0;
    virtual void recreateSwapchain(uint32_t width, uint32_t height) {}

    /// Renders the GUI. Returns whether re-rendering has become necessary due to the user's actions.
    virtual bool renderGuiPropertyEditorNodes(sgl::PropertyEditor& propertyEditor) { return false; }
};

std::shared_ptr<Denoiser> createDenoiserObject(DenoiserType denoiserType, sgl::vk::Renderer* renderer);

#endif //CLOUDRENDERING_DENOISER_HPP
