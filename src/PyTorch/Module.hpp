/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2022, Christoph Neuhauser, Timm Knörle
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

#ifndef CLOUDRENDERING_MODULE_HPP
#define CLOUDRENDERING_MODULE_HPP

#include <torch/script.h>
#include <torch/types.h>

MODULE_OP_API void initialize();
MODULE_OP_API void cleanup();

MODULE_OP_API void loadCloudFile(const std::string& filename);
MODULE_OP_API void loadEmissionFile(const std::string& filename);

MODULE_OP_API void loadEnvironmentMap(const std::string& filename);
MODULE_OP_API void setEnvironmentMapIntensityFactor(double intensityFactor);

MODULE_OP_API void setScatteringAlbedo(std::vector<double> albedo);
MODULE_OP_API void setExtinctionScale(double extinctionScale);
MODULE_OP_API void setExtinctionBase(std::vector<double> extinctionBase);
MODULE_OP_API void setPhaseG(double phaseG);

MODULE_OP_API void setUseTransferFunction(bool _useTf);
MODULE_OP_API void loadTransferFunctionFile(const std::string& tfFilePath);

MODULE_OP_API std::vector<double> getCameraPosition();
MODULE_OP_API std::vector<double> getCameraViewMatrix();
MODULE_OP_API double getCameraFOVy();

MODULE_OP_API void setCameraPosition(std::vector<double> cameraPosition);
MODULE_OP_API void setCameraTarget(std::vector<double> cameraTarget);
MODULE_OP_API void overwriteCameraViewMatrix(std::vector<double> viewMatrixData);
MODULE_OP_API void setCameraFOVy(double FOVy);

MODULE_OP_API void setVPTMode(int64_t mode);
MODULE_OP_API void setVPTModeFromName(const std::string& modeName);
MODULE_OP_API void setFeatureMapType(int64_t type);

MODULE_OP_API void setUseIsosurfaces(bool _useIsosurfaces);
MODULE_OP_API void setIsoValue(double _isoValue);
MODULE_OP_API void setIsoSurfaceColor(std::vector<double> _isoSurfaceColor);
MODULE_OP_API void setIsosurfaceType(const std::string& _isosurfaceType);
MODULE_OP_API void setSurfaceBrdf(const std::string& _surfaceBrdf);

MODULE_OP_API void setSeedOffset(int64_t offset);

MODULE_OP_API void setViewProjectionMatrixAsPrevious();

MODULE_OP_API void setEmissionCap(double emissionCap);
MODULE_OP_API void setEmissionStrength(double emissionStrength);
MODULE_OP_API void setUseEmission(bool useEmission);
MODULE_OP_API void flipYZ(bool flip);

MODULE_OP_API std::vector<double> getRenderBoundingBox();
MODULE_OP_API void rememberNextBounds();
MODULE_OP_API void forgetCurrentBounds();

MODULE_OP_API torch::Tensor renderFrame(torch::Tensor inputTensor, int64_t frameCount);
MODULE_OP_API torch::Tensor getFeatureMap(torch::Tensor inputTensor, int64_t frameCount);

class VolumetricPathTracingModuleRenderer;
extern VolumetricPathTracingModuleRenderer* vptRenderer;

torch::Tensor renderFrameCpu(torch::Tensor inputTensor, int64_t frameCount);
torch::Tensor renderFrameVulkan(torch::Tensor inputTensor, int64_t frameCount);
#ifdef SUPPORT_CUDA_INTEROP
torch::Tensor renderFrameCuda(torch::Tensor inputTensor, int64_t frameCount);
#endif

#endif //CLOUDRENDERING_MODULE_HPP
