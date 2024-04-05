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

#ifdef USE_TBB
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#endif

#include <Utils/Dialog.hpp>
#include <Utils/File/Logfile.hpp>
#include <Graphics/Vulkan/Shader/ShaderManager.hpp>
#include <Graphics/Vulkan/Render/Data.hpp>
#include <Graphics/Vulkan/Render/Renderer.hpp>
#include <Graphics/Vulkan/Render/AccelerationStructure.hpp>

#include <Utils/Mesh/TriangleNormals.hpp>
#include <Utils/Mesh/MeshSmoothing.hpp>
#include <ImGui/imgui.h>
#include <ImGui/imgui_custom.h>
#include <ImGui/Widgets/PropertyEditor.hpp>

#include "Renderers/LineRenderer.hpp"
#include "Mesh/MeshBoundarySurface.hpp"
#include "LineData.hpp"

LineData::LinePrimitiveMode LineData::linePrimitiveMode = LineData::LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL;
int LineData::tubeNumSubdivisions = 6;
bool LineData::renderThickBands = true;
float LineData::minBandThickness = 0.15f;

const char *const LINE_PRIMITIVE_MODE_DISPLAYNAMES[] = {
        "Quads (Programmable Pull)",
        "Quads (Geometry Shader)",
        "Tube (Programmable Pull)",
        "Tube (Geometry Shader)",
        "Tube (Triangle Mesh)",
#ifdef VK_EXT_mesh_shader
        "Tube (Mesh Shader)",
#endif
        "Tube (Mesh Shader NV)",
        "Ribbon Quads (Geometry Shader)",
        "Tube Ribbons (Programmable Pull)",
        "Tube Ribbons (Geometry Shader)",
        "Tube Ribbons (Triangle Mesh)",
#ifdef VK_EXT_mesh_shader
        "Tube Ribbons (Mesh Shader)",
#endif
        "Tube Ribbons (Mesh Shader NV)",
};

LineData::LineData(sgl::TransferFunctionWindow &transferFunctionWindow, DataSetType dataSetType)
        : dataSetType(dataSetType), transferFunctionWindow(transferFunctionWindow) {
    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    lineUniformDataBuffer = std::make_shared<sgl::vk::Buffer>(
            device, sizeof(lineUniformData),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);
}

LineData::~LineData() = default;

bool LineData::setNewSettings(const SettingsMap& settings) {
    bool reloadGatherShader = false;

    std::string attributeName;
    if (settings.getValueOpt("attribute", attributeName)) {
        int i;
        for (i = 0; i < int(attributeNames.size()); i++) {
            if (attributeNames.at(i) == attributeName) {
                selectedAttributeIndexUi = i;
                break;
            }
        }
        if (i != int(attributeNames.size())) {
            setSelectedAttributeIndex(selectedAttributeIndexUi);
        } else {
            sgl::Logfile::get()->writeError(
                    "Error in LineData::setNewSettings: Invalid attribute name \"" + attributeName + "\".");
        }
    }

    std::string linePrimitiveModeName;
    if (settings.getValueOpt("line_primitive_mode", linePrimitiveModeName)) {
        int i;
        for (i = 0; i < IM_ARRAYSIZE(LINE_PRIMITIVE_MODE_DISPLAYNAMES); i++) {
            if (LINE_PRIMITIVE_MODE_DISPLAYNAMES[i] == linePrimitiveModeName) {
                linePrimitiveMode = LinePrimitiveMode(i);
                break;
            }
        }
        if (i != IM_ARRAYSIZE(LINE_PRIMITIVE_MODE_DISPLAYNAMES)) {
            if (!lineRenderersCached.empty()) {
                updateLinePrimitiveMode(lineRenderersCached.front());
                reloadGatherShader = true;
            }
        } else {
            sgl::Logfile::get()->writeError(
                    "Error in LineData::setNewSettings: Invalid line primitive mode name \""
                    + linePrimitiveModeName + "\".");
        }
    }

    int linePrimitiveModeIndex = 0;
    if (settings.getValueOpt("line_primitive_mode_index", linePrimitiveModeIndex)) {
        if (linePrimitiveModeIndex < 0 || linePrimitiveModeIndex >= IM_ARRAYSIZE(LINE_PRIMITIVE_MODE_DISPLAYNAMES)) {
            sgl::Logfile::get()->writeError(
                    "Error in LineData::setNewSettings: Invalid line primitive mode index \""
                    + std::to_string(linePrimitiveModeIndex) + "\".");
        }
        linePrimitiveMode = LinePrimitiveMode(linePrimitiveModeIndex);
        if (!lineRenderersCached.empty()) {
            updateLinePrimitiveMode(lineRenderersCached.front());
            reloadGatherShader = true;
        }
    }

    if (!simulationMeshOutlineTriangleIndices.empty()) {
        if (settings.getValueOpt("hull_opacity", hullOpacity)) {
            shallRenderSimulationMeshBoundary = hullOpacity > 0.0f;
            reRender = true;
            for (auto* lineRenderer : lineRenderersCached) {
                if (lineRenderer && !lineRenderer->isRasterizer) {
                    lineRenderer->setRenderSimulationMeshHull(
                            shallRenderSimulationMeshBoundary);
                }
            }
        }
    }

    if (settings.getValueOpt("tube_num_subdivisions", tubeNumSubdivisions)) {
        for (auto* lineRenderer : lineRenderersCached) {
            if (lineRenderer && lineRenderer->getIsRasterizer()) {
                reloadGatherShader = true;
            }
        }
        if (getLinePrimitiveModeUsesMeshShader(linePrimitiveMode)) {
            dirty = true;
        }
        setTriangleRepresentationDirty();
    }

    if (settings.getValueOpt("use_capped_tubes", useCappedTubes)) {
        triangleRepresentationDirty = true;
        for (auto* lineRenderer : lineRenderersCached) {
            if (lineRenderer && !lineRenderer->isRasterizer) {
                reloadGatherShader = true;
            }
        }
    }

    if (settings.getValueOpt("use_halos", useHalos)) {
        reloadGatherShader = true;
    }

    return reloadGatherShader;
}

bool LineData::getCanUseLiveUpdate(LineDataAccessType accessType) const {
    if (accessType == LineDataAccessType::FILTERED_LINES) {
        return getIsSmallDataSet();
    }
    bool canUseLiveUpdate = std::all_of(
            lineRenderersCached.cbegin(), lineRenderersCached.cend(), [accessType](LineRenderer* lineRenderer){
                return lineRenderer->getCanUseLiveUpdate(accessType);
            });
    return canUseLiveUpdate;
}

bool LineData::setUseCappedTubes(LineRenderer* lineRenderer, bool cappedTubes) {
    bool useCappedTubesOld = useCappedTubes;
    useCappedTubes = cappedTubes;
    if (useCappedTubesOld != cappedTubes) {
        triangleRepresentationDirty = true;
        if (!lineRenderer->isRasterizer) {
            return true;
        }
    }
    return false;
}

