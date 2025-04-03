/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2020, Christoph Neuhauser
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

#ifndef LINEVIS_MLABRENDERER_HPP
#define LINEVIS_MLABRENDERER_HPP

#include <Graphics/Vulkan/Utils/Timer.hpp>

#include "SyncMode.hpp"
#include "Renderers/ResolvePass.hpp"
#include "Renderers/LineRenderer.hpp"

/**
 * Renders all lines with transparency values determined by the transfer function set by the user.
 * For this, the order-independent transparency (OIT) technique Multi-Layer Alpha Blending (MLAB) is used.
 * For more details see: Marco Salvi and Karthik Vaidyanathan. 2014. Multi-layer Alpha Blending. In Proceedings of the
 * 18th Meeting of the ACM SIGGRAPH Symposium on Interactive 3D Graphics and Games (San Francisco, California)
 * (I3D ’14). ACM, New York, NY, USA, 151–158. https://doi.org/10.1145/2556700.2556705
 *
 * For a comparison of different OIT algorithms see:
 * M. Kern, C. Neuhauser, T. Maack, M. Han, W. Usher and R. Westermann, "A Comparison of Rendering Techniques for 3D
 * Line Sets with Transparency," in IEEE Transactions on Visualization and Computer Graphics, 2020.
 * doi: 10.1109/TVCG.2020.2975795
 * URL: http://ieeexplore.ieee.org/stamp/stamp.jsp?tp=&arnumber=9007507&isnumber=4359476
 */
class MLABRenderer : public LineRenderer {
public:
    MLABRenderer(SceneData* sceneData, sgl::TransferFunctionWindow& transferFunctionWindow);
    MLABRenderer(
            const std::string& windowName, SceneData* sceneData, sgl::TransferFunctionWindow &transferFunctionWindow);
    void initialize() override;
    ~MLABRenderer() override = default;
    [[nodiscard]] RenderingMode getRenderingMode() const override { return RENDERING_MODE_MLAB; }

    /**
     * Re-generates the visualization mapping.
     * @param lineData The render data.
     */
    void setLineData(LineDataPtr& lineData, bool isNewData) override;

    /// Sets the shader preprocessor defines used by the renderer.
    void getVulkanShaderPreprocessorDefines(std::map<std::string, std::string>& preprocessorDefines) override;
    void setGraphicsPipelineInfo(
            sgl::vk::GraphicsPipelineInfo& pipelineInfo, const sgl::vk::ShaderStagesPtr& shaderStages) override;
    void setRenderDataBindings(const sgl::vk::RenderDataPtr& renderData) override;
    void updateVulkanUniformBuffers() override;
    void setFramebufferAttachments(sgl::vk::FramebufferPtr& framebuffer, VkAttachmentLoadOp loadOp) override;

    /// Called when the resolution of the application window has changed.
    void onResolutionChanged() override;

    /// Called when the background clear color was changed.
    void onClearColorChanged() override;

    // Renders the object to the scene framebuffer.
    void render() override;
    /// Renders the entries in the property editor.
    void renderGuiPropertyEditorNodes(sgl::PropertyEditor& propertyEditor) override;

    /// For changing performance measurement modes.
    void setNewState(const InternalState& newState) override;

protected:
    void updateSyncMode();
    void updateLayerMode();
    virtual void reallocateFragmentBuffer();
    void setUniformData();
    void clear();
    void gather();
    void resolve();
    void reloadShaders();
    void reloadGatherShader() override;
    void reloadResolveShader();

    // Render passes.
    std::shared_ptr<ResolvePass> resolveRasterPass;
    std::shared_ptr<ResolvePass> clearRasterPass;

    // Stored fragment data.
    sgl::vk::BufferPtr fragmentBuffer;
    sgl::vk::BufferPtr spinlockViewportBuffer; ///< if (syncMode == SYNC_SPINLOCK)

    // Uniform data buffer shared by all shaders.
    struct UniformData {
        // Size of the viewport in x direction (in pixels).
        int viewportW{};

        // Range of logarithmic depth, used by child class MLABBucketRenderer.
        float logDepthMin{};
        float logDepthMax{};
    };
    UniformData uniformData = {};
    sgl::vk::BufferPtr uniformDataBuffer;

    // Window data.
    int windowWidth = 0, windowHeight = 0;
    int paddedWindowWidth = 0, paddedWindowHeight = 0;
    bool clearBitSet = true;
    size_t maxStorageBufferSize = 0;

    // Data for performance measurements.
    int frameCounter = 0;
    std::string currentStateName;
    bool timerDataIsWritten = true;
    sgl::vk::TimerPtr timer;

    // MLAB settings.
    int numLayers = 8;
    SyncMode syncMode = SYNC_FRAGMENT_SHADER_INTERLOCK; ///< Initialized depending on system capabilities.
    bool useOrderedFragmentShaderInterlock = true;
};

#endif //LINEVIS_MLABRENDERER_HPP
