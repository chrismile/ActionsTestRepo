/**
 * MIT License
 *
 * Copyright (c) 2021-2022, Christoph Neuhauser, Timm Knörle, Ludwig Leonard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

layout (local_size_x = LOCAL_SIZE, local_size_y = LOCAL_SIZE, local_size_z = 1) in;

layout (binding = 0, rgba32f) uniform image2D resultImage;

#ifdef USE_NANOVDB
layout (binding = 1) readonly buffer NanoVdbBuffer {
    uint pnanovdb_buf_data[];
};
#ifdef USE_EMISSION
layout (binding = 2) readonly buffer EmissionNanoVdbBuffer {
    uint pnanovdb_emission_buf_data[];
};
#endif
#else // USE_NANOVDB
layout (binding = 1) uniform sampler3D gridImage;
#ifdef USE_EMISSION
layout (binding = 2) uniform sampler3D emissionImage;
#endif
#endif // USE_NANOVDB

layout (binding = 3) uniform Parameters {
    // Transform from normalized device coordinates to world space.
    mat4 inverseViewProjMatrix;
    mat4 previousViewProjMatrix;

    // Cloud properties.
    vec3 boxMin;
    vec3 boxMax;
    vec3 gridMin;
    vec3 gridMax;
    vec3 emissionBoxMin;
    vec3 emissionBoxMax;
    vec3 extinction;
    vec3 scatteringAlbedo;

    float phaseG;

    // Sky properties.
    vec3 sunDirection;
    vec3 sunIntensity;
    float environmentMapIntensityFactor;

    float emissionCap;
    float emissionStrength;
    int numFeatureMapSamplesPerFrame;

    // Whether to use linear RGB or sRGB.
    int useLinearRGB;

    // For residual ratio tracking and decomposition tracking.
    ivec3 superVoxelSize;
    ivec3 superVoxelGridSize;

    //ivec3 gridResolution;

    // Isosurfaces.
    vec3 isoSurfaceColor;
    float isoValue;
} parameters;

layout (binding = 4) uniform FrameInfo {
    uint frameCount;
    uvec3 other;
} frameInfo;

layout (binding = 5, rgba32f) uniform image2D accImage;

layout (binding = 6, rgba32f) uniform image2D firstX;

layout (binding = 7, rgba32f) uniform image2D firstW;

#if !defined(USE_NANOVDB) && (defined(USE_RESIDUAL_RATIO_TRACKING) || defined(USE_DECOMPOSITION_TRACKING))
layout (binding = 8) uniform sampler3D superVoxelGridImage;
layout (binding = 9) uniform usampler3D superVoxelGridOccupancyImage;
#endif

#ifdef USE_ENVIRONMENT_MAP_IMAGE
layout (binding = 10) uniform sampler2D environmentMapTexture;
layout (binding = 11) uniform sampler2D environmentMapOctahedralTexture;
#endif

#ifdef COMPUTE_PRIMARY_RAY_ABSORPTION_MOMENTS
layout (binding = 12, r32f) uniform image2DArray primaryRayAbsorptionMomentsImage;
#endif

#ifdef COMPUTE_SCATTER_RAY_ABSORPTION_MOMENTS
layout (binding = 13, r32f) uniform image2DArray scatterRayAbsorptionMomentsImage;
#endif

layout (binding = 14, rgba32f) uniform image2D cloudOnlyImage;
layout (binding = 15, rg32f) uniform image2D depthImage;
layout (binding = 16, rg32f) uniform image2D densityImage;
layout (binding = 17, rg32f) uniform image2D backgroundImage;
layout (binding = 18, rg32f) uniform image2D reprojUVImage;
layout (binding = 19, rgba32f) uniform image2D normalImage;



/**
 * This code is part of an GLSL port of the HLSL code accompanying the paper "Moment-Based Order-Independent
 * Transparency" by Münstermann, Krumpen, Klein, and Peters (http://momentsingraphics.de/?page_id=210).
 * The original code was released in accordance to CC0 (https://creativecommons.org/publicdomain/zero/1.0/).
 *
 * This port is released under the terms of the MIT License.
 */
/*! This function implements complex multiplication.*/
layout(std140, binding = 20) uniform MomentUniformData {
    vec4 wrapping_zone_parameters;
    //float overestimation;
    //float moment_bias;
};
const float ABSORBANCE_MAX_VALUE = 10.0;

#ifdef USE_TRANSFER_FUNCTION
layout(binding = 21) uniform sampler1D transferFunctionTexture;
#endif

vec2 Multiply(vec2 LHS, vec2 RHS) {
    return vec2(LHS.x * RHS.x - LHS.y * RHS.y, LHS.x * RHS.y + LHS.y * RHS.x);
}
