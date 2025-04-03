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

#include <memory>

#ifdef USE_TBB
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#endif

#include <Utils/AppSettings.hpp>
#include <Graphics/Vulkan/Render/Renderer.hpp>
#include <Graphics/Vulkan/Render/AccelerationStructure.hpp>
#include <Graphics/Vulkan/Render/CommandBuffer.hpp>
#include <ImGui/ImGuiWrapper.hpp>
#include <ImGui/imgui_custom.h>
#include <ImGui/Widgets/PropertyEditor.hpp>

#include "LineData/LineData.hpp"
#include "Renderers/LineRenderer.hpp"
#include "VulkanAmbientOcclusionBaker.hpp"

VulkanAmbientOcclusionBaker::VulkanAmbientOcclusionBaker(sgl::vk::Renderer* renderer)
        : AmbientOcclusionBaker(renderer) {
    aoComputeRenderPass = std::make_shared<AmbientOcclusionComputeRenderPass>(rendererMain);
}

VulkanAmbientOcclusionBaker::~VulkanAmbientOcclusionBaker() {
    if (workerThread.joinable()) {
        workerThread.join();
    }

    waitCommandBuffersFinished();
    aoComputeRenderPass = {};

    /*if (rendererVk) {
        delete rendererVk;
        rendererVk = nullptr;
    }*/
}

void VulkanAmbientOcclusionBaker::waitCommandBuffersFinished() {
    if (!commandBuffersVk.empty()) {
        if (commandBuffersUseFence) {
            commandBuffersUseFence->wait(std::numeric_limits<uint64_t>::max());
            commandBuffersUseFence = sgl::vk::FencePtr();
        }
        if (commandBuffers.empty()) {
            rendererMain->getDevice()->freeCommandBuffers(commandPool, commandBuffersVk);
        } else {
            commandBuffers.clear();
        }
        commandBuffersVk.clear();
    }
}

void VulkanAmbientOcclusionBaker::deriveOptimalAoSettingsFromLineData(LineDataPtr& lineData) {
    float linesLengthSum = 0.0f;
    std::vector<std::vector<glm::vec3>> lines = lineData->getFilteredLines(nullptr);
#ifdef USE_TBB
    linesLengthSum = tbb::parallel_reduce(
            tbb::blocked_range<size_t>(0, lines.size()), 0.0f,
            [&lines](tbb::blocked_range<size_t> const& r, float linesLengthSum) {
                for (auto lineIdx = r.begin(); lineIdx != r.end(); lineIdx++) {
#else
#if _OPENMP >= 201107
    #pragma omp parallel for reduction(+: linesLengthSum) shared(lines) default(none)
#endif
    for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
#endif
        std::vector<glm::vec3>& line = lines.at(lineIdx);
        const size_t n = line.size();
        float polylineLength = 0.0f;
        for (size_t i = 1; i < n; i++) {
            polylineLength += glm::length(line[i] - line[i-1]);
        }
        linesLengthSum += polylineLength;
    }
#ifdef USE_TBB
                return linesLengthSum;
            }, std::plus<>{});
#endif

    if (linesLengthSum <= 50.0f) { // Very small data set, e.g., cantilever (31.5167).
        maxNumIterations = 128;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 16;
        aoComputeRenderPass->expectedParamSegmentLength = 0.001f;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    } else if (linesLengthSum <= 500.0f) { // Small data set, e.g., femur (214.138) or rings (277.836).
        maxNumIterations = 128;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 4;
        aoComputeRenderPass->expectedParamSegmentLength = 0.001f;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    } else if (linesLengthSum <= 5000.0f) { // Medium-sized data set.
        maxNumIterations = 256;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 1;
        aoComputeRenderPass->expectedParamSegmentLength = 0.005f;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    } else { // Large data set, e.g., aneurysm (8530.48).
        maxNumIterations = 128;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 1;
        aoComputeRenderPass->expectedParamSegmentLength = 0.005f;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    }

    /*size_t numLineSegments = lineData->getNumLineSegments();
    if (numLineSegments <= 10000) { // Very small data set, e.g., cantilever (6302).
        maxNumIterations = 128;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 16;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    } else if (numLineSegments <= 100000) { // Small data set, e.g., femur (77307).
        maxNumIterations = 128;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 4;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    } else if (numLineSegments <= 1000000) { // Medium-sized data set, e.g., rings (243030).
        maxNumIterations = 256;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 1;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    } else { // Large data set, e.g., aneurysm (2267219).
        maxNumIterations = 128;
        aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame = 1;
        bakingMode = BakingMode::ITERATIVE_UPDATE;
    }*/

    // Stress line often intersect, which is why more subdivision might be necessary to get good-looking AO.
    if (lineData->getType() == DATA_SET_TYPE_STRESS_LINES) {
        aoComputeRenderPass->numTubeSubdivisionsNew = 16;
    } else {
        aoComputeRenderPass->numTubeSubdivisionsNew = 8;
    }
    aoComputeRenderPass->numTubeSubdivisions = aoComputeRenderPass->numTubeSubdivisionsNew;

    // aoComputeRenderPass->ambientOcclusionRadius, aoComputeRenderPass->expectedParamSegmentLength
}

