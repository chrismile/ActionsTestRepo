/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2022, Christoph Neuhauser
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

#include <Utils/AppSettings.hpp>
#include <Graphics/Vulkan/Render/Renderer.hpp>
#include <Graphics/Vulkan/Render/CommandBuffer.hpp>
#include <Graphics/Vulkan/Render/RayTracingPipeline.hpp>
#include <ImGui/Widgets/PropertyEditor.hpp>
#include <memory>
#include <utility>
#include <ImGui/imgui_custom.h>
#include "Renderers/LineRenderer.hpp"
#include "LineData/LineData.hpp"

#include "Renderers/Scattering/Denoiser/EAWDenoiser.hpp"
#ifdef SUPPORT_OPTIX
#include "Renderers/Scattering/Denoiser/OptixVptDenoiser.hpp"
#endif

#include "VulkanRayTracedAmbientOcclusion.hpp"

VulkanRayTracedAmbientOcclusion::VulkanRayTracedAmbientOcclusion(SceneData* sceneData, sgl::vk::Renderer* renderer)
        : AmbientOcclusionBaker(renderer), sceneData(sceneData) {
    rtaoRenderPass = std::make_shared<VulkanRayTracedAmbientOcclusionPass>(
            sceneData, rendererMain, [this]() { this->onHasMoved(); });
    VulkanRayTracedAmbientOcclusion::onResolutionChanged();
}

VulkanRayTracedAmbientOcclusion::~VulkanRayTracedAmbientOcclusion() = default;

bool VulkanRayTracedAmbientOcclusion::needsReRender() {
    return rtaoRenderPass->needsReRender();
}

void VulkanRayTracedAmbientOcclusion::onHasMoved() {
    accumulatedFramesCounter = 0;
    rtaoRenderPass->onHasMoved();
}

void VulkanRayTracedAmbientOcclusion::onResolutionChanged() {
    uint32_t width = *sceneData->viewportWidth;
    uint32_t height = *sceneData->viewportHeight;
    hasTextureResolutionChanged = true;
    rtaoRenderPass->recreateSwapchain(width, height);
    onHasMoved();
}

void VulkanRayTracedAmbientOcclusion::startAmbientOcclusionBaking(LineDataPtr& lineData, bool isNewData) {
    if (lineData) {
        this->lineData = lineData;
        rtaoRenderPass->setLineData(lineData, isNewData);
    }

    accumulatedFramesCounter = 0;
    rtaoRenderPass->onHasMoved();
    isDataReady = false;
    hasComputationFinished = false;
}

