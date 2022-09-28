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

#include <Math/Math.hpp>
#include <Graphics/Vulkan/Render/Renderer.hpp>
#include "LineData/LineData.hpp"
#include "LineData/TrianglePayload/NodesBVHTreePayload.hpp"
#include "PersistentThreadHelper.hpp"
#include "NodesBVHDrawCountPass.hpp"

NodesBVHDrawCountPass::NodesBVHDrawCountPass(sgl::vk::Renderer* renderer) : ComputePass(renderer) {}

void NodesBVHDrawCountPass::setLineData(LineDataPtr& lineData, bool isNewData) {
    this->lineData = lineData;
    dataDirty = true;
}

void NodesBVHDrawCountPass::setRecheckOccludedOnly(bool recheck) {
    if (recheckOccludedOnly != recheck) {
        recheckOccludedOnly = recheck;
        setShaderDirty();
    }
}

void NodesBVHDrawCountPass::setMaxNumPrimitivesPerMeshlet(uint32_t num) {
    if (maxNumPrimitivesPerMeshlet != num) {
        maxNumPrimitivesPerMeshlet = num;
        setDataDirty();
    }
}

void NodesBVHDrawCountPass::setVisibilityCullingUniformBuffer(const sgl::vk::BufferPtr& uniformBuffer) {
    visibilityCullingUniformBuffer = uniformBuffer;
}

void NodesBVHDrawCountPass::setDepthBufferTexture(const sgl::vk::TexturePtr& texture) {
    depthBufferTexture = texture;
    setDataDirty();
}

void NodesBVHDrawCountPass::loadShader() {
    sgl::vk::ShaderManager->invalidateShaderCache();
    std::map<std::string, std::string> preprocessorDefines;
    DevicePersistentThreadInfo threadInfo = getDevicePersistentThreadInfo(device);
    threadInfo.optimalWorkgroupSize = device->getPhysicalDeviceSubgroupProperties().subgroupSize;
    preprocessorDefines.insert(std::make_pair("WORKGROUP_SIZE", std::to_string(
            threadInfo.optimalWorkgroupSize)));
    preprocessorDefines.insert(std::make_pair("SUBGROUP_SIZE", std::to_string(
            device->getPhysicalDeviceSubgroupProperties().subgroupSize)));
    workgroupSize = threadInfo.optimalWorkgroupSize;
    if (recheckOccludedOnly) {
        preprocessorDefines.insert(std::make_pair("RECHECK_OCCLUDED_ONLY", ""));
    }
    shaderStages = sgl::vk::ShaderManager->getShaderStages(
            { "NodesBVHDrawCountPass.Traverse.Compute" }, preprocessorDefines);
}

void NodesBVHDrawCountPass::createComputeData(sgl::vk::Renderer* renderer, sgl::vk::ComputePipelinePtr& computePipeline) {
    computeData = std::make_shared<sgl::vk::ComputeData>(renderer, computePipeline);

    TubeTriangleRenderDataPayloadPtr payloadSuperClass(new NodesBVHTreePayload(maxNumPrimitivesPerMeshlet));
    TubeTriangleRenderData tubeRenderData = lineData->getLinePassTubeTriangleMeshRenderDataPayload(
            true, false, payloadSuperClass);

    if (!tubeRenderData.indexBuffer) {
        return;
    }
    auto* payload = static_cast<NodesBVHTreePayload*>(payloadSuperClass.get());

    DevicePersistentThreadInfo threadInfo = getDevicePersistentThreadInfo(device);
    numWorkgroups = threadInfo.optimalNumWorkgroups;
    numNodes = payload->getNumNodes();
    indirectDrawCountBuffer = payload->getIndirectDrawCountBuffer();
    computeData->setStaticBuffer(payload->getNodeDataBuffer(), "NodeBuffer");
    if (recheckOccludedOnly) {
        computeData->setStaticBuffer(payload->getQueueBufferRecheck(), "QueueBuffer");
        computeData->setStaticBuffer(payload->getQueueStateBufferRecheck(), "QueueStateBuffer");
    } else {
        computeData->setStaticBuffer(payload->getQueueBuffer(), "QueueBuffer");
        computeData->setStaticBuffer(payload->getQueueStateBuffer(), "QueueStateBuffer");
        computeData->setStaticBuffer(payload->getQueueBufferRecheck(), "QueueBufferRecheck");
        computeData->setStaticBuffer(payload->getQueueStateBufferRecheck(), "QueueStateBufferRecheck");
    }
    computeData->setStaticBuffer(payload->getIndirectDrawBuffer(), "DrawIndexedIndirectCommandBuffer");
    computeData->setStaticBuffer(indirectDrawCountBuffer, "IndirectDrawCountBuffer");
    computeData->setStaticBuffer(visibilityCullingUniformBuffer, "VisibilityCullingUniformBuffer");
    computeData->setStaticTexture(depthBufferTexture, "depthBuffer");
    computeData->setStaticBuffer(payload->getMaxWorkLeftTestBuffer(), "TestBuffer"); // For debugging.
    maxWorkLeftTestBuffer = payload->getMaxWorkLeftTestBuffer();
}

void NodesBVHDrawCountPass::_render() {
    indirectDrawCountBuffer->fill(0, renderer->getVkCommandBuffer());
    renderer->insertBufferMemoryBarrier(
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            indirectDrawCountBuffer);
    renderer->dispatch(
            computeData, std::min(sgl::uiceil(numNodes, workgroupSize), numWorkgroups),
            1, 1);
}