bool LineData::updateLinePrimitiveMode(LineRenderer* lineRenderer) {
    bool shallReloadGatherShader = false;
    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    bool unsupportedLineRenderingMode = false;
    std::string warningText;
    if (getLinePrimitiveModeUsesGeometryShader(linePrimitiveMode)
            && !device->getPhysicalDeviceFeatures().geometryShader) {
        unsupportedLineRenderingMode = true;
        warningText =
                "The selected line primitives mode uses geometry shaders, but geometry shaders are not "
                "supported by the used GPU.";
    }
#ifdef VK_EXT_mesh_shader
    if ((linePrimitiveMode == LINE_PRIMITIVES_TUBE_MESH_SHADER
                || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_MESH_SHADER)
            && !device->getPhysicalDeviceMeshShaderFeaturesEXT().meshShader) {
        unsupportedLineRenderingMode = true;
        warningText =
                "The selected line primitives mode uses mesh shaders via the VK_EXT_mesh_shader extension, "
                "but the extension is not supported by the used GPU.";
    }
#endif
    if ((linePrimitiveMode == LINE_PRIMITIVES_TUBE_MESH_SHADER_NV
                || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_MESH_SHADER_NV)
            && !device->getPhysicalDeviceMeshShaderFeaturesNV().meshShader) {
        unsupportedLineRenderingMode = true;
        warningText =
                "The selected line primitives mode uses mesh shaders via the VK_NV_mesh_shader extension, "
                "but the extension is not supported by the used GPU.";
    }
    if (unsupportedLineRenderingMode) {
        sgl::Logfile::get()->writeWarning(
                "Warning in LineData::renderGuiPropertyEditorNodesRenderer: " + warningText,
                false);
        auto handle = sgl::dialog::openMessageBox(
                "Unsupported Line Primitives Mode", warningText, sgl::dialog::Icon::WARNING);
        lineRenderer->getSceneData()->nonBlockingMsgBoxHandles->push_back(handle);
        linePrimitiveMode = LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL;
    }
    dirty = true;

    if (lineRenderer && lineRenderer->useAmbientOcclusion && lineRenderer->ambientOcclusionBaker && getUseCappedTubes()
            && lineRenderer->isRasterizer && !lineRenderer->getIsTriangleRepresentationUsedByPrimitiveMode()) {
        setUseCappedTubes(lineRenderer, false);
    }

    shallReloadGatherShader = true;
    return shallReloadGatherShader;
}

bool LineData::renderGuiPropertyEditorNodesRenderer(sgl::PropertyEditor& propertyEditor, LineRenderer* lineRenderer) {
    bool shallReloadGatherShader = false;

    if (lineRenderer->getIsRasterizer() && getType() != DATA_SET_TYPE_TRIANGLE_MESH) {
        int numPrimitiveModes = IM_ARRAYSIZE(LINE_PRIMITIVE_MODE_DISPLAYNAMES);
        if (!hasBandsData) {
            numPrimitiveModes -= 5;
        }
        bool canChangeLinePrimitiveMode =
                lineRenderer->getRenderingMode() != RENDERING_MODE_OPACITY_OPTIMIZATION
                && lineRenderer->getRenderingMode() != RENDERING_MODE_DEFERRED_SHADING;
        if (canChangeLinePrimitiveMode && propertyEditor.addCombo(
                "Line Primitives", (int*)&linePrimitiveMode,
                LINE_PRIMITIVE_MODE_DISPLAYNAMES, numPrimitiveModes)) {
            if (updateLinePrimitiveMode(lineRenderer)) {
                shallReloadGatherShader = true;
            }
        }
    }

    bool isTriangleRepresentationUsed = lineRenderer && lineRenderer->getIsTriangleRepresentationUsed();
    if (isTriangleRepresentationUsed
            || (lineRenderer->getIsRasterizer() && lineRenderer->getRenderingMode() != RENDERING_MODE_OPACITY_OPTIMIZATION
                && (linePrimitiveMode == LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL
                    || linePrimitiveMode == LINE_PRIMITIVES_TUBE_GEOMETRY_SHADER
                    || linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
                    || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_PROGRAMMABLE_PULL
                    || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_GEOMETRY_SHADER
                    || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH
                    || getLinePrimitiveModeUsesMeshShader(linePrimitiveMode)))) {
        if (propertyEditor.addSliderInt("Tube Subdivisions", &tubeNumSubdivisions, 3, 8)) {
            if (lineRenderer->getIsRasterizer()) {
                shallReloadGatherShader = true;
            }
            if (getLinePrimitiveModeUsesMeshShader(linePrimitiveMode)) {
                dirty = true;
            }
            setTriangleRepresentationDirty();
        }
    }

    bool usesDeferredShading = lineRenderer && lineRenderer->getRenderingMode() == RENDERING_MODE_DEFERRED_SHADING;
    if (lineRenderer && (linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH
            || usesDeferredShading || !lineRenderer->isRasterizer)
            && (!usesDeferredShading || lineRenderer->getUsesTriangleMeshInternally())) {
        if (propertyEditor.addCheckbox("Capped Tubes", &useCappedTubes)) {
            triangleRepresentationDirty = true;
            if (!lineRenderer->isRasterizer || usesDeferredShading) {
                shallReloadGatherShader = true;
            }
        }
    }

    if (!simulationMeshOutlineTriangleIndices.empty()) {
        ImGui::EditMode editModeHullOpacity = propertyEditor.addSliderFloatEdit(
                "Hull Opacity", &hullOpacity, 0.0f, 1.0f, "%.4f");
        if (editModeHullOpacity != ImGui::EditMode::NO_CHANGE) {
            shallRenderSimulationMeshBoundary = hullOpacity > 0.0f;
            reRender = true;
        }
        if (lineRenderer && !lineRenderer->isRasterizer && editModeHullOpacity == ImGui::EditMode::INPUT_FINISHED) {
            lineRenderer->setRenderSimulationMeshHull(shallRenderSimulationMeshBoundary);
        }
        if (shallRenderSimulationMeshBoundary) {
            if (propertyEditor.addColorEdit3("Hull Color", &hullColor.r)) {
                reRender = true;
            }
        }
    }

    return shallReloadGatherShader;
}

bool LineData::renderGuiPropertyEditorNodesRendererAdvanced(
        sgl::PropertyEditor& propertyEditor, LineRenderer* lineRenderer) {
    bool shallReloadGatherShader = false;

    if (propertyEditor.addCheckbox("Use Halos", &useHalos)) {
        shallReloadGatherShader = true;
    }

    if (propertyEditor.addCheckbox("Use Shading", &useShading)) {
        shallReloadGatherShader = true;
    }

    propertyEditor.addCheckbox("Render Color Legend", &shallRenderColorLegendWidgets);

    return shallReloadGatherShader;
}