void VulkanRayTracedAmbientOcclusion::updateIterative(VkPipelineStageFlags pipelineStageFlags) {
    sgl::vk::TexturePtr aoTextureVk = rtaoRenderPass->getAmbientOcclusionTextureVk();

    rtaoRenderPass->setFrameNumber(accumulatedFramesCounter);
    rtaoRenderPass->render();
    rendererMain->insertImageMemoryBarrier(
            aoTextureVk->getImage(),
            aoTextureVk->getImage()->getVkImageLayout(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, pipelineStageFlags,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

    accumulatedFramesCounter++;
    isDataReady = true;
    hasComputationFinished = accumulatedFramesCounter >= maxNumAccumulatedFrames;
}

sgl::vk::TexturePtr VulkanRayTracedAmbientOcclusion::getAmbientOcclusionFrameTexture()  {
    return rtaoRenderPass->getAmbientOcclusionTextureVk();
}

bool VulkanRayTracedAmbientOcclusion::getHasTextureResolutionChanged() {
    bool tmp = hasTextureResolutionChanged;
    hasTextureResolutionChanged = false;
    return tmp;
}

bool VulkanRayTracedAmbientOcclusion::setNewSettings(const SettingsMap& settings) {
    bool optionChanged = false;

    if (settings.getValueOpt("ambient_occlusion_iterations", maxNumAccumulatedFrames)) {
        optionChanged = true;
    }
    if (settings.getValueOpt(
            "ambient_occlusion_samples_per_frame", rtaoRenderPass->numAmbientOcclusionSamplesPerFrame)) {
        optionChanged = true;
    }
    if (settings.getValueOpt("ambient_occlusion_radius", rtaoRenderPass->ambientOcclusionRadius)) {
        optionChanged = true;
    }
    if (settings.getValueOpt("ambient_occlusion_distance_based", rtaoRenderPass->useDistance)) {
        optionChanged = true;
    }

    if (rtaoRenderPass->setNewSettings(settings)) {
        optionChanged = true;
    }
    if (optionChanged) {
        accumulatedFramesCounter = 0;
        rtaoRenderPass->onHasMoved();
    }

    return optionChanged;
}

bool VulkanRayTracedAmbientOcclusion::renderGuiPropertyEditorNodes(sgl::PropertyEditor& propertyEditor) {
    bool optionChanged = false;

    if (propertyEditor.beginNode("RTAO")) {
        if (propertyEditor.addSliderIntEdit(
                "#Iterations", &maxNumAccumulatedFrames,
                1, 4096) == ImGui::EditMode::INPUT_FINISHED) {
            optionChanged = true;
        }
        if (propertyEditor.addSliderIntEdit(
                "#Samples/Frame",
                reinterpret_cast<int*>(&rtaoRenderPass->numAmbientOcclusionSamplesPerFrame),
                1, 4096) == ImGui::EditMode::INPUT_FINISHED) {
            optionChanged = true;
        }
        if (propertyEditor.addSliderFloatEdit(
                "AO Radius", &rtaoRenderPass->ambientOcclusionRadius,
                0.01f, 0.2f) == ImGui::EditMode::INPUT_FINISHED) {
            optionChanged = true;
        }
        if (propertyEditor.addCheckbox("Use Distance-based AO", &rtaoRenderPass->useDistance)) {
            optionChanged = true;
        }

        if (rtaoRenderPass->renderGuiPropertyEditorNodes(propertyEditor)) {
            optionChanged = true;
        }

        propertyEditor.endNode();
    }

    if (optionChanged) {
        accumulatedFramesCounter = 0;
        rtaoRenderPass->onHasMoved();
    }

    return optionChanged;
}



VulkanRayTracedAmbientOcclusionPass::VulkanRayTracedAmbientOcclusionPass(
        SceneData* sceneData, sgl::vk::Renderer* renderer, std::function<void()> onHasMovedCallback)
        : ComputePass(renderer), sceneData(sceneData), onHasMovedParent(std::move(onHasMovedCallback)) {
    uniformBuffer = std::make_shared<sgl::vk::Buffer>(
            device, sizeof(UniformData),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

    blitResultRenderPass = std::make_shared<sgl::vk::BlitRenderPass>(renderer);

    createDenoiser();
}

void VulkanRayTracedAmbientOcclusionPass::createDenoiser() {
    denoiser = createDenoiserObject(denoiserType, renderer, DenoisingMode::AMBIENT_OCCLUSION);
    if (denoiser) {
        denoiser->setFileDialogInstance(fileDialogInstance);
    }

    if (accumulationTexture) {
        checkRecreateFeatureMaps();
        setDenoiserFeatureMaps();
        if (denoiser) {
            denoiser->recreateSwapchain(lastViewportWidth, lastViewportHeight);
        }
    }
}

void VulkanRayTracedAmbientOcclusionPass::setDenoiserFeatureMaps() {
    if (denoiser) {
        denoiser->setFeatureMap(FeatureMapType::COLOR, accumulationTexture);
        if (denoiser->getUseFeatureMap(FeatureMapType::NORMAL)) {
            denoiser->setFeatureMap(FeatureMapType::NORMAL, normalMapTexture);
        }
        if (denoiser->getUseFeatureMap(FeatureMapType::DEPTH)) {
            denoiser->setFeatureMap(FeatureMapType::DEPTH, depthMapTexture);
        }
        if (denoiser->getUseFeatureMap(FeatureMapType::POSITION)) {
            denoiser->setFeatureMap(FeatureMapType::POSITION, positionMapTexture);
        }
        if (denoiser->getUseFeatureMap(FeatureMapType::ALBEDO)) {
            denoiser->setFeatureMap(FeatureMapType::ALBEDO, albedoTexture);
        }
        if (denoiser->getUseFeatureMap(FeatureMapType::FLOW)) {
            denoiser->setFeatureMap(FeatureMapType::FLOW, flowMapTexture);
        }
        denoiser->setOutputImage(denoisedTexture->getImageView());
    }
}

void VulkanRayTracedAmbientOcclusionPass::recreateSwapchain(uint32_t width, uint32_t height) {
    lastViewportWidth = width;
    lastViewportHeight = height;

    sgl::vk::ImageSamplerSettings samplerSettings;
    sgl::vk::ImageSettings imageSettings;
    imageSettings.width = width;
    imageSettings.height = height;
    imageSettings.format = VK_FORMAT_R32G32B32A32_SFLOAT;

    imageSettings.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    accumulationTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    imageSettings.usage =
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT
            | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    denoisedTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);

    imageSettings.usage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
            | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    resultTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);

    recreateFeatureMaps();

    blitResultRenderPass->setInputTexture(accumulationTexture);
    blitResultRenderPass->setOutputImage(resultTexture->getImageView());
    blitResultRenderPass->recreateSwapchain(width, height);

    if (useDenoiser && denoiser) {
        denoiser->recreateSwapchain(width, height);
    }

    setDataDirty();
}

void VulkanRayTracedAmbientOcclusionPass::recreateFeatureMaps() {
    sgl::vk::ImageSamplerSettings samplerSettings;
    sgl::vk::ImageSettings imageSettings;
    imageSettings.width = lastViewportWidth;
    imageSettings.height = lastViewportHeight;

    normalMapTexture = {};
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::NORMAL)) {
        imageSettings.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageSettings.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        normalMapTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    }

    depthMapTexture = {};
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::DEPTH)) {
        imageSettings.format = VK_FORMAT_R32_SFLOAT;
        imageSettings.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        depthMapTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    }

    positionMapTexture = {};
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::POSITION)) {
        imageSettings.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageSettings.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        positionMapTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    }

    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::ALBEDO)) {
        imageSettings.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageSettings.usage =
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        albedoTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
        VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
        albedoTexture->getImage()->transitionImageLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, commandBuffer);
        albedoTexture->getImageView()->clearColor(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), commandBuffer);
        device->endSingleTimeCommands(commandBuffer);
    }

    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::FLOW)) {
        imageSettings.format = VK_FORMAT_R32G32_SFLOAT;
        imageSettings.usage =
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        flowMapTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    }

    setDenoiserFeatureMaps();
}