void VulkanAmbientOcclusionBaker::startAmbientOcclusionBaking(LineDataPtr& lineData, bool isNewData) {
    if (lineData) {
        if (this->lineData != lineData) {
            deriveOptimalAoSettingsFromLineData(lineData);
        }
        this->lineData = lineData;

        aoComputeRenderPass->setLineData(lineData);
        aoBufferVk = aoComputeRenderPass->getAmbientOcclusionBufferVulkan();
    }

    if (aoComputeRenderPass->getNumLineVertices() == 0) {
        return;
    }

    if (workerThread.joinable()) {
        workerThread.join();
    }

    numIterations = 0;
    isDataReady = false;
    hasComputationFinished = false;
    hasThreadUpdate = false;
    if (bakingMode == BakingMode::IMMEDIATE) {
        bakeAoTexture();
    } else if (bakingMode == BakingMode::MULTI_THREADED) {
        threadFinished = false;
        aoBufferSizeThreaded = aoBufferVk->getSizeInBytes();
        workerThread = std::thread(&VulkanAmbientOcclusionBaker::bakeAoTextureThreadFunction, this);
    }
}

void VulkanAmbientOcclusionBaker::bakeAoTexture() {
    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();

    std::vector<sgl::vk::SemaphorePtr> waitSemaphores;
    std::vector<sgl::vk::SemaphorePtr> signalSemaphores;
    waitCommandBuffersFinished();

    sgl::vk::CommandPoolType commandPoolType;
    commandPoolType.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    //commandPoolType.queueFamilyIndex = device->getComputeQueueIndex();
    //commandBuffersVk = device->allocateCommandBuffers(commandPoolType, &commandPool, maxNumIterations);
    commandBuffers.reserve(maxNumIterations + 1);
    commandBuffersVk.reserve(maxNumIterations + 1);
    for (int i = 0; i < maxNumIterations + 1; i++) {
        commandBuffers.push_back(std::make_shared<sgl::vk::CommandBuffer>(device, commandPoolType));
        commandBuffersVk.push_back(commandBuffers.back()->getVkCommandBuffer());
    }

    waitSemaphores.resize(maxNumIterations + 1);
    signalSemaphores.resize(maxNumIterations + 1);

    for (int i = 1; i < maxNumIterations + 1; i++) {
        waitSemaphores.at(i) = std::make_shared<sgl::vk::Semaphore>(device);
        signalSemaphores.at(i - 1) = waitSemaphores.at(i);
    }
    waitSemaphores.front() = {};
    signalSemaphores.back() = {};

    while (numIterations < maxNumIterations) {
        const auto& commandBuffer = commandBuffers.at(numIterations);
        rendererMain->endCommandBuffer();
        if (signalSemaphores.at(numIterations)) {
            commandBuffer->pushSignalSemaphore(signalSemaphores.at(numIterations));
        }
        rendererMain->pushCommandBuffer(commandBuffer);
        rendererMain->beginCommandBuffer();
        if (waitSemaphores.at(numIterations)) {
            commandBuffer->pushWaitSemaphore(
                    waitSemaphores.at(numIterations), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        aoComputeRenderPass->setFrameNumber(numIterations);
        aoComputeRenderPass->render();
        rendererMain->insertMemoryBarrier(
                VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        // Submit the rendering operation in Vulkan.
        if (numIterations + 1 == maxNumIterations) {
            commandBuffersUseFence = std::make_shared<sgl::vk::Fence>(device, 0);
            commandBuffer->setFence(commandBuffersUseFence);
        }

        numIterations++;
    }
    waitSemaphoresTmp = waitSemaphores;
    signalSemaphoresTmp = signalSemaphores;

    const auto& commandBuffer = commandBuffers.at(numIterations);
    rendererMain->endCommandBuffer();
    if (signalSemaphores.at(numIterations)) {
        commandBuffer->pushSignalSemaphore(signalSemaphores.at(numIterations));
    }
    rendererMain->pushCommandBuffer(commandBuffer);
    rendererMain->beginCommandBuffer();
    if (waitSemaphores.at(numIterations)) {
        commandBuffer->pushWaitSemaphore(
                waitSemaphores.at(numIterations), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    isDataReady = true;
    hasComputationFinished = true;
}

void VulkanAmbientOcclusionBaker::bakeAoTextureThreadFunction() {
    int maxNumIterations = this->maxNumIterations;

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();

    std::vector<sgl::vk::SemaphorePtr> waitSemaphores;
    std::vector<sgl::vk::SemaphorePtr> signalSemaphores;
    std::vector<VkCommandBuffer> commandBuffers;

    aoBufferThreaded = std::make_shared<sgl::vk::Buffer>(
            device, aoBufferSizeThreaded,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, true);

    auto renderer = new sgl::vk::Renderer(device, 10);
    aoComputeRenderPass->setAoBufferTmp(aoBufferThreaded);
    aoComputeRenderPass->buildIfNecessary();
    aoComputeRenderPass->setRenderer(renderer);

    threadFinishedSemaphore = std::make_shared<sgl::vk::Semaphore>(device);

    VkCommandPool commandPool;
    sgl::vk::CommandPoolType commandPoolType;
    commandPoolType.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolType.queueFamilyIndex = device->getWorkerThreadGraphicsQueueIndex();
    commandPoolType.threadIndex = 1;
    commandBuffers = device->allocateCommandBuffers(commandPoolType, &commandPool, maxNumIterations);

    waitSemaphores.resize(maxNumIterations);
    signalSemaphores.resize(maxNumIterations);

    for (int i = 1; i < maxNumIterations; i++) {
        waitSemaphores.at(i) = std::make_shared<sgl::vk::Semaphore>(device);
        signalSemaphores.at(i - 1) = waitSemaphores.at(i);
    }
    waitSemaphores.front() = nullptr;
    signalSemaphores.back() = threadFinishedSemaphore;

    uint32_t numIterations = 0;
    while (numIterations < uint32_t(maxNumIterations)) {
        aoComputeRenderPass->setFrameNumber(numIterations);
        renderer->setCustomCommandBuffer(commandBuffers.at(numIterations), false);
        renderer->beginCommandBuffer();
        aoComputeRenderPass->render();
        if (int(numIterations) + 1 == maxNumIterations) {
            renderer->insertBufferMemoryBarrier(
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    //device->getComputeQueueIndex(), device->getGraphicsQueueIndex(),
                    aoBufferThreaded);
        } else {
            renderer->insertMemoryBarrier(
                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        renderer->endCommandBuffer();

        // Submit the rendering operation in Vulkan.
        sgl::vk::FencePtr fence;
        if (int(numIterations) + 1 == maxNumIterations) {
            fence = std::make_shared<sgl::vk::Fence>(device, 0);
        }
        renderer->submitToQueue(
                waitSemaphores.at(numIterations), signalSemaphores.at(numIterations), fence,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if (fence) {
            fence->wait(std::numeric_limits<uint64_t>::max());
        }

        numIterations++;
    }
    device->freeCommandBuffers(commandPool, commandBuffers);

    delete renderer;
    aoComputeRenderPass->setRenderer(rendererMain);
    aoComputeRenderPass->resetAoBufferTmp();

    hasThreadUpdate = true;
    threadFinished = true;
    hasComputationFinished = true;
}

void VulkanAmbientOcclusionBaker::updateIterative(VkPipelineStageFlags pipelineStageFlags) {
    aoComputeRenderPass->setFrameNumber(numIterations);

    aoComputeRenderPass->render();
    rendererMain->insertMemoryBarrier(
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, pipelineStageFlags);

    numIterations++;
    isDataReady = true;
    hasComputationFinished = numIterations >= maxNumIterations;
}

void VulkanAmbientOcclusionBaker::updateMultiThreaded(VkPipelineStageFlags pipelineStageFlags) {
    if (workerThread.joinable()) {
        workerThread.join();
    }

    //sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    waitCommandBuffersFinished();

    rendererMain->insertBufferMemoryBarrier(
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            //device->getComputeQueueIndex(), device->getGraphicsQueueIndex(),
            aoBufferThreaded);
    aoBufferThreaded->copyDataTo(aoBufferVk, rendererMain->getVkCommandBuffer());

    hasThreadUpdate = false;
    isDataReady = true;
    this->numIterations = maxNumIterations;
}

bool VulkanAmbientOcclusionBaker::getIsDataReady() {
    return isDataReady;
}

bool VulkanAmbientOcclusionBaker::getIsComputationRunning() {
    return numIterations < maxNumIterations;
}

bool VulkanAmbientOcclusionBaker::getHasComputationFinished() {
    return hasComputationFinished;
}

sgl::vk::BufferPtr VulkanAmbientOcclusionBaker::getAmbientOcclusionBuffer() {
    return aoComputeRenderPass->getAmbientOcclusionBufferVulkan();
}

sgl::vk::BufferPtr VulkanAmbientOcclusionBaker::getBlendingWeightsBuffer() {
    return aoComputeRenderPass->getBlendingWeightsBufferVulkan();
}

uint32_t VulkanAmbientOcclusionBaker::getNumTubeSubdivisions() {
    return aoComputeRenderPass->getNumTubeSubdivisions();
}

uint32_t VulkanAmbientOcclusionBaker::getNumLineVertices() {
    return aoComputeRenderPass->getNumLineVertices();
}

uint32_t VulkanAmbientOcclusionBaker::getNumParametrizationVertices() {
    return aoComputeRenderPass->getNumParametrizationVertices();
}

bool VulkanAmbientOcclusionBaker::renderGuiPropertyEditorNodes(sgl::PropertyEditor& propertyEditor) {
    bool dirty = false;
    bool parametrizationDirty = false;

    if (propertyEditor.beginNode("RTAO Baking")) {
        if (propertyEditor.addCombo(
                "Baking Mode", (int*)&bakingMode, BAKING_MODE_NAMES,
                IM_ARRAYSIZE(BAKING_MODE_NAMES))) {
            dirty = true;
        }

        if (propertyEditor.addSliderIntEdit(
                "#Iterations", &maxNumIterations, 1, 4096) == ImGui::EditMode::INPUT_FINISHED) {
            dirty = true;
        }
        if (propertyEditor.addSliderFloatEdit(
                "Line Resolution", &aoComputeRenderPass->expectedParamSegmentLength,
                0.0001f, 0.01f, "%.4f") == ImGui::EditMode::INPUT_FINISHED) {
            dirty = true;
            parametrizationDirty = true;
        }
        if (propertyEditor.addSliderIntEdit(
                "#Subdivisions", reinterpret_cast<int*>(&aoComputeRenderPass->numTubeSubdivisionsNew),
                3, 16) == ImGui::EditMode::INPUT_FINISHED) {
            aoComputeRenderPass->numTubeSubdivisions = aoComputeRenderPass->numTubeSubdivisionsNew;
            dirty = true;
            parametrizationDirty = true;
        }
        if (propertyEditor.addSliderIntEdit(
                "#Samples/Frame",
                reinterpret_cast<int*>(&aoComputeRenderPass->numAmbientOcclusionSamplesPerFrame),
                1, 4096) == ImGui::EditMode::INPUT_FINISHED) {
            dirty = true;
        }
        if (propertyEditor.addSliderFloatEdit(
                "AO Radius", &aoComputeRenderPass->ambientOcclusionRadius,
                0.01f, 0.2f) == ImGui::EditMode::INPUT_FINISHED) {
            dirty = true;
        }
        if (propertyEditor.addCheckbox("Use Distance-based AO", &aoComputeRenderPass->useDistance)) {
            dirty = true;
        }

        propertyEditor.endNode();
    }

    if (dirty) {
        if (parametrizationDirty) {
            aoComputeRenderPass->generateBlendingWeightParametrization();
            aoBufferVk = aoComputeRenderPass->getAmbientOcclusionBufferVulkan();
        }
        LineDataPtr lineData;
        startAmbientOcclusionBaking(lineData, false);
    }

    return dirty || (bakingMode == BakingMode::MULTI_THREADED && hasThreadUpdate);
}


AmbientOcclusionComputeRenderPass::AmbientOcclusionComputeRenderPass(sgl::vk::Renderer* renderer)
        : ComputePass(renderer) {
    lineRenderSettingsBuffer = std::make_shared<sgl::vk::Buffer>(
            device, sizeof(LineRenderSettings),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
}

void AmbientOcclusionComputeRenderPass::setLineData(LineDataPtr& lineData) {
    topLevelAS = lineData->getRayTracingTubeTriangleTopLevelAS();
    lines = lineData->getFilteredLines(nullptr);

    if (this->lineData && this->lineData->getType() != lineData->getType()) {
        setShaderDirty();
    }
    this->lineData = lineData;

    auto renderData = lineData->getLinePassTubeTriangleMeshRenderData(
            false, true);
    linePointDataBuffer = renderData.linePointDataBuffer;
    stressLinePointDataBuffer = renderData.stressLinePointDataBuffer;
    stressLinePointPrincipalStressDataBuffer = renderData.stressLinePointPrincipalStressDataBuffer;

    numLineVertices =
            linePointDataBuffer ? uint32_t(linePointDataBuffer->getSizeInBytes() / sizeof(LinePointDataUnified)) : 0;
    if (numLineVertices != 0) {
        generateBlendingWeightParametrization();
    } else {
        linesLengthSum = 0.0f;
        numPolylineSegments = 0;
        polylineLengths.clear();

        numParametrizationVertices = 0;

        blendingWeightParametrizationBuffer = sgl::vk::BufferPtr();
        lineSegmentVertexConnectivityBuffer = sgl::vk::BufferPtr();
        samplingLocationsBuffer = sgl::vk::BufferPtr();
        aoBufferVk = sgl::vk::BufferPtr();
    }
}

void AmbientOcclusionComputeRenderPass::generateBlendingWeightParametrization() {
    // First, compute data necessary for parametrizing the polylines (number of segments, segment lengths).
    linesLengthSum = 0.0f;
    numPolylineSegments = 0;
    polylineLengths.clear();
    polylineLengths.shrink_to_fit();
    polylineLengths.resize(lines.size());

#ifdef USE_TBB
    auto& lines = this->lines;
    auto& polylineLengths = this->polylineLengths;
    std::tie(linesLengthSum, numPolylineSegments) = tbb::parallel_reduce(
            tbb::blocked_range<size_t>(0, lines.size()), std::make_pair(0.0f, 0),
            [&lines, &polylineLengths](tbb::blocked_range<size_t> const& r, std::pair<float, uint32_t> init) {
                float& linesLengthSum = init.first;
                uint32_t& numPolylineSegments = init.second;
                for (auto lineIdx = r.begin(); lineIdx != r.end(); lineIdx++) {
#else
#if _OPENMP >= 201107
    #pragma omp parallel for reduction(+: linesLengthSum) reduction(+: numPolylineSegments) \
    shared(lines, polylineLengths) default(none)
#endif
    for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
#endif
        std::vector<glm::vec3>& line = lines.at(lineIdx);
        const size_t n = line.size();
        float polylineLength = 0.0f;
        for (size_t i = 1; i < n; i++) {
            polylineLength += glm::length(line[i] - line[i-1]);
        }
        polylineLengths.at(lineIdx) = polylineLength;
        linesLengthSum += polylineLength;
        numPolylineSegments += uint32_t(n) - 1;
    }
#ifdef USE_TBB
                return init;
            }, [&](std::pair<float, uint32_t> lhs, std::pair<float, uint32_t> rhs) -> std::pair<float, uint32_t> {
                return { lhs.first + rhs.first, lhs.second + rhs.second };
            });
#endif

    recomputeStaticParametrization();
}

void AmbientOcclusionComputeRenderPass::recomputeStaticParametrization() {
    std::vector<float> blendingWeightParametrizationData(numLineVertices, 0);
    std::vector<glm::uvec2> lineSegmentVertexConnectivityData;
    std::vector<float> samplingLocations;

    const float EPSILON = 1e-5f;
    const int approximateLineSegmentsTotal = int(std::ceil(linesLengthSum / expectedParamSegmentLength));
    lineSegmentVertexConnectivityData.reserve(approximateLineSegmentsTotal);
    samplingLocations.reserve(approximateLineSegmentsTotal);

    size_t segmentVertexIdOffset = 0;
    size_t vertexIdx = 0;
    for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
        std::vector<glm::vec3>& line = lines.at(lineIdx);
        const size_t n = line.size();
        float polylineLength = polylineLengths.at(lineIdx);

        uint32_t numLineSubdivs = std::max(1u, uint32_t(std::ceil(polylineLength / expectedParamSegmentLength)));
        float lineSubdivLength = polylineLength / float(numLineSubdivs);
        uint32_t numSubdivVertices = numLineSubdivs + 1;

        // Set the first vertex manually (we can guarantee there is no segment before it).
        assert(line.size() >= 2);
        auto startVertexIdx = uint32_t(vertexIdx);
        blendingWeightParametrizationData.at(vertexIdx) = float(segmentVertexIdOffset);
        vertexIdx++;

        // Compute the per-vertex blending weight parametrization.
        float currentLength = 0.0f;
        for (size_t i = 1; i < n; i++) {
            currentLength += glm::length(line[i] - line[i-1]);
            float w = currentLength / lineSubdivLength;
            blendingWeightParametrizationData.at(vertexIdx) =
                    float(segmentVertexIdOffset) + glm::clamp(w, 0.0f, float(numLineSubdivs) - EPSILON);
            vertexIdx++;
        }

        float lastLength = 0.0f;
        currentLength = glm::length(line[1] - line[0]);
        size_t currVertexIdx = 1;
        samplingLocations.push_back(float(startVertexIdx));
        for (uint32_t i = 1; i < numSubdivVertices; i++) {
            auto parametrizationIdx = uint32_t(currentLength / lineSubdivLength);
            while (i > parametrizationIdx && currVertexIdx < n - 1) {
                float segLength = glm::length(line[currVertexIdx + 1] - line[currVertexIdx]);
                lastLength = currentLength;
                currentLength += segLength;
                parametrizationIdx = uint32_t(currentLength / lineSubdivLength);
                currVertexIdx++;
            }

            float samplingLocation =
                    float(currVertexIdx - 1)
                    + (float(i) * lineSubdivLength - lastLength) / (currentLength - lastLength);
            samplingLocation = float(startVertexIdx) + std::min(samplingLocation, float(uint32_t(n) - 1u) - EPSILON);
            samplingLocations.push_back(samplingLocation);
        }

        if (numSubdivVertices == 1) {
            lineSegmentVertexConnectivityData.emplace_back(segmentVertexIdOffset, segmentVertexIdOffset);
        } else {
            lineSegmentVertexConnectivityData.emplace_back(segmentVertexIdOffset, segmentVertexIdOffset + 1);
            for (size_t i = 1; i < numSubdivVertices - 1; i++) {
                lineSegmentVertexConnectivityData.emplace_back(
                        segmentVertexIdOffset + i - 1u, segmentVertexIdOffset + i + 1u);
            }
            lineSegmentVertexConnectivityData.emplace_back(
                    segmentVertexIdOffset + numSubdivVertices - 2u, segmentVertexIdOffset + numSubdivVertices - 1u);
        }

        segmentVertexIdOffset += numSubdivVertices;
    }
    numParametrizationVertices = uint32_t(samplingLocations.size());

    blendingWeightParametrizationBuffer = std::make_shared<sgl::vk::Buffer>(
            device, numLineVertices * sizeof(float), blendingWeightParametrizationData.data(),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, true);

    lineSegmentVertexConnectivityBuffer = std::make_shared<sgl::vk::Buffer>(
            device, lineSegmentVertexConnectivityData.size() * sizeof(glm::uvec2),
            lineSegmentVertexConnectivityData.data(),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

    samplingLocationsBuffer = std::make_shared<sgl::vk::Buffer>(
            device, samplingLocations.size() * sizeof(float), samplingLocations.data(),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

    aoBufferVk = std::make_shared<sgl::vk::Buffer>(
            device, samplingLocations.size() * numTubeSubdivisions * sizeof(float),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY, true);

    dataDirty = true;
}

void AmbientOcclusionComputeRenderPass::loadShader() {
    sgl::vk::ShaderManager->invalidateShaderCache();
    std::map<std::string, std::string> preprocessorDefines;
    lineData->getVulkanShaderPreprocessorDefines(preprocessorDefines);
    shaderStages = sgl::vk::ShaderManager->getShaderStages(
            {"VulkanAmbientOcclusionBaker.Compute"}, preprocessorDefines);
}

void AmbientOcclusionComputeRenderPass::setComputePipelineInfo(sgl::vk::ComputePipelineInfo& pipelineInfo) {
}

void AmbientOcclusionComputeRenderPass::setRenderer(sgl::vk::Renderer* renderer) {
    this->renderer = renderer;
}

void AmbientOcclusionComputeRenderPass::setAoBufferTmp(const sgl::vk::BufferPtr& buffer) {
    aoBufferVkTmp = aoBufferVk;
    aoBufferVk = buffer;
    dataDirty = true;
}
void AmbientOcclusionComputeRenderPass::resetAoBufferTmp() {
    aoBufferVk = aoBufferVkTmp;
    aoBufferVkTmp = sgl::vk::BufferPtr();
    dataDirty = true;
}

void AmbientOcclusionComputeRenderPass::createComputeData(
        sgl::vk::Renderer* renderer, sgl::vk::ComputePipelinePtr& computePipeline) {
    computeData = std::make_shared<sgl::vk::ComputeData>(renderer, computePipeline);
    computeData->setStaticBuffer(lineRenderSettingsBuffer, "UniformsBuffer");
    computeData->setStaticBuffer(linePointDataBuffer, "LinePointDataBuffer");
    computeData->setStaticBufferOptional(stressLinePointDataBuffer, "StressLinePointDataBuffer");
    computeData->setStaticBufferOptional(
            stressLinePointPrincipalStressDataBuffer, "StressLinePointPrincipalStressDataBuffer");
    computeData->setStaticBuffer(samplingLocationsBuffer, "SamplingLocationsBuffer");
    computeData->setStaticBuffer(aoBufferVk, "AmbientOcclusionFactorsBuffer");
    computeData->setTopLevelAccelerationStructure(topLevelAS, "topLevelAS");
    lineData->setVulkanRenderDataDescriptors(computeData);
}

void AmbientOcclusionComputeRenderPass::_render() {
    lineData->updateVulkanUniformBuffers(nullptr, renderer);

    lineRenderSettings.lineRadius = LineRenderer::getLineWidth() * 0.5f;
    lineRenderSettings.bandRadius = LineRenderer::getBandWidth() * 0.5f;
    lineRenderSettings.minBandThickness = LineRenderer::getMinBandThickness();
    lineRenderSettings.ambientOcclusionRadius = ambientOcclusionRadius;
    lineRenderSettings.numLinePoints = numLineVertices;
    lineRenderSettings.numParametrizationVertices = numParametrizationVertices;
    lineRenderSettings.numTubeSubdivisions = numTubeSubdivisions;
    lineRenderSettings.numAmbientOcclusionSamples = numAmbientOcclusionSamplesPerFrame;
    lineRenderSettings.useDistance = uint32_t(useDistance);
    lineRenderSettingsBuffer->updateData(
            sizeof(LineRenderSettings), &lineRenderSettings, renderer->getVkCommandBuffer());

    renderer->insertBufferMemoryBarrier(
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_UNIFORM_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            lineRenderSettingsBuffer);

    renderer->dispatch(
            computeData, uint32_t(samplingLocationsBuffer->getSizeInBytes() / sizeof(float)),
            1, 1);
}