bool LineData::renderGuiRenderingSettingsPropertyEditor(sgl::PropertyEditor& propertyEditor) {
    if (getUseBandRendering()) {
        bool canUseLiveUpdate = getCanUseLiveUpdate(LineDataAccessType::TRIANGLE_MESH);
        if (LineRenderer::bandWidth != LineRenderer::displayBandWidthStaging) {
            LineRenderer::displayBandWidthStaging = LineRenderer::bandWidth;
            LineRenderer::displayBandWidth = LineRenderer::bandWidth;
            reRender = true;
            setTriangleRepresentationDirty();
        }
        ImGui::EditMode editMode = propertyEditor.addSliderFloatEdit(
                "Band Width", &LineRenderer::displayBandWidth,
                LineRenderer::MIN_BAND_WIDTH, LineRenderer::MAX_BAND_WIDTH, "%.4f");
        if ((canUseLiveUpdate && editMode != ImGui::EditMode::NO_CHANGE)
                || (!canUseLiveUpdate && editMode == ImGui::EditMode::INPUT_FINISHED)) {
            LineRenderer::bandWidth = LineRenderer::displayBandWidth;
            reRender = true;
            setTriangleRepresentationDirty();
        }
    }
    return false;
}

bool LineData::renderGuiWindowSecondary() {
    return false;
}

bool LineData::renderGuiOverlay() {
    bool shallReloadGatherShader = false;

    if (shallRenderColorLegendWidgets && !colorLegendWidgets.empty()) {
        colorLegendWidgets.at(selectedAttributeIndex).setAttributeMinValue(
                transferFunctionWindow.getSelectedRangeMin());
        colorLegendWidgets.at(selectedAttributeIndex).setAttributeMaxValue(
                transferFunctionWindow.getSelectedRangeMax());
        colorLegendWidgets.at(selectedAttributeIndex).renderGui();
    }

    return shallReloadGatherShader;
}

void LineData::setLineRenderers(const std::vector<LineRenderer*>& lineRenderers) {
    lineRenderersCached = lineRenderers;
}

void LineData::setFileDialogInstance(ImGuiFileDialog* _fileDialogInstance) {
    this->fileDialogInstance = _fileDialogInstance;
}

bool LineData::renderGuiPropertyEditorNodes(sgl::PropertyEditor& propertyEditor) {
    bool shallReloadGatherShader = false;

    // Switch importance criterion.
    if (propertyEditor.addCombo(
            "Attribute", (int*)&selectedAttributeIndexUi,
            attributeNames.data(), int(attributeNames.size()))) {
        setSelectedAttributeIndex(selectedAttributeIndexUi);
    }

    return shallReloadGatherShader;
}

void LineData::setClearColor(const sgl::Color& clearColor) {
    for (auto& colorLegendWidget : colorLegendWidgets) {
        colorLegendWidget.setClearColor(clearColor);
    }
}

void LineData::setSelectedAttributeIndex(int attributeIndex) {
    if (this->selectedAttributeIndex != attributeIndex) {
        dirty = true;
        this->selectedAttributeIndex = attributeIndex;
    }
    recomputeHistogram();
}

int LineData::getAttributeNameIndex(const std::string& attributeName) {
    auto it = std::find(attributeNames.begin(), attributeNames.end(), attributeName);
    if (it != attributeNames.end()) {
        return int(it - attributeNames.begin());
    } else {
        sgl::Logfile::get()->throwError(
                "Error in LineData::getAttributeNameIndex: Couldn't find attribute with name \""
                + attributeName + "\".");
        return -1;
    }
}

void LineData::onTransferFunctionMapRebuilt() {
    recomputeColorLegend();
}

void LineData::recomputeColorLegend() {
    for (auto& colorLegendWidget : colorLegendWidgets) {
        colorLegendWidget.setTransferFunctionColorMap(
                transferFunctionWindow.getTransferFunctionMap_sRGB());
        colorLegendWidget.setAttributeMinValue(
                transferFunctionWindow.getSelectedRangeMin());
        colorLegendWidget.setAttributeMinValue(
                transferFunctionWindow.getSelectedRangeMax());
    }
}

void LineData::rebuildInternalRepresentationIfNecessary() {
    if (dirty) {
        cachedRenderDataGeometryShader = {};
    }
    if (dirty || cachedTubeNumSubdivisions != tubeNumSubdivisions) {
        cachedRenderDataProgrammablePull = {};
        cachedRenderDataMeshShader = {};
    }
    if (dirty || triangleRepresentationDirty) {
        sgl::AppSettings::get()->getPrimaryDevice()->waitIdle();
        //updateMeshTriangleIntersectionDataStructure();

        cachedTubeTriangleRenderData = {};
        cachedTubeAabbRenderData = {};
        cachedHullTriangleRenderData = {};
        tubeTriangleBottomLevelASes = {};
        tubeAabbBottomLevelAS = {};
        hullTriangleBottomLevelAS = {};
        tubeTriangleTopLevelAS = {};
        tubeTriangleAndHullTopLevelAS = {};
        tubeAabbTopLevelAS = {};
        tubeAabbAndHullTopLevelAS = {};
        cachedTubeTriangleRenderDataPayload = {};

        dirty = false;
        triangleRepresentationDirty = false;
    }
}

void LineData::removeOtherCachedDataTypes(RequestMode requestMode) {
    if (requestMode != RequestMode::TRIANGLES) {
        cachedTubeTriangleRenderData = {};
        tubeTriangleBottomLevelASes = {};
        tubeTriangleTopLevelAS = {};
        tubeTriangleAndHullTopLevelAS = {};
        cachedTubeTriangleRenderDataPayload = {};
    }
    if (requestMode != RequestMode::AABBS) {
        cachedTubeAabbRenderData = {};
        tubeAabbBottomLevelAS = {};
        tubeAabbTopLevelAS = {};
        tubeAabbAndHullTopLevelAS = {};
    }
    if (requestMode != RequestMode::GEOMETRY_SHADER) {
        cachedRenderDataGeometryShader = {};
    }
    if (requestMode != RequestMode::PROGRAMMABLE_PULL) {
        cachedRenderDataProgrammablePull = {};
    }
    if (requestMode != RequestMode::MESH_SHADER) {
        cachedRenderDataMeshShader = {};
    }
}

std::vector<std::string> LineData::getShaderModuleNames() {
    sgl::vk::ShaderManager->invalidateShaderCache();
    if (linePrimitiveMode == LINE_PRIMITIVES_QUADS_PROGRAMMABLE_PULL) {
        return {
                "LinePassQuads.Programmable.Vertex",
                "LinePassQuads.Fragment"
        };
    } else if (linePrimitiveMode == LINE_PRIMITIVES_QUADS_GEOMETRY_SHADER) {
        return {
                "LinePassQuads.VBO.Vertex",
                "LinePassQuads.VBO.Geometry",
                "LinePassQuads.Fragment"
        };
    } else if (linePrimitiveMode == LINE_PRIMITIVES_RIBBON_QUADS_GEOMETRY_SHADER) {
        return {
                "LinePassRibbonQuads.VBO.Vertex",
                "LinePassRibbonQuads.VBO.Geometry",
                "LinePassRibbonQuads.Fragment"
        };
    } else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_PROGRAMMABLE_PULL) {
        return {
                "LinePassProgrammablePullTubes.Vertex",
                "LinePassGeometryShaderTubes.Fragment"
        };
    } else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_GEOMETRY_SHADER
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_GEOMETRY_SHADER) {
        return {
                "LinePassGeometryShaderTubes.VBO.Vertex",
                "LinePassGeometryShaderTubes.VBO.Geometry",
                "LinePassGeometryShaderTubes.Fragment"
        };
    } else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH) {
        return {
                "LinePassTriangleTubes.Vertex",
                "LinePassGeometryShaderTubes.Fragment"
        };
    }