void VulkanRayTracedAmbientOcclusionPass::checkRecreateFeatureMaps() {
    bool useNormalMapRenderer = normalMapTexture.get() != nullptr;
    bool useDepthRenderer = depthMapTexture.get() != nullptr;
    bool usePositionRenderer = positionMapTexture.get() != nullptr;
    bool useAlbedoRenderer = albedoTexture.get() != nullptr;
    bool useFlowRenderer = flowMapTexture.get() != nullptr;

    bool shallRecreateFeatureMaps = false;
    if (denoiser) {
        if (useNormalMapRenderer != denoiser->getUseFeatureMap(FeatureMapType::NORMAL)
                || useDepthRenderer != denoiser->getUseFeatureMap(FeatureMapType::DEPTH)
                || usePositionRenderer != denoiser->getUseFeatureMap(FeatureMapType::POSITION)
                || useAlbedoRenderer != denoiser->getUseFeatureMap(FeatureMapType::ALBEDO)
                || useFlowRenderer != denoiser->getUseFeatureMap(FeatureMapType::FLOW)) {
            shallRecreateFeatureMaps = true;
        }
    } else {
        if (useNormalMapRenderer || useDepthRenderer || usePositionRenderer || useAlbedoRenderer || useFlowRenderer) {
            shallRecreateFeatureMaps = true;
        }
    }

    if (shallRecreateFeatureMaps) {
        setShaderDirty();
        device->waitIdle();
        recreateFeatureMaps();
        onHasMovedParent();
        changedDenoiserSettings = false;
    }
}

void VulkanRayTracedAmbientOcclusionPass::setLineData(LineDataPtr& data, bool isNewData) {
    lineData = data;
    topLevelAS = lineData->getRayTracingTubeTriangleTopLevelAS();
    tubeTriangleRenderData = lineData->getLinePassTubeTriangleMeshRenderData(false, true);

    bool useSplitBlasesNew = tubeTriangleRenderData.instanceTriangleIndexOffsetBuffer.get() != nullptr;
    if (useSplitBlases != useSplitBlasesNew) {
        useSplitBlases = useSplitBlasesNew;
        setShaderDirty();
    }

    if (denoiser) {
        denoiser->resetFrameNumber();
    }
    lastFrameViewProjectionMatrix = sceneData->camera->getProjectionMatrix() * sceneData->camera->getViewMatrix();

    uniformData.frameNumber = 0;
    setDataDirty();
}

