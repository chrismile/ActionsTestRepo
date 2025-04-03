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

#include <glm/glm.hpp>

#ifdef USE_TBB
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#endif

#include <Math/Math.hpp>
#include "VolumetricPathTracingPass.hpp"
#include "SuperVoxelGrid.hpp"

SuperVoxelGridResidualRatioTracking::SuperVoxelGridResidualRatioTracking(
        sgl::vk::Device* device, int voxelGridSizeX, int voxelGridSizeY, int voxelGridSizeZ,
        const float* voxelGridData, int superVoxelSize1D,
        bool clampToZeroBorder, GridInterpolationType gridInterpolationType)
        : voxelGridSizeX(voxelGridSizeX), voxelGridSizeY(voxelGridSizeY), voxelGridSizeZ(voxelGridSizeZ),
          clampToZeroBorder(clampToZeroBorder), interpolationType(gridInterpolationType) {
    superVoxelSize1D = std::max(superVoxelSize1D, 1);
    if (voxelGridSizeX < superVoxelSize1D || voxelGridSizeY < superVoxelSize1D || voxelGridSizeZ < superVoxelSize1D) {
        bool divisible;
        do {
            divisible =
                    voxelGridSizeX % superVoxelSize1D == 0
                    && voxelGridSizeY % superVoxelSize1D == 0
                    && voxelGridSizeZ % superVoxelSize1D == 0;
            if (!divisible) {
                superVoxelSize1D /= 2;
            }
        } while (!divisible);
    }

    superVoxelSize = glm::ivec3(superVoxelSize1D);

    superVoxelGridSizeX = sgl::iceil(voxelGridSizeX, superVoxelSize1D);
    superVoxelGridSizeY = sgl::iceil(voxelGridSizeY, superVoxelSize1D);
    superVoxelGridSizeZ = sgl::iceil(voxelGridSizeZ, superVoxelSize1D);
    int superVoxelGridSize = superVoxelGridSizeX * superVoxelGridSizeY * superVoxelGridSizeZ;

    superVoxelGrid = new SuperVoxelResidualRatioTracking[superVoxelGridSize];
    superVoxelGridOccupany = new uint8_t[superVoxelGridSize];
    superVoxelGridMinDensity = new float[superVoxelGridSize];
    superVoxelGridMaxDensity = new float[superVoxelGridSize];
    superVoxelGridAvgDensity = new float[superVoxelGridSize];

    sgl::vk::ImageSettings imageSettings{};
    sgl::vk::ImageSamplerSettings samplerSettings{};
    imageSettings.width = superVoxelGridSizeX;
    imageSettings.height = superVoxelGridSizeY;
    imageSettings.depth = superVoxelGridSizeZ;
    imageSettings.imageType = VK_IMAGE_TYPE_3D;
    imageSettings.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageSettings.format = VK_FORMAT_R32G32_SFLOAT;
    samplerSettings.addressModeU = samplerSettings.addressModeV = samplerSettings.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerSettings.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    superVoxelGridTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    imageSettings.format = VK_FORMAT_R8_UINT;
    samplerSettings.minFilter = samplerSettings.magFilter = VK_FILTER_NEAREST;
    samplerSettings.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    superVoxelGridOccupancyTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);

    computeSuperVoxels(voxelGridData);
}

SuperVoxelGridResidualRatioTracking::~SuperVoxelGridResidualRatioTracking() {
    delete[] superVoxelGrid;
    delete[] superVoxelGridOccupany;
    delete[] superVoxelGridMinDensity;
    delete[] superVoxelGridMaxDensity;
    delete[] superVoxelGridAvgDensity;
}