#ifdef VK_EXT_mesh_shader
    else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_MESH_SHADER
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_MESH_SHADER) {
        return {
                "LinePassMeshShaderTubes.MeshEXT",
                "LinePassGeometryShaderTubes.Fragment"
        };
    }
#endif
    else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_MESH_SHADER_NV
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_MESH_SHADER_NV) {
        return {
                "LinePassMeshShaderTubes.MeshNV",
                "LinePassGeometryShaderTubes.Fragment"
        };
    } else {
        sgl::Logfile::get()->writeError("Error in LineData::getShaderModuleNames: Invalid line primitive mode.");
        return {};
    }
}

void LineData::setGraphicsPipelineInfo(
        sgl::vk::GraphicsPipelineInfo& pipelineInfo, const sgl::vk::ShaderStagesPtr& shaderStages) {
    if (linePrimitiveMode == LINE_PRIMITIVES_QUADS_PROGRAMMABLE_PULL
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_PROGRAMMABLE_PULL
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH) {
        pipelineInfo.setInputAssemblyTopology(sgl::vk::PrimitiveTopology::TRIANGLE_LIST);
    } else {
        pipelineInfo.setInputAssemblyTopology(sgl::vk::PrimitiveTopology::LINE_LIST);
    }

    if (getLinePrimitiveModeUsesSingleVertexShaderInputs(linePrimitiveMode)) {
        pipelineInfo.setVertexBufferBindingByLocationIndex("vertexPosition", sizeof(glm::vec3));
        pipelineInfo.setVertexBufferBindingByLocationIndex("vertexAttribute", sizeof(float));
        pipelineInfo.setVertexBufferBindingByLocationIndexOptional("vertexNormal", sizeof(glm::vec3));
        pipelineInfo.setVertexBufferBindingByLocationIndex("vertexTangent", sizeof(glm::vec3));
    }
}

void LineData::setRasterDataBindings(sgl::vk::RasterDataPtr& rasterData) {
    setVulkanRenderDataDescriptors(rasterData);

    if (linePrimitiveMode == LINE_PRIMITIVES_QUADS_PROGRAMMABLE_PULL) {
        LinePassQuadsRenderDataProgrammablePull tubeRenderData = this->getLinePassQuadsRenderDataProgrammablePull();
        if (!tubeRenderData.indexBuffer) {
            return;
        }
        rasterData->setIndexBuffer(tubeRenderData.indexBuffer);
        rasterData->setStaticBuffer(tubeRenderData.linePointsBuffer, "LinePoints");
    } else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_PROGRAMMABLE_PULL) {
        LinePassTubeRenderDataProgrammablePull tubeRenderData = this->getLinePassTubeRenderDataProgrammablePull();
        if (!tubeRenderData.indexBuffer) {
            return;
        }
        rasterData->setIndexBuffer(tubeRenderData.indexBuffer);
        rasterData->setStaticBuffer(tubeRenderData.linePointDataBuffer, "LinePointDataBuffer");
        if (tubeRenderData.multiVarAttributeDataBuffer) {
            rasterData->setStaticBuffer(
                    tubeRenderData.multiVarAttributeDataBuffer, "AttributeDataArrayBuffer");
        }
    } else if (getLinePrimitiveModeUsesMeshShader(linePrimitiveMode)) {
        LinePassTubeRenderDataMeshShader tubeRenderData = this->getLinePassTubeRenderDataMeshShader();
        if (!tubeRenderData.meshletDataBuffer) {
            return;
        }
        rasterData->setStaticBuffer(tubeRenderData.meshletDataBuffer, "MeshletDataBuffer");
        rasterData->setStaticBuffer(tubeRenderData.linePointDataBuffer, "LinePointDataBuffer");
#ifdef VK_EXT_mesh_shader
        if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_MESH_SHADER
                || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_MESH_SHADER) {
            rasterData->setMeshTasksGroupCountEXT(tubeRenderData.numMeshlets, 1, 1);
        } else {
#endif
            rasterData->setMeshTasksNV(tubeRenderData.numMeshlets, 0);
#ifdef VK_EXT_mesh_shader
        }
#endif
        if (tubeRenderData.multiVarAttributeDataBuffer) {
            rasterData->setStaticBuffer(
                    tubeRenderData.multiVarAttributeDataBuffer, "AttributeDataArrayBuffer");
        }
    } else if (linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
               || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH) {
        TubeTriangleRenderData tubeRenderData = this->getLinePassTubeTriangleMeshRenderData(
                true, false);
        if (!tubeRenderData.indexBuffer) {
            return;
        }
        rasterData->setIndexBuffer(tubeRenderData.indexBuffer);
        rasterData->setStaticBuffer(tubeRenderData.vertexBuffer, "TubeTriangleVertexDataBuffer");
        rasterData->setStaticBuffer(tubeRenderData.linePointDataBuffer, "LinePointDataBuffer");
        if (tubeRenderData.multiVarAttributeDataBuffer) {
            rasterData->setStaticBuffer(
                    tubeRenderData.multiVarAttributeDataBuffer, "AttributeDataArrayBuffer");
        }
    } else {
        LinePassTubeRenderData tubeRenderData = this->getLinePassTubeRenderData();
        if (!tubeRenderData.indexBuffer) {
            return;
        }
        rasterData->setIndexBuffer(tubeRenderData.indexBuffer);
        rasterData->setVertexBuffer(tubeRenderData.vertexPositionBuffer, "vertexPosition");
        rasterData->setVertexBuffer(tubeRenderData.vertexAttributeBuffer, "vertexAttribute");
        rasterData->setVertexBuffer(tubeRenderData.vertexNormalBuffer, "vertexNormal");
        rasterData->setVertexBuffer(tubeRenderData.vertexTangentBuffer, "vertexTangent");
        if (tubeRenderData.multiVarAttributeDataBuffer) {
            rasterData->setStaticBuffer(
                    tubeRenderData.multiVarAttributeDataBuffer, "AttributeDataArrayBuffer");
        }
    }
}