void VulkanRayTracedAmbientOcclusionPass::setFileDialogInstance(ImGuiFileDialog* _fileDialogInstance) {
    this->fileDialogInstance = _fileDialogInstance;
}

void VulkanRayTracedAmbientOcclusionPass::onHasMoved() {
    uniformData.frameNumber = 0;
}

void VulkanRayTracedAmbientOcclusionPass::loadShader() {
    sgl::vk::ShaderManager->invalidateShaderCache();
    std::map<std::string, std::string> preprocessorDefines;
    if (useSplitBlases) {
        preprocessorDefines.insert(std::make_pair("USE_INSTANCE_TRIANGLE_INDEX_OFFSET", ""));
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::NORMAL)) {
        preprocessorDefines.insert(std::make_pair("WRITE_NORMAL_MAP", ""));
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::DEPTH)) {
        preprocessorDefines.insert(std::make_pair("WRITE_DEPTH_MAP", ""));
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::POSITION)) {
        preprocessorDefines.insert(std::make_pair("WRITE_POSITION_MAP", ""));
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::FLOW)) {
        preprocessorDefines.insert(std::make_pair("WRITE_FLOW_MAP", ""));
    }
    shaderStages = sgl::vk::ShaderManager->getShaderStages(
            { "VulkanRayTracedAmbientOcclusion.Compute" }, preprocessorDefines);
}

void VulkanRayTracedAmbientOcclusionPass::setComputePipelineInfo(sgl::vk::ComputePipelineInfo& pipelineInfo) {
}

void VulkanRayTracedAmbientOcclusionPass::createComputeData(
        sgl::vk::Renderer* renderer, sgl::vk::ComputePipelinePtr& computePipeline) {
    computeData = std::make_shared<sgl::vk::ComputeData>(renderer, computePipeline);
    computeData->setStaticImageView(accumulationTexture->getImageView(), "outputImage");
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::NORMAL)) {
        computeData->setStaticImageView(normalMapTexture->getImageView(), "normalMap");
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::DEPTH)) {
        computeData->setStaticImageView(depthMapTexture->getImageView(), "depthMap");
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::POSITION)) {
        computeData->setStaticImageView(positionMapTexture->getImageView(), "positionMap");
    }
    if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::FLOW)) {
        computeData->setStaticImageView(flowMapTexture->getImageView(), "flowMap");
    }
    computeData->setTopLevelAccelerationStructure(topLevelAS, "topLevelAS");
    computeData->setStaticBuffer(uniformBuffer, "UniformsBuffer");

    // Geometry data.
    computeData->setStaticBuffer(
            tubeTriangleRenderData.indexBuffer, "TubeIndexBuffer");
    computeData->setStaticBuffer(
            tubeTriangleRenderData.vertexBuffer, "TubeTriangleVertexDataBuffer");
    computeData->setStaticBuffer(
            tubeTriangleRenderData.linePointDataBuffer, "LinePointDataBuffer");
    if (tubeTriangleRenderData.instanceTriangleIndexOffsetBuffer) {
        computeData->setStaticBuffer(
                tubeTriangleRenderData.instanceTriangleIndexOffsetBuffer,
                "InstanceTriangleIndexOffsetBuffer");
    }
}

