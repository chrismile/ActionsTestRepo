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
#include "LineData/TrianglePayload/MeshletsDrawIndirectPayload.hpp"
#include "MeshletDrawCountAtomicPass.hpp"

MeshletDrawCountAtomicPass::MeshletDrawCountAtomicPass(sgl::vk::Renderer* renderer) : ComputePass(renderer) {}

void MeshletDrawCountAtomicPass::setLineData(LineDataPtr& lineData, bool isNewData) {
    this->lineData = lineData;
    dataDirty = true;
}

void MeshletDrawCountAtomicPass::setRecheckOccludedOnly(bool recheck) {
    if (recheckOccludedOnly != recheck) {
        recheckOccludedOnly = recheck;
        setShaderDirty();
    }
}

void MeshletDrawCountAtomicPass::setMaxNumPrimitivesPerMeshlet(uint32_t num) {
    if (maxNumPrimitivesPerMeshlet != num) {
        maxNumPrimitivesPerMeshlet = num;
        setShaderDirty();
    }
}

void MeshletDrawCountAtomicPass::setShallVisualizeNodes(bool _shallVisualizeNodes) {
    if (shallVisualizeNodes != _shallVisualizeNodes) {
        shallVisualizeNodes = _shallVisualizeNodes;
        setDataDirty();
        setShaderDirty();
    }
}

void MeshletDrawCountAtomicPass::setVisibilityCullingUniformBuffer(const sgl::vk::BufferPtr& uniformBuffer) {
    visibilityCullingUniformBuffer = uniformBuffer;
}

void MeshletDrawCountAtomicPass::setDepthBufferTexture(const sgl::vk::TexturePtr& texture) {
    depthBufferTexture = texture;
    setDataDirty();
}

void MeshletDrawCountAtomicPass::loadShader() {
    sgl::vk::ShaderManager->invalidateShaderCache();
    std::map<std::string, std::string> preprocessorDefines;
    preprocessorDefines.insert(std::make_pair("WORKGROUP_SIZE", std::to_string(WORKGROUP_SIZE)));
    if (recheckOccludedOnly) {
        preprocessorDefines.insert(std::make_pair("RECHECK_OCCLUDED_ONLY", ""));
    }
    if (shallVisualizeNodes) {
        preprocessorDefines.insert(std::make_pair("VISUALIZE_BVH_HIERARCHY", ""));
    }
    shaderStages = sgl::vk::ShaderManager->getShaderStages(
            { "MeshletDrawCountAtomicPass.Compute" }, preprocessorDefines);
}

void MeshletDrawCountAtomicPass::createComputeData(sgl::vk::Renderer* renderer, sgl::vk::ComputePipelinePtr& computePipeline) {
    computeData = std::make_shared<sgl::vk::ComputeData>(renderer, computePipeline);

    TubeTriangleRenderDataPayloadPtr payloadSuperClass(new MeshletsDrawIndirectPayload(
            maxNumPrimitivesPerMeshlet, shallVisualizeNodes));
    TubeTriangleRenderData tubeRenderData = lineData->getLinePassTubeTriangleMeshRenderDataPayload(
            true, false, payloadSuperClass);

    if (!tubeRenderData.indexBuffer) {
        return;
    }
    auto* payload = static_cast<MeshletsDrawIndirectPayload*>(payloadSuperClass.get());

    numMeshlets = payload->getNumMeshlets();
    indirectDrawCountBuffer = payload->getIndirectDrawCountBuffer();
    if (shallVisualizeNodes) {
        computeData->setStaticBuffer(payload->getNodeAabbBuffer(), "NodeAabbBuffer");
        computeData->setStaticBuffer(payload->getNodeAabbCountBuffer(), "NodeAabbCountBuffer");
    }
    computeData->setStaticBuffer(payload->getMeshletDataBuffer(), "MeshletDataBuffer");
    computeData->setStaticBuffer(payload->getMeshletVisibilityArrayBuffer(), "MeshletVisibilityArrayBuffer");
    computeData->setStaticBuffer(payload->getIndirectDrawBuffer(), "DrawIndexedIndirectCommandBuffer");
    computeData->setStaticBuffer(indirectDrawCountBuffer, "IndirectDrawCountBuffer");
    computeData->setStaticBuffer(visibilityCullingUniformBuffer, "VisibilityCullingUniformBuffer");
    computeData->setStaticTexture(depthBufferTexture, "depthBuffer");
}

void MeshletDrawCountAtomicPass::_render() {
    indirectDrawCountBuffer->fill(0, renderer->getVkCommandBuffer());
    renderer->insertBufferMemoryBarrier(
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            indirectDrawCountBuffer);
    renderer->dispatch(computeData, sgl::uiceil(numMeshlets, WORKGROUP_SIZE), 1, 1);
}