SimulationMeshOutlineRenderData LineData::getSimulationMeshOutlineRenderData() {
    SimulationMeshOutlineRenderData renderData;
    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();

    // Add the index buffer.
    renderData.indexBuffer = std::make_shared<sgl::vk::Buffer>(
            device, simulationMeshOutlineTriangleIndices.size() * sizeof(uint32_t),
            simulationMeshOutlineTriangleIndices.data(),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

    // Add the position buffer.
    renderData.vertexPositionBuffer = std::make_shared<sgl::vk::Buffer>(
            device, simulationMeshOutlineVertexPositions.size() * sizeof(glm::vec3),
            simulationMeshOutlineVertexPositions.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

    // Add the normal buffer.
    renderData.vertexNormalBuffer = std::make_shared<sgl::vk::Buffer>(
            device, simulationMeshOutlineVertexNormals.size() * sizeof(glm::vec3),
            simulationMeshOutlineVertexNormals.data(),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

    return renderData;
}

void LineData::loadSimulationMeshOutlineFromFile(
        const std::string& simulationMeshFilename, const sgl::AABB3& oldAABB, glm::mat4* transformationMatrixPtr) {
    loadMeshBoundarySurfaceFromFile(
            simulationMeshFilename, simulationMeshOutlineTriangleIndices, simulationMeshOutlineVertexPositions);
    normalizeVertexPositions(simulationMeshOutlineVertexPositions, oldAABB, transformationMatrixPtr);
    sgl::laplacianSmoothing(simulationMeshOutlineTriangleIndices, simulationMeshOutlineVertexPositions);
    sgl::computeSmoothTriangleNormals(
            simulationMeshOutlineTriangleIndices, simulationMeshOutlineVertexPositions,
            simulationMeshOutlineVertexNormals);
}

std::vector<sgl::vk::BottomLevelAccelerationStructurePtr> LineData::getTubeTriangleBottomLevelAS() {
    rebuildInternalRepresentationIfNecessary();
    if (!tubeTriangleBottomLevelASes.empty()) {
        return tubeTriangleBottomLevelASes;
    }

    /*
     * On NVIDIA hardware, we noticed that a 151MiB base data size, or 1922MiB triangle vertices and 963MiB triangle
     * indices object, was too large and sometimes caused timeout detection and recovery (TDR) in the graphics driver.
     * Thus, everything with more than approximately 256MiB of triangle vertices is split into multiple acceleration
     * structures.
     */
    bool needsSplit = getBaseSizeInBytes() > batchSizeLimit;
    //|| tubeTriangleRenderData.vertexBuffer->getSizeInBytes() > (1024 * 1024 * 256);
    generateSplitTriangleData = true;//needsSplit;

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    TubeTriangleRenderData tubeTriangleRenderData = getLinePassTubeTriangleMeshRenderData(
            false, true);
    generateSplitTriangleData = false;

    if (!tubeTriangleRenderData.indexBuffer) {
        return tubeTriangleBottomLevelASes;
    }

    sgl::Logfile::get()->writeInfo("Building tube triangle bottom level ray tracing acceleration structure...");
    size_t inputVerticesSize = tubeTriangleRenderData.indexBuffer->getSizeInBytes();
    size_t inputIndicesSize =
            tubeTriangleRenderData.vertexBuffer->getSizeInBytes() / sizeof(TubeTriangleVertexData) * sizeof(glm::vec3);
    sgl::Logfile::get()->writeInfo(
            "Input vertices size: " + sgl::toString(double(inputVerticesSize) / 1024.0 / 1024.0) + "MiB");
    sgl::Logfile::get()->writeInfo(
            "Input indices size: " + sgl::toString(double(inputIndicesSize) / 1024.0 / 1024.0) + "MiB");

    VkBuildAccelerationStructureFlagsKHR flags =
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
            | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

    if (needsSplit && !tubeTriangleSplitData.numBatchIndices.empty()) {
        std::vector<sgl::vk::BottomLevelAccelerationStructureInputPtr> blasInputs;
        blasInputs.reserve(tubeTriangleSplitData.numBatchIndices.size());
        uint32_t batchIndexBufferOffset = 0;
        for (const uint32_t& batchNumIndices : tubeTriangleSplitData.numBatchIndices) {
            auto asTubeInput = new sgl::vk::TrianglesAccelerationStructureInput(
                    device, VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR);
            asTubeInput->setIndexBufferOffset(
                    tubeTriangleRenderData.indexBuffer,
                    batchIndexBufferOffset * sizeof(uint32_t), batchNumIndices);
            asTubeInput->setVertexBuffer(
                    tubeTriangleRenderData.vertexBuffer, VK_FORMAT_R32G32B32_SFLOAT,
                    sizeof(TubeTriangleVertexData));
            auto asTubeInputPtr = sgl::vk::BottomLevelAccelerationStructureInputPtr(asTubeInput);
            blasInputs.push_back(asTubeInputPtr);
            batchIndexBufferOffset += uint32_t(batchNumIndices);
        }
        tubeTriangleBottomLevelASes = sgl::vk::buildBottomLevelAccelerationStructuresFromInputListBatched(
                blasInputs, flags, true);
    } else {
        auto asTubeInput = new sgl::vk::TrianglesAccelerationStructureInput(
                device, VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR);
        asTubeInput->setIndexBuffer(tubeTriangleRenderData.indexBuffer);
        asTubeInput->setVertexBuffer(
                tubeTriangleRenderData.vertexBuffer, VK_FORMAT_R32G32B32_SFLOAT,
                sizeof(TubeTriangleVertexData));
        auto asTubeInputPtr = sgl::vk::BottomLevelAccelerationStructureInputPtr(asTubeInput);
        tubeTriangleBottomLevelASes = sgl::vk::buildBottomLevelAccelerationStructuresFromInputList(
                { asTubeInputPtr }, flags, true);
    }

    return tubeTriangleBottomLevelASes;
}

void LineData::splitTriangleIndices(
        std::vector<uint32_t>& tubeTriangleIndices,
        const std::vector<TubeTriangleVertexData> &tubeTriangleVertexDataList) {
#ifdef USE_TBB

    sgl::AABB3 geometryAABB = tbb::parallel_reduce(
            tbb::blocked_range<size_t>(0, tubeTriangleVertexDataList.size()), sgl::AABB3(),
            [&tubeTriangleVertexDataList](tbb::blocked_range<size_t> const& r, sgl::AABB3 init) {
                for (auto i = r.begin(); i != r.end(); i++) {
                    const glm::vec3& pt = tubeTriangleVertexDataList.at(i).vertexPosition;
                    init.min.x = std::min(init.min.x, pt.x);
                    init.min.y = std::min(init.min.y, pt.y);
                    init.min.z = std::min(init.min.z, pt.z);
                    init.max.x = std::max(init.max.x, pt.x);
                    init.max.y = std::max(init.max.y, pt.y);
                    init.max.z = std::max(init.max.z, pt.z);
                }
                return init;
            },
            [&](sgl::AABB3 lhs, sgl::AABB3 rhs) -> sgl::AABB3 {
                lhs.combine(rhs);
                return lhs;
            });

#else

    sgl::AABB3 geometryAABB;
    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX, maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
#if _OPENMP >= 201107
    #pragma omp parallel for shared(tubeTriangleVertexDataList) default(none) reduction(min: minX) \
    reduction(min: minY) reduction(min: minZ) reduction(max: maxX) reduction(max: maxY) reduction(max: maxZ)
#endif
    for (size_t i = 0; i < tubeTriangleVertexDataList.size(); i++) {
        const glm::vec3& pt = tubeTriangleVertexDataList.at(i).vertexPosition;
        minX = std::min(minX, pt.x);
        minY = std::min(minY, pt.y);
        minZ = std::min(minZ, pt.z);
        maxX = std::max(maxX, pt.x);
        maxY = std::max(maxY, pt.y);
        maxZ = std::max(maxZ, pt.z);
    }
    geometryAABB.min = glm::vec3(minX, minY, minZ);
    geometryAABB.max = glm::vec3(maxX, maxY, maxZ);

#endif

    // Assume here that in all subdivisions we have the same amount of data.
    size_t numIndicesPerBatch = batchSizeLimit;

    int numBatches = sgl::nextPowerOfTwo(int(tubeTriangleIndices.size() / numIndicesPerBatch));
    numBatches = std::max(numBatches, 1);
    auto numSubdivisions = sgl::intlog2(numBatches);
    std::vector<std::vector<uint32_t>> batchIndicesList;
    batchIndicesList.resize(numBatches);

    tubeTriangleSplitData = {};
    for (uint32_t triangleIdx = 0; triangleIdx < tubeTriangleIndices.size(); triangleIdx += 3) {
        uint32_t idx0 = tubeTriangleIndices.at(triangleIdx);
        uint32_t idx1 = tubeTriangleIndices.at(triangleIdx + 1);
        uint32_t idx2 = tubeTriangleIndices.at(triangleIdx + 2);
        glm::vec3 p0 = tubeTriangleVertexDataList.at(idx0).vertexPosition;
        glm::vec3 p1 = tubeTriangleVertexDataList.at(idx1).vertexPosition;
        glm::vec3 p2 = tubeTriangleVertexDataList.at(idx2).vertexPosition;
        glm::vec3 triangleCentroid = (p0 + p1 + p2) / 3.0f;

        sgl::AABB3 regionAABB = geometryAABB;
        //const int k = 3; // Number of dimensions
        int batchIdx = 0;
        for (int depth = 0; depth < numSubdivisions; depth++) {
            // Assign axis depending on the largest axis of extent of regionAABB.
            //int axis = depth % k;
            glm::vec3 dimensions = regionAABB.getDimensions();
            int axis;
            if (dimensions.x > dimensions.y && dimensions.x > dimensions.z) {
                axis = 0;
            } else if (dimensions.y > dimensions.z) {
                axis = 1;
            } else {
                axis = 2;
            }
            float splitPosition = (regionAABB.min[axis] + regionAABB.max[axis]) / 2.0f;
            if (triangleCentroid[axis] <= splitPosition) {
                regionAABB.max[axis] = splitPosition;
            } else {
                regionAABB.min[axis] = splitPosition;
                batchIdx += 1 << (numSubdivisions - depth - 1);
            }
        }
        std::vector<uint32_t>& batchIndices = batchIndicesList.at(batchIdx);
        batchIndices.push_back(idx0);
        batchIndices.push_back(idx1);
        batchIndices.push_back(idx2);
    }

    tubeTriangleIndices.clear();
    for (std::vector<uint32_t>& batchIndices : batchIndicesList) {
        tubeTriangleSplitData.numBatchIndices.push_back(uint32_t(batchIndices.size()));
        tubeTriangleIndices.insert(tubeTriangleIndices.end(), batchIndices.begin(), batchIndices.end());
    }
}

sgl::vk::BottomLevelAccelerationStructurePtr LineData::getTubeAabbBottomLevelAS(bool ellipticTubes) {
    rebuildInternalRepresentationIfNecessary();
    if (tubeAabbBottomLevelAS && tubeAabbEllipticTubes == ellipticTubes) {
        return tubeAabbBottomLevelAS;
    }
    tubeAabbBottomLevelAS = {};

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    TubeAabbRenderData tubeAabbRenderData = getLinePassTubeAabbRenderData(false, ellipticTubes);

    if (!tubeAabbRenderData.indexBuffer) {
        return tubeAabbBottomLevelAS;
    }

    auto asAabbInput = new sgl::vk::AabbsAccelerationStructureInput(
            device, VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR);
    asAabbInput->setAabbsBuffer(tubeAabbRenderData.aabbBuffer);
    auto asAabbInputPtr = sgl::vk::BottomLevelAccelerationStructureInputPtr(asAabbInput);
    sgl::Logfile::get()->writeInfo("Building tube AABB bottom level ray tracing acceleration structure...");
    size_t inputSize = tubeAabbRenderData.aabbBuffer->getSizeInBytes();
    sgl::Logfile::get()->writeInfo(
            "Input AABBs size: " + sgl::toString(double(inputSize) / 1024.0 / 1024.0) + "MiB");
    tubeAabbBottomLevelAS = buildBottomLevelAccelerationStructureFromInput(
            asAabbInputPtr,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
            | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR, true);

    return tubeAabbBottomLevelAS;
}

sgl::vk::BottomLevelAccelerationStructurePtr LineData::getHullTriangleBottomLevelAS() {
    rebuildInternalRepresentationIfNecessary();
    if (hullTriangleBottomLevelAS) {
        return hullTriangleBottomLevelAS;
    }

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    HullTriangleRenderData hullTriangleRenderData = getVulkanHullTriangleRenderData(true);

    if (!hullTriangleRenderData.indexBuffer) {
        return hullTriangleBottomLevelAS;
    }

    auto asHullInput = new sgl::vk::TrianglesAccelerationStructureInput(
            device, VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR);
    asHullInput->setIndexBuffer(hullTriangleRenderData.indexBuffer);
    asHullInput->setVertexBuffer(
            hullTriangleRenderData.vertexBuffer, VK_FORMAT_R32G32B32_SFLOAT,
            sizeof(HullTriangleVertexData));
    auto asHullInputPtr = sgl::vk::BottomLevelAccelerationStructureInputPtr(asHullInput);

    sgl::Logfile::get()->writeInfo("Building hull triangle bottom level ray tracing acceleration structure...");
    size_t inputVerticesSize = hullTriangleRenderData.indexBuffer->getSizeInBytes();
    size_t inputIndicesSize =
            hullTriangleRenderData.vertexBuffer->getSizeInBytes() / sizeof(HullTriangleVertexData) * sizeof(glm::vec3);
    sgl::Logfile::get()->writeInfo(
            "Input vertices size: " + sgl::toString(double(inputVerticesSize) / 1024.0 / 1024.0) + "MiB");
    sgl::Logfile::get()->writeInfo(
            "Input indices size: " + sgl::toString(double(inputIndicesSize) / 1024.0 / 1024.0) + "MiB");
    hullTriangleBottomLevelAS = buildBottomLevelAccelerationStructureFromInput(
            asHullInputPtr,
            VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
            | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR, true);

    return hullTriangleBottomLevelAS;
}

sgl::vk::TopLevelAccelerationStructurePtr LineData::getRayTracingTubeTriangleTopLevelAS() {
    rebuildInternalRepresentationIfNecessary();
    if (tubeTriangleTopLevelAS) {
        return tubeTriangleTopLevelAS;
    }

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    tubeTriangleBottomLevelASes = getTubeTriangleBottomLevelAS();

    if (tubeTriangleBottomLevelASes.empty()) {
        return tubeTriangleTopLevelAS;
    }

    std::vector<sgl::vk::BlasInstance> blasInstances;
    for (size_t i = 0; i < tubeTriangleBottomLevelASes.size(); i++) {
        sgl::vk::BlasInstance tubeBlasInstance;
        tubeBlasInstance.blasIdx = uint32_t(i);
        tubeBlasInstance.instanceCustomIndex = size_t(i);
        blasInstances.push_back(tubeBlasInstance);
    }

    tubeTriangleTopLevelAS = std::make_shared<sgl::vk::TopLevelAccelerationStructure>(device);
    tubeTriangleTopLevelAS->build(tubeTriangleBottomLevelASes, blasInstances);

    return tubeTriangleTopLevelAS;
}

sgl::vk::TopLevelAccelerationStructurePtr LineData::getRayTracingTubeTriangleAndHullTopLevelAS() {
    rebuildInternalRepresentationIfNecessary();
    if (tubeTriangleAndHullTopLevelAS) {
        return tubeTriangleAndHullTopLevelAS;
    }
    if (simulationMeshOutlineTriangleIndices.empty()) {
        return getRayTracingTubeTriangleTopLevelAS();
    }

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    tubeTriangleBottomLevelASes = getTubeTriangleBottomLevelAS();
    hullTriangleBottomLevelAS = getHullTriangleBottomLevelAS();

    if (tubeTriangleBottomLevelASes.empty() && !hullTriangleBottomLevelAS) {
        return tubeTriangleAndHullTopLevelAS;
    }

    sgl::vk::BlasInstance hullBlasInstance;
    hullBlasInstance.shaderBindingTableRecordOffset = 1;
    tubeTriangleAndHullTopLevelAS = std::make_shared<sgl::vk::TopLevelAccelerationStructure>(device);
    if (!tubeTriangleBottomLevelASes.empty()) {
        hullBlasInstance.blasIdx = uint32_t(tubeTriangleBottomLevelASes.size());
        hullBlasInstance.instanceCustomIndex = uint32_t(tubeTriangleBottomLevelASes.size());
        std::vector<sgl::vk::BottomLevelAccelerationStructurePtr> blases = tubeTriangleBottomLevelASes;
        blases.push_back(hullTriangleBottomLevelAS);

        std::vector<sgl::vk::BlasInstance> blasInstances;
        for (size_t i = 0; i < tubeTriangleBottomLevelASes.size(); i++) {
            sgl::vk::BlasInstance tubeBlasInstance;
            tubeBlasInstance.blasIdx = uint32_t(i);
            tubeBlasInstance.instanceCustomIndex = size_t(i);
            blasInstances.push_back(tubeBlasInstance);
        }
        blasInstances.push_back(hullBlasInstance);

        tubeTriangleAndHullTopLevelAS->build(blases, blasInstances);
    } else {
        hullBlasInstance.blasIdx = 0;
        tubeTriangleAndHullTopLevelAS->build({ hullTriangleBottomLevelAS }, { hullBlasInstance });
    }

    return tubeTriangleAndHullTopLevelAS;
}

sgl::vk::TopLevelAccelerationStructurePtr LineData::getRayTracingTubeAabbTopLevelAS(bool ellipticTubes) {
    rebuildInternalRepresentationIfNecessary();
    if (tubeAabbTopLevelAS && tubeAabbEllipticTubes == ellipticTubes) {
        return tubeAabbTopLevelAS;
    }
    tubeAabbTopLevelAS = {};

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    tubeAabbBottomLevelAS = getTubeAabbBottomLevelAS(ellipticTubes);

    if (!tubeAabbBottomLevelAS) {
        return tubeAabbTopLevelAS;
    }

    tubeAabbTopLevelAS = std::make_shared<sgl::vk::TopLevelAccelerationStructure>(device);
    tubeAabbTopLevelAS->build({ tubeAabbBottomLevelAS }, { sgl::vk::BlasInstance() });

    return tubeAabbTopLevelAS;
}

sgl::vk::TopLevelAccelerationStructurePtr LineData::getRayTracingTubeAabbAndHullTopLevelAS(bool ellipticTubes) {
    rebuildInternalRepresentationIfNecessary();
    if (tubeAabbAndHullTopLevelAS && tubeAabbEllipticTubes == ellipticTubes) {
        return tubeAabbAndHullTopLevelAS;
    }
    tubeAabbAndHullTopLevelAS = {};
    if (simulationMeshOutlineTriangleIndices.empty()) {
        return getRayTracingTubeAabbTopLevelAS(ellipticTubes);
    }

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    tubeAabbBottomLevelAS = getTubeAabbBottomLevelAS(ellipticTubes);
    hullTriangleBottomLevelAS = getHullTriangleBottomLevelAS();

    if (!tubeAabbBottomLevelAS && !hullTriangleBottomLevelAS) {
        return tubeAabbAndHullTopLevelAS;
    }

    sgl::vk::BlasInstance tubeBlasInstance, hullBlasInstance;
    hullBlasInstance.shaderBindingTableRecordOffset = 1;
    tubeAabbAndHullTopLevelAS = std::make_shared<sgl::vk::TopLevelAccelerationStructure>(device);
    if (tubeAabbBottomLevelAS) {
        hullBlasInstance.blasIdx = 1;
        tubeAabbAndHullTopLevelAS->build(
                { tubeAabbBottomLevelAS, hullTriangleBottomLevelAS },
                { tubeBlasInstance, hullBlasInstance });
    } else {
        hullBlasInstance.blasIdx = 0;
        tubeAabbAndHullTopLevelAS->build({ hullTriangleBottomLevelAS }, { hullBlasInstance });
    }

    return tubeAabbAndHullTopLevelAS;
}

HullTriangleRenderData LineData::getVulkanHullTriangleRenderData(bool vulkanRayTracing) {
    rebuildInternalRepresentationIfNecessary();
    if (cachedHullTriangleRenderData.vertexBuffer) {
        return cachedHullTriangleRenderData;
    }
    if (simulationMeshOutlineTriangleIndices.empty()) {
        return {};
    }

    sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
    cachedHullTriangleRenderData = {};

    std::vector<HullTriangleVertexData> vertexDataList;
    vertexDataList.reserve(simulationMeshOutlineVertexPositions.size());
    for (size_t i = 0; i < simulationMeshOutlineVertexPositions.size(); i++) {
        HullTriangleVertexData vertex{};
        vertex.vertexPosition = simulationMeshOutlineVertexPositions.at(i);
        vertex.vertexNormal = simulationMeshOutlineVertexNormals.at(i);
        vertexDataList.push_back(vertex);
    }

    uint32_t indexBufferFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    uint32_t vertexBufferFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vulkanRayTracing) {
        indexBufferFlags |=
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        vertexBufferFlags |=
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    cachedHullTriangleRenderData.indexBuffer = std::make_shared<sgl::vk::Buffer>(
            device, simulationMeshOutlineTriangleIndices.size() * sizeof(uint32_t),
            simulationMeshOutlineTriangleIndices.data(),
            indexBufferFlags, VMA_MEMORY_USAGE_GPU_ONLY);

    cachedHullTriangleRenderData.vertexBuffer = std::make_shared<sgl::vk::Buffer>(
            device, vertexDataList.size() * sizeof(HullTriangleVertexData), vertexDataList.data(),
            vertexBufferFlags, VMA_MEMORY_USAGE_GPU_ONLY);

    return cachedHullTriangleRenderData;
}

void LineData::getVulkanShaderPreprocessorDefines(
        std::map<std::string, std::string>& preprocessorDefines, bool isRasterizer) {
    if (linePrimitiveMode == LINE_PRIMITIVES_RIBBON_QUADS_GEOMETRY_SHADER
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_GEOMETRY_SHADER
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_GEOMETRY_SHADER
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_PROGRAMMABLE_PULL
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_PROGRAMMABLE_PULL
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH
            || getLinePrimitiveModeUsesMeshShader(linePrimitiveMode)) {
        preprocessorDefines.insert(std::make_pair(
                "NUM_TUBE_SUBDIVISIONS", std::to_string(tubeNumSubdivisions)));
    }
    if (linePrimitiveMode == LINE_PRIMITIVES_QUADS_GEOMETRY_SHADER
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_GEOMETRY_SHADER
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_GEOMETRY_SHADER) {
        preprocessorDefines.insert(std::make_pair("USE_GEOMETRY_SHADER", ""));
    }
    if (getLinePrimitiveModeUsesMeshShader(linePrimitiveMode)) {
        sgl::vk::Device* device = sgl::AppSettings::get()->getPrimaryDevice();
        uint32_t workgroupSize = device->getPhysicalDeviceMeshShaderPropertiesNV().maxMeshWorkGroupSize[0];
        preprocessorDefines.insert(std::make_pair("WORKGROUP_SIZE", std::to_string(workgroupSize)));
    }
    bool usesDeferredShading = std::any_of(
            lineRenderersCached.cbegin(), lineRenderersCached.cend(), [](LineRenderer* lineRenderer){
                return lineRenderer->getRenderingMode() == RENDERING_MODE_DEFERRED_SHADING;
            });
    bool usesTriangleMeshInternally = std::any_of(
            lineRenderersCached.cbegin(), lineRenderersCached.cend(), [](LineRenderer* lineRenderer){
                return lineRenderer->getUsesTriangleMeshInternally();
            });
    if (useCappedTubes && (usesDeferredShading || linePrimitiveMode == LINE_PRIMITIVES_TUBE_TRIANGLE_MESH
            || linePrimitiveMode == LINE_PRIMITIVES_TUBE_RIBBONS_TRIANGLE_MESH || !isRasterizer)
            && (!usesDeferredShading || usesTriangleMeshInternally)) {
        preprocessorDefines.insert(std::make_pair("USE_CAPPED_TUBES", ""));
    }
    if (renderThickBands) {
        preprocessorDefines.insert(std::make_pair("MIN_THICKNESS", std::to_string(minBandThickness)));
    } else {
        preprocessorDefines.insert(std::make_pair("MIN_THICKNESS", std::to_string(1e-2f)));
    }
    if (useHalos) {
        preprocessorDefines.insert(std::make_pair("USE_HALOS", ""));
    }
    if (!useShading) {
        preprocessorDefines.insert(std::make_pair("DISABLE_SHADING", ""));
    }
}

void LineData::setVulkanRenderDataDescriptors(const sgl::vk::RenderDataPtr& renderData) {
    renderData->setStaticBufferOptional(lineUniformDataBuffer, "LineUniformDataBuffer");

    if (renderData->getShaderStages()->hasDescriptorBinding(0, "transferFunctionTexture")) {
        const sgl::vk::DescriptorInfo& descriptorInfo = renderData->getShaderStages()->getDescriptorInfoByName(
                0, "transferFunctionTexture");
        if (descriptorInfo.image.arrayed == 0) {
            renderData->setStaticTexture(
                    transferFunctionWindow.getTransferFunctionMapTextureVulkan(),
                    "transferFunctionTexture");
            renderData->setStaticBuffer(
                    transferFunctionWindow.getMinMaxUboVulkan(),
                    "MinMaxUniformBuffer");
        }
    }
}

void LineData::updateVulkanUniformBuffers(LineRenderer* lineRenderer, sgl::vk::Renderer* renderer) {
    SceneData* sceneData = nullptr;
    if (lineRenderer) {
        sceneData = lineRenderer->getSceneData();
    }

    if (sceneData) {
        glm::vec4 backgroundColor = sceneData->clearColor->getFloatColorRGBA();
        glm::vec4 foregroundColor = glm::vec4(1.0f) - backgroundColor;
        lineUniformData.cameraPosition = sceneData->camera->getPosition();
        lineUniformData.fieldOfViewY = sceneData->camera->getFOVy();
        lineUniformData.viewMatrix = sceneData->camera->getViewMatrix();
        lineUniformData.projectionMatrix = sceneData->camera->getProjectionMatrix();
        lineUniformData.inverseViewMatrix = glm::inverse(sceneData->camera->getViewMatrix());
        lineUniformData.inverseProjectionMatrix = glm::inverse(sceneData->camera->getProjectionMatrix());
        lineUniformData.backgroundColor = backgroundColor;
        lineUniformData.foregroundColor = foregroundColor;
    }

    lineUniformData.lineWidth = LineRenderer::getLineWidth();
    lineUniformData.bandWidth = LineRenderer::getBandWidth();
    lineUniformData.minBandThickness = minBandThickness;
    lineUniformData.depthCueStrength = lineRenderer ? lineRenderer->depthCueStrength : 0.0f;
    lineUniformData.ambientOcclusionStrength = lineRenderer ? lineRenderer->ambientOcclusionStrength : 0.0f;
    lineUniformData.ambientOcclusionGamma = lineRenderer ? lineRenderer->ambientOcclusionGamma : 1.0f;
    if (lineRenderer && lineRenderer->useAmbientOcclusion && lineRenderer->ambientOcclusionBaker) {
        lineUniformData.numAoTubeSubdivisions = lineRenderer->ambientOcclusionBaker->getNumTubeSubdivisions();
        lineUniformData.numLineVertices = lineRenderer->ambientOcclusionBaker->getNumLineVertices();
        lineUniformData.numParametrizationVertices =
                lineRenderer->ambientOcclusionBaker->getNumParametrizationVertices();
    }

    lineUniformData.hasHullMesh = simulationMeshOutlineTriangleIndices.empty() ? 0 : 1;
    lineUniformData.hullColor = glm::vec4(hullColor.r, hullColor.g, hullColor.b, hullOpacity);
    lineUniformData.hullUseShading = uint32_t(hullUseShading);

    if (sceneData) {
        auto scalingFactor = uint32_t(lineRenderer->getResolutionIntegerScalingFactor());
        lineUniformData.viewportSize = glm::uvec2(
                *sceneData->viewportWidth * scalingFactor,
                *sceneData->viewportHeight * scalingFactor);
    }

    lineUniformDataBuffer->updateData(
            sizeof(LineUniformData), &lineUniformData, renderer->getVkCommandBuffer());
}