void SuperVoxelGridResidualRatioTracking::computeSuperVoxels(const float* voxelGridData) {
    int superVoxelGridSize = superVoxelGridSizeX * superVoxelGridSizeY * superVoxelGridSizeZ;

#ifdef USE_TBB
    tbb::parallel_for(tbb::blocked_range<int>(0, superVoxelGridSize), [&](auto const& r) {
        for (auto superVoxelIdx = r.begin(); superVoxelIdx != r.end(); superVoxelIdx++) {
#else
#if _OPENMP >= 200805
    #pragma omp parallel for firstprivate(superVoxelGridSize) default(none) \
    shared(voxelGridData, superVoxelGridMinDensity, superVoxelGridMaxDensity, superVoxelGridAvgDensity, clampToZeroBorder)
#endif
    for (int superVoxelIdx = 0; superVoxelIdx < superVoxelGridSize; superVoxelIdx++) {
#endif
        int superVoxelIdxX = superVoxelIdx % superVoxelGridSizeX;
        int superVoxelIdxY = (superVoxelIdx / superVoxelGridSizeX) % superVoxelGridSizeY;
        int superVoxelIdxZ = superVoxelIdx / (superVoxelGridSizeX * superVoxelGridSizeY);

        float densityMin = std::numeric_limits<float>::max();
        float densityMax = std::numeric_limits<float>::lowest();
        float densityAvg = 0.0f;
        int numValidVoxels = 0;

        if (interpolationType == GridInterpolationType::NEAREST) {
            for (int offsetZ = 0; offsetZ < superVoxelSize.z; offsetZ++) {
                for (int offsetY = 0; offsetY < superVoxelSize.y; offsetY++) {
                    for (int offsetX = 0; offsetX < superVoxelSize.x; offsetX++) {
                        int voxelIdxX = superVoxelIdxX * superVoxelSize.x + offsetX;
                        int voxelIdxY = superVoxelIdxY * superVoxelSize.y + offsetY;
                        int voxelIdxZ = superVoxelIdxZ * superVoxelSize.z + offsetZ;

                        float value;
                        if (voxelIdxX >= 0 && voxelIdxY >= 0 && voxelIdxZ >= 0
                                && voxelIdxX < voxelGridSizeX && voxelIdxY < voxelGridSizeY && voxelIdxZ < voxelGridSizeZ) {
                            int voxelIdx = voxelIdxX + (voxelIdxY + voxelIdxZ * voxelGridSizeY) * voxelGridSizeX;
                            value = voxelGridData[voxelIdx];
                        } else {
                            if (!clampToZeroBorder) {
                                continue;
                            }
                            /*
                             * If this is a boundary voxel: Per default, we use VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                             * with the border color VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK. Thus, we need to set the
                             * minimum to zero for boundary super voxels, as linear and stochastic blending can lead to
                             * smearing of boundary values into the domain.
                             */
                            value = 0.0f;
                        }

                        densityMin = std::min(densityMin, value);
                        densityMax = std::max(densityMax, value);
                        densityAvg += value;
                        numValidVoxels++;
                    }
                }
            }
        } else {
            for (int offsetZ = -1; offsetZ <= superVoxelSize.z; offsetZ++) {
                for (int offsetY = -1; offsetY <= superVoxelSize.y; offsetY++) {
                    for (int offsetX = -1; offsetX <= superVoxelSize.x; offsetX++) {
                        int voxelIdxX = superVoxelIdxX * superVoxelSize.x + offsetX;
                        int voxelIdxY = superVoxelIdxY * superVoxelSize.y + offsetY;
                        int voxelIdxZ = superVoxelIdxZ * superVoxelSize.z + offsetZ;

                        float value;
                        if (voxelIdxX >= 0 && voxelIdxY >= 0 && voxelIdxZ >= 0
                                && voxelIdxX < voxelGridSizeX && voxelIdxY < voxelGridSizeY && voxelIdxZ < voxelGridSizeZ) {
                            int voxelIdx = voxelIdxX + (voxelIdxY + voxelIdxZ * voxelGridSizeY) * voxelGridSizeX;
                            value = voxelGridData[voxelIdx];
                        } else {
                            if (!clampToZeroBorder) {
                                continue;
                            }
                            /*
                             * If this is a boundary voxel: Per default, we use VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                             * with the border color VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK. Thus, we need to set the
                             * minimum to zero for boundary super voxels, as linear and stochastic blending can lead to
                             * smearing of boundary values into the domain.
                             */
                            value = 0.0f;
                        }

                        densityMin = std::min(densityMin, value);
                        densityMax = std::max(densityMax, value);
                        densityAvg += value;
                        numValidVoxels++;
                    }
                }
            }
        }
        densityAvg /= float(numValidVoxels);

        /*
         * If this is a boundary voxel: Per default, we use VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER with the border
         * color VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK. Thus, we need to set the minimum to zero for boundary super
         * voxels if they reach outside the grid region.
         */
        if (clampToZeroBorder
            && ((voxelGridSizeX % superVoxelSize.x != 0 && superVoxelIdxX == superVoxelGridSizeX - 1)
                || (voxelGridSizeY % superVoxelSize.y != 0 && superVoxelIdxY == superVoxelGridSizeY - 1)
                || (voxelGridSizeZ % superVoxelSize.z != 0 && superVoxelIdxZ == superVoxelGridSizeZ - 1))) {
            densityMin = 0.0f;
        }

        superVoxelGridMinDensity[superVoxelIdx] = densityMin;
        superVoxelGridMaxDensity[superVoxelIdx] = densityMax;
        superVoxelGridAvgDensity[superVoxelIdx] = densityAvg;
    }
#ifdef USE_TBB
    });
#endif
}

void SuperVoxelGridResidualRatioTracking::setExtinction(float extinction) {
    this->extinction = extinction;
    recomputeSuperVoxels();
}

void SuperVoxelGridResidualRatioTracking::recomputeSuperVoxels() {
    int superVoxelGridSize = superVoxelGridSizeX * superVoxelGridSizeY * superVoxelGridSizeZ;

    const float gamma = 2.0f;
    const float D = std::sqrt(3.0f) * float(std::max(superVoxelSize.x, std::max(superVoxelSize.y, superVoxelSize.z)));

#ifdef USE_TBB
    tbb::parallel_for(tbb::blocked_range<int>(0, superVoxelGridSize), [&](auto const& r) {
        for (auto superVoxelIdx = r.begin(); superVoxelIdx != r.end(); superVoxelIdx++) {
#else
#if _OPENMP >= 200805
    #pragma omp parallel for firstprivate(superVoxelGridSize, D, gamma) default(none) \
    shared(superVoxelGridMinDensity, superVoxelGridMaxDensity, superVoxelGridAvgDensity)
#endif
    for (int superVoxelIdx = 0; superVoxelIdx < superVoxelGridSize; superVoxelIdx++) {
#endif
        float densityMin = superVoxelGridMinDensity[superVoxelIdx];
        float densityMax = superVoxelGridMaxDensity[superVoxelIdx];
        float densityAvg = superVoxelGridAvgDensity[superVoxelIdx];

        float mu_min = densityMin * extinction;
        float mu_max = densityMax * extinction;
        float mu_avg = densityAvg * extinction;
        //float mu_min = densityMin;
        //float mu_max = densityMax;
        //float mu_avg = densityAvg;

        // Sec. 5.1 in paper by Novák et al. [2014].
        float mu_r_bar = std::max(mu_max - mu_min, 0.1f);
        float mu_c = mu_min + mu_r_bar * std::pow(gamma, (1.0f / (D * mu_r_bar)) - 1.0f);
        float mu_c_prime = glm::clamp(mu_c, mu_min, mu_avg);

        SuperVoxelResidualRatioTracking& superVoxel = superVoxelGrid[superVoxelIdx];
        superVoxel.mu_c = mu_c_prime;
        superVoxel.mu_r_bar = mu_r_bar;

        bool isSuperVoxelEmpty = densityMax < 1e-5f;
        superVoxelGridOccupany[superVoxelIdx] = isSuperVoxelEmpty ? 0 : 1;
    }
#ifdef USE_TBB
    });
#endif

    superVoxelGridTexture->getImage()->uploadData(
            superVoxelGridSize * sizeof(SuperVoxelResidualRatioTracking), superVoxelGrid);
    superVoxelGridOccupancyTexture->getImage()->uploadData(
            superVoxelGridSize * sizeof(uint8_t), superVoxelGridOccupany);
}



SuperVoxelGridDecompositionTracking::SuperVoxelGridDecompositionTracking(
        sgl::vk::Device* device, int voxelGridSizeX, int voxelGridSizeY, int voxelGridSizeZ,
        const float* voxelGridData, int superVoxelSize1D,
        bool clampToZeroBorder, GridInterpolationType gridInterpolationType)
        : voxelGridSizeX(voxelGridSizeX), voxelGridSizeY(voxelGridSizeY), voxelGridSizeZ(voxelGridSizeZ),
          clampToZeroBorder(clampToZeroBorder), interpolationType(gridInterpolationType) {
    superVoxelSize1D = std::max(superVoxelSize1D, 1);
    if (voxelGridSizeX < superVoxelSize1D || voxelGridSizeY < superVoxelSize1D || voxelGridSizeZ < superVoxelSize1D) {
        bool divisible;
        do {
            divisible =
                    voxelGridSizeX % superVoxelSize1D == 0
                    && voxelGridSizeY % superVoxelSize1D == 0
                    && voxelGridSizeZ % superVoxelSize1D == 0;
            if (!divisible) {
                superVoxelSize1D /= 2;
            }
        } while (!divisible);
    }

    superVoxelSize = glm::ivec3(superVoxelSize1D);

    superVoxelGridSizeX = sgl::iceil(voxelGridSizeX, superVoxelSize1D);
    superVoxelGridSizeY = sgl::iceil(voxelGridSizeY, superVoxelSize1D);
    superVoxelGridSizeZ = sgl::iceil(voxelGridSizeZ, superVoxelSize1D);
    int superVoxelGridSize = superVoxelGridSizeX * superVoxelGridSizeY * superVoxelGridSizeZ;

    superVoxelGridOccupany = new uint8_t[superVoxelGridSize];
    superVoxelGridMinMaxDensity = new glm::vec2[superVoxelGridSize];

    sgl::vk::ImageSettings imageSettings{};
    sgl::vk::ImageSamplerSettings samplerSettings{};
    imageSettings.width = superVoxelGridSizeX;
    imageSettings.height = superVoxelGridSizeY;
    imageSettings.depth = superVoxelGridSizeZ;
    imageSettings.imageType = VK_IMAGE_TYPE_3D;
    imageSettings.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageSettings.format = VK_FORMAT_R32G32_SFLOAT;
    samplerSettings.addressModeU = samplerSettings.addressModeV = samplerSettings.addressModeW =
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerSettings.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    superVoxelGridTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);
    imageSettings.format = VK_FORMAT_R8_UINT;
    samplerSettings.minFilter = samplerSettings.magFilter = VK_FILTER_NEAREST;
    samplerSettings.borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
    superVoxelGridOccupancyTexture = std::make_shared<sgl::vk::Texture>(device, imageSettings, samplerSettings);

#ifdef USE_TBB
    tbb::parallel_for(tbb::blocked_range<int>(0, superVoxelGridSize), [&](auto const& r) {
        for (auto superVoxelIdx = r.begin(); superVoxelIdx != r.end(); superVoxelIdx++) {
#else
#if _OPENMP >= 200805
    #pragma omp parallel for firstprivate(superVoxelGridSize) default(none) \
    shared(voxelGridSizeX, voxelGridSizeY, voxelGridSizeZ, voxelGridData, superVoxelGridMinMaxDensity, clampToZeroBorder)
#endif
    for (int superVoxelIdx = 0; superVoxelIdx < superVoxelGridSize; superVoxelIdx++) {
#endif
        int superVoxelIdxX = superVoxelIdx % superVoxelGridSizeX;
        int superVoxelIdxY = (superVoxelIdx / superVoxelGridSizeX) % superVoxelGridSizeY;
        int superVoxelIdxZ = superVoxelIdx / (superVoxelGridSizeX * superVoxelGridSizeY);

        float densityMin = std::numeric_limits<float>::max();
        float densityMax = std::numeric_limits<float>::lowest();
        if (interpolationType == GridInterpolationType::NEAREST) {
            for (int offsetZ = 0; offsetZ < superVoxelSize.z; offsetZ++) {
                for (int offsetY = 0; offsetY < superVoxelSize.y; offsetY++) {
                    for (int offsetX = 0; offsetX < superVoxelSize.x; offsetX++) {
                        int voxelIdxX = superVoxelIdxX * superVoxelSize.x + offsetX;
                        int voxelIdxY = superVoxelIdxY * superVoxelSize.y + offsetY;
                        int voxelIdxZ = superVoxelIdxZ * superVoxelSize.z + offsetZ;
                        float value;
                        if (voxelIdxX >= 0 && voxelIdxY >= 0 && voxelIdxZ >= 0
                                && voxelIdxX < voxelGridSizeX && voxelIdxY < voxelGridSizeY && voxelIdxZ < voxelGridSizeZ) {
                            int voxelIdx = voxelIdxX + (voxelIdxY + voxelIdxZ * voxelGridSizeY) * voxelGridSizeX;
                            value = voxelGridData[voxelIdx];
                        } else {
                            if (!clampToZeroBorder) {
                                continue;
                            }
                            /*
                             * If this is a boundary voxel: Per default, we use VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                             * with the border color VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK. Thus, we need to set the
                             * minimum to zero for boundary super voxels, as linear and stochastic blending can lead to
                             * smearing of boundary values into the domain.
                             */
                            value = 0.0f;
                        }
                        densityMin = std::min(densityMin, value);
                        densityMax = std::max(densityMax, value);
                    }
                }
            }
        } else {
            for (int offsetZ = -1; offsetZ <= superVoxelSize.z; offsetZ++) {
                for (int offsetY = -1; offsetY <= superVoxelSize.y; offsetY++) {
                    for (int offsetX = -1; offsetX <= superVoxelSize.x; offsetX++) {
                        int voxelIdxX = superVoxelIdxX * superVoxelSize.x + offsetX;
                        int voxelIdxY = superVoxelIdxY * superVoxelSize.y + offsetY;
                        int voxelIdxZ = superVoxelIdxZ * superVoxelSize.z + offsetZ;
                        float value;
                        if (voxelIdxX >= 0 && voxelIdxY >= 0 && voxelIdxZ >= 0
                                && voxelIdxX < voxelGridSizeX && voxelIdxY < voxelGridSizeY && voxelIdxZ < voxelGridSizeZ) {
                            int voxelIdx = voxelIdxX + (voxelIdxY + voxelIdxZ * voxelGridSizeY) * voxelGridSizeX;
                            value = voxelGridData[voxelIdx];
                        } else {
                            if (!clampToZeroBorder) {
                                continue;
                            }
                            /*
                             * If this is a boundary voxel: Per default, we use VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER
                             * with the border color VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK. Thus, we need to set the
                             * minimum to zero for boundary super voxels, as linear and stochastic blending can lead to
                             * smearing of boundary values into the domain.
                             */
                            value = 0.0f;
                        }
                        densityMin = std::min(densityMin, value);
                        densityMax = std::max(densityMax, value);
                    }
                }
            }
        }

        superVoxelGridMinMaxDensity[superVoxelIdx] = glm::vec2(densityMin, densityMax);

        bool isSuperVoxelEmpty = densityMax < 1e-5f;
        superVoxelGridOccupany[superVoxelIdx] = isSuperVoxelEmpty ? 0 : 1;
    }
#ifdef USE_TBB
    });
#endif

    superVoxelGridTexture->getImage()->uploadData(
            superVoxelGridSize * sizeof(glm::vec2), superVoxelGridMinMaxDensity);
    superVoxelGridOccupancyTexture->getImage()->uploadData(
            superVoxelGridSize * sizeof(uint8_t), superVoxelGridOccupany);
}

SuperVoxelGridDecompositionTracking::~SuperVoxelGridDecompositionTracking() {
    delete[] superVoxelGridOccupany;
    delete[] superVoxelGridMinMaxDensity;
}