void VulkanRayTracedAmbientOcclusionPass::_render() {
    if (!changedDenoiserSettings) {
        uniformData.viewMatrix = sceneData->camera->getViewMatrix();
        uniformData.inverseViewMatrix = glm::inverse(uniformData.viewMatrix);
        uniformData.inverseProjectionMatrix = glm::inverse(sceneData->camera->getProjectionMatrix());
        uniformData.inverseTransposedViewMatrix = glm::transpose(uniformData.inverseViewMatrix);
        uniformData.lastFrameViewProjectionMatrix = lastFrameViewProjectionMatrix;
        uniformData.numSamplesPerFrame = numAmbientOcclusionSamplesPerFrame;
        uniformData.useDistance = useDistance;
        uniformData.ambientOcclusionRadius = ambientOcclusionRadius;
        //float radius = LineRenderer::getLineWidth();
        //if (lineData->getUseBandRendering()) {
        //    radius = std::max(LineRenderer::getLineWidth(), LineRenderer::getBandWidth());
        //}
        //uniformData.subdivisionCorrectionFactor =
        //        radius * (1.0f - std::cos(sgl::PI / float(lineData->getTubeNumSubdivisions())));
        uniformData.subdivisionCorrectionFactor = std::cos(sgl::PI / float(lineData->getTubeNumSubdivisions()));
        uniformBuffer->updateData(
                sizeof(UniformData), &uniformData, renderer->getVkCommandBuffer());

        renderer->insertBufferMemoryBarrier(
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                uniformBuffer);

        renderer->transitionImageLayout(accumulationTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL);
        if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::NORMAL)) {
            renderer->transitionImageLayout(normalMapTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL);
        }
        if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::DEPTH)) {
            renderer->transitionImageLayout(depthMapTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL);
        }
        if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::POSITION)) {
            renderer->transitionImageLayout(positionMapTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL);
        }
        if (denoiser && denoiser->getUseFeatureMap(FeatureMapType::FLOW)) {
            renderer->transitionImageLayout(flowMapTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL);
        }
        auto& imageSettings = accumulationTexture->getImage()->getImageSettings();
        int width = int(imageSettings.width);
        int height = int(imageSettings.height);
        int groupCountX = sgl::iceil(width, 16);
        int groupCountY = sgl::iceil(height, 16);
        if (topLevelAS) {
            renderer->dispatch(computeData, groupCountX, groupCountY, 1);
        }
    }
    lastFrameViewProjectionMatrix = sceneData->camera->getProjectionMatrix() * sceneData->camera->getViewMatrix();
    changedDenoiserSettings = false;

    if (useDenoiser && denoiser && denoiser->getIsEnabled()) {
        denoiser->denoise();
        renderer->transitionImageLayout(
                denoisedTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        renderer->transitionImageLayout(
                resultTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        denoisedTexture->getImage()->blit(
                resultTexture->getImage(), renderer->getVkCommandBuffer());
    } else {
        renderer->transitionImageLayout(
                accumulationTexture->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        blitResultRenderPass->render();
        /*renderer->transitionImageLayout(
                 accumulationTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
         renderer->transitionImageLayout(
                 resultTextureVk->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
         accumulationTexture->getImage()->blit(
                 resultTextureVk->getImage(), renderer->getVkCommandBuffer());*/
    }

    /*renderer->transitionImageLayout(
            resultTextureVk->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);*/
    /*renderer->insertImageMemoryBarrier(
            resultTextureVk->getImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);*/
}

bool VulkanRayTracedAmbientOcclusionPass::setNewSettings(const SettingsMap& settings) {
    bool optionChanged = false;

    int numDenoisersSupported = IM_ARRAYSIZE(DENOISER_NAMES);
#ifdef SUPPORT_OPTIX
    if (!OptixVptDenoiser::isOptixEnabled()) {
        numDenoisersSupported--;
    }
#endif
    std::string denoiserName;
    if (settings.getValueOpt("ambient_occlusion_denoiser", denoiserName)) {
        for (int i = 0; i < numDenoisersSupported; i++) {
            if (denoiserName == DENOISER_NAMES[i]) {
                if (denoiserType == DenoiserType(i)) {
                    break;
                }
                denoiserType = DenoiserType(i);
                createDenoiser();
                reRender = true;
                changedDenoiserSettings = true;
                break;
            }
        }
    }

    return optionChanged;
}

bool VulkanRayTracedAmbientOcclusionPass::renderGuiPropertyEditorNodes(sgl::PropertyEditor &propertyEditor) {
    bool optionChanged = false;

    int numDenoisersSupported = IM_ARRAYSIZE(DENOISER_NAMES);
#ifdef SUPPORT_OPTIX
    if (!OptixVptDenoiser::isOptixEnabled()) {
        numDenoisersSupported--;
    }
#endif
    if (propertyEditor.addCombo(
            "Denoiser", (int*)&denoiserType, DENOISER_NAMES, numDenoisersSupported)) {
        reRender = true;
        changedDenoiserSettings = true;
        createDenoiser();
    }

    if (useDenoiser && denoiser) {
        if (propertyEditor.beginNode(denoiser->getDenoiserName())) {
            bool denoiserReRender = denoiser->renderGuiPropertyEditorNodes(propertyEditor);
            reRender = denoiserReRender || reRender;
            changedDenoiserSettings = denoiserReRender || changedDenoiserSettings;
            if (denoiserReRender) {
                checkRecreateFeatureMaps();
            }
            propertyEditor.endNode();
        }
    }

    return optionChanged;
}
