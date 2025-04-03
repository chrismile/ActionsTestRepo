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

#if defined(VULKAN_RAY_TRACING_SHADER)
#include "TubeRayTracingHeader.glsl"
#endif
#include "TransferFunction.glsl"

#ifndef GENERAL_TRIANGLE_MESH
#include "LineDataSSBO.glsl"
#endif // !defined(GENERAL_TRIANGLE_MESH)

#ifdef USE_MLAT
#include "MlatInsert.glsl"
#endif

//#define ANALYTIC_HIT_COMPUTATION
#include "Lighting.glsl"
#include "Antialiasing.glsl"
#include "MultiVar.glsl"

#define M_PI 3.14159265358979323846

//#define USE_ORTHOGRAPHIC_TUBE_PROJECTION

#ifndef USE_ORTHOGRAPHIC_TUBE_PROJECTION
mat3 shearSymmetricMatrix(vec3 p) {
    return mat3(vec3(0.0, -p.z, p.y), vec3(p.z, 0.0, -p.x), vec3(-p.y, p.x, 0.0));
}
#endif

#if defined(USE_ROTATING_HELICITY_BANDS) || defined(USE_MULTI_VAR_RENDERING)
void drawSeparatorStripe(inout vec4 surfaceColor, in float varFraction, in float separatorWidth, in float aaf) {
    aaf *= 10.0;
    float alphaBorder1 = smoothstep(aaf, 0.0, varFraction);
    float alphaBorder2 = smoothstep(separatorWidth - aaf * 0.5, separatorWidth + aaf * 0.5, varFraction);
    surfaceColor.rgb = surfaceColor.rgb * max(alphaBorder1, alphaBorder2);
}
#endif

#ifdef USE_HELICITY_BANDS_TEXTURE
layout(binding = HELICITY_BANDS_TEXTURE_BINDING) uniform sampler2D helicityBandsTexture;
vec4 sampleHelicityBandsTexture(in float u, in float globalPos) {
    return texture(helicityBandsTexture, vec2(u, 0.5));
    //return textureGrad(helicityBandsTexture, vec2(u, 0.5), vec2(dFdx(globalPos), 0.0), vec2(dFdy(globalPos), 0.0));
}
#endif

void computeFragmentColor(
        vec3 fragmentPositionWorld, vec3 fragmentNormal,
#ifndef GENERAL_TRIANGLE_MESH
        vec3 fragmentTangent,
#endif
#if defined(USE_CAPPED_TUBES) && !defined(GENERAL_TRIANGLE_MESH)
        bool isCap,
#endif
#if defined (USE_BANDS) || defined(USE_AMBIENT_OCCLUSION) || defined(USE_ROTATING_HELICITY_BANDS)
        float phi,
#endif
#if (defined(USE_AMBIENT_OCCLUSION) || defined(USE_MULTI_VAR_RENDERING)) && !defined(GENERAL_TRIANGLE_MESH)
        float fragmentVertexId,
#endif
#ifdef USE_BANDS
        vec3 linePosition, vec3 lineNormal,
#endif
#ifdef USE_ROTATING_HELICITY_BANDS
        float fragmentRotation,
#endif
#if defined(USE_ROTATING_HELICITY_BANDS) && defined(UNIFORM_HELICITY_BAND_WIDTH)
        float rotationSeparatorScale,
#endif
#ifdef STRESS_LINE_DATA
        uint principalStressIndex, float lineAppearanceOrder,
#ifdef USE_PRINCIPAL_STRESSES
        float fragmentMajorStress, float fragmentMediumStress, float fragmentMinorStress,
#endif
#endif
        float fragmentAttribute
) {
#ifdef USE_ROTATING_HELICITY_BANDS
#ifdef USE_MULTI_VAR_RENDERING
    float varFraction = mod(phi + fragmentRotation, 2.0 / float(numSubdivisionsBands) * float(M_PI));
#else
    float varFraction = mod(phi + fragmentRotation, 2.0 / float(numSubdivisionsBands) * float(M_PI));
    //float varFraction = mod(phi + fragmentRotation, 0.25 * float(M_PI));
#endif
#endif

#if defined(USE_MULTI_VAR_RENDERING) && defined(USE_ROTATING_HELICITY_BANDS)
    vec4 fragmentColor = vec4(vec3(0.5), 1.0);
    if (numSelectedAttributes > 0u) {
        uint attributeIdx = uint(mod((phi + fragmentRotation) * 0.5 / float(M_PI), 1.0) * float(numSubdivisionsBands)) % numSelectedAttributes;
        uint attributeIdxReal = getRealAttributeIndex(attributeIdx);
        float sampledFragmentAttribute = sampleAttributeLinear(fragmentVertexId, attributeIdxReal);
        fragmentColor = transferFunction(sampledFragmentAttribute, attributeIdxReal);
    }
#elif !defined(USE_MULTI_VAR_RENDERING)
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    vec4 fragmentColor = transferFunction(fragmentAttribute, principalStressIndex);
#else
    vec4 fragmentColor = transferFunction(fragmentAttribute);
#endif
#endif

#if defined(USE_MLAT) && !(defined(USE_MULTI_VAR_RENDERING) && !defined(USE_ROTATING_HELICITY_BANDS))
    if (fragmentColor.a == 0.0) {
        ignoreIntersectionEXT;
    }
#endif

    const vec3 n = normalize(fragmentNormal);
    const vec3 v = normalize(cameraPosition - fragmentPositionWorld);
#ifndef GENERAL_TRIANGLE_MESH
    const vec3 t = normalize(fragmentTangent);
    // Project v into plane perpendicular to t to get newV.
    vec3 helperVec = normalize(cross(t, v));
    vec3 newV = normalize(cross(helperVec, t));

#ifdef USE_BANDS
#if defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES)
    float stressX;
    float stressZ;
    if (principalStressIndex == 0) {
        stressX = fragmentMediumStress;
        stressZ = fragmentMinorStress;
    } else if (principalStressIndex == 1) {
        stressX = fragmentMinorStress;
        stressZ = fragmentMajorStress;
    } else {
        stressX = fragmentMediumStress;
        stressZ = fragmentMajorStress;
    }
#endif

#ifdef ANALYTIC_TUBE_INTERSECTIONS
    bool useBand = false; // Bands not supported (yet) for analytic tube intersections.
#else
#ifdef STRESS_LINE_DATA
    bool useBand = psUseBands[principalStressIndex] > 0;
#else
    bool useBand = true;
#endif
#endif

#if defined(USE_NORMAL_STRESS_RATIO_TUBES)
    float factorX = clamp(abs(stressX / stressZ), 0.0, 1.0f);
    float factorZ = clamp(abs(stressZ / stressX), 0.0, 1.0f);
    const float thickness0 = useBand ? factorX : 1.0;
    const float thickness1 = useBand ? factorZ : 1.0;
#elif defined(USE_HYPERSTREAMLINES)
    stressX = max(abs(stressX), minimumHyperstreamlineWidth);
    stressZ = max(abs(stressZ), minimumHyperstreamlineWidth);
    const float thickness0 = useBand ? stressX : 1.0;
    const float thickness1 = useBand ? stressZ : 1.0;
#else
    // Bands with minimum thickness.
    const float thickness = useBand ? MIN_THICKNESS : 1.0;
#endif

#endif

#if defined(USE_HALOS) || defined(USE_MULTI_VAR_RENDERING)
    float ribbonPosition;

#if defined(USE_CAPPED_TUBES) && !defined(GENERAL_TRIANGLE_MESH)
    if (isCap) {
        vec3 crossProdVn = cross(v, n);
        ribbonPosition = length(crossProdVn);

        // Get the symmetric ribbon position (ribbon direction is perpendicular to line direction) between 0 and 1.
        // NOTE: len(cross(a, b)) == area of parallelogram spanned by a and b.
        vec3 crossProdVn2 = cross(newV, n);
        float ribbonPosition2 = length(crossProdVn2);

        // Side note: We can also use the code below, as for a, b with length 1:
        // sqrt(1 - dot^2(a,b)) = len(cross(a,b))
        // Due to:
        // - dot(a,b) = ||a|| ||b|| cos(phi)
        // - len(cross(a,b)) = ||a|| ||b|| |sin(phi)|
        // - sin^2(phi) + cos^2(phi) = 1
        //ribbonPosition = dot(newV, n);
        //ribbonPosition = sqrt(1 - ribbonPosition * ribbonPosition);

        // Get the winding of newV relative to n, taking into account that t is the normal of the plane both vectors lie in.
        // NOTE: dot(a, cross(b, c)) = det(a, b, c), which is the signed volume of the parallelepiped spanned by a, b, c.
        if (dot(t, crossProdVn) < 0.0) {
            ribbonPosition2 = -ribbonPosition2;
        }
        if (dot(t, crossProdVn) < 0.0) {
            ribbonPosition = -ribbonPosition;
        }
        // Normalize the ribbon position: [-1, 1] -> [0, 1].
        //ribbonPosition = ribbonPosition / 2.0 + 0.5;
        ribbonPosition2 = clamp(ribbonPosition2, -1.0, 1.0);

        //ribbonPosition = min(ribbonPosition, abs(ribbonPosition2));
        if (abs(ribbonPosition2) < abs(ribbonPosition)) {
            ribbonPosition = ribbonPosition2;
        }
    } else {
#endif

#ifdef USE_BANDS
        vec3 lineN = normalize(lineNormal);
        vec3 lineB = cross(t, lineN);
        mat3 tangentFrameMatrix = mat3(lineN, lineB, t);

#ifdef USE_ORTHOGRAPHIC_TUBE_PROJECTION
        vec2 localV = normalize((transpose(tangentFrameMatrix) * newV).xy);
#if defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES)
        vec2 p = vec2(thickness0 * cos(phi), thickness1 * sin(phi));
#else
        vec2 p = vec2(thickness * cos(phi), sin(phi));
#endif
        float d = length(p);
        p = normalize(p);
        float alpha = acos(dot(localV, p));

#if defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES)
        float phiMax = atan(thickness1 * localV.x, -thickness0 * localV.y);
        vec2 pointMax0 = vec2(thickness0 * cos(phiMax), thickness1 * sin(phiMax));
        vec2 pointMax1 = vec2(thickness0 * cos(phiMax + M_PI), thickness1 * sin(phiMax + M_PI));
#else
        float phiMax = atan(localV.x, -thickness * localV.y);
        vec2 pointMax0 = vec2(thickness * cos(phiMax), sin(phiMax));
        vec2 pointMax1 = vec2(thickness * cos(phiMax + M_PI), sin(phiMax + M_PI));
#endif

        vec2 planeDir = pointMax1 - pointMax0;
        float totalDist = length(planeDir);
        planeDir = normalize(planeDir);

        float beta = acos(dot(planeDir, localV));

        float x = d / sin(beta) * sin(alpha);
        ribbonPosition = x / totalDist * 2.0;
#else
        // Project onto the tangent plane.
        const vec3 cNorm = cameraPosition - linePosition;
        const float dist = dot(cNorm, fragmentTangent);
        const vec3 cHat = transpose(tangentFrameMatrix) * (cNorm - dist * fragmentTangent);
        const float lineRadius = (useBand ? bandWidth : lineWidth) * 0.5;

        // Homogeneous, normalized coordinates of the camera position in the tangent plane.
        const vec3 c = vec3(cHat.xy / lineRadius, 1.0);

        // Primal conic section matrix.
        //const mat3 A = mat3(
        //        1.0 / (thickness*thickness), 0.0, 0.0,
        //        0.0, 1.0, 0.0,
        //        0.0, 0.0, -1.0
        //);

        // Polar of c.
        //const vec3 l = A * c;

        // Polar of c.
#if defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES)
        const float a = 1.0 / (thickness0 * thickness0);
        const float b = 1.0 / (thickness1 * thickness1);
        const vec3 l = vec3(a * c.x, b * c.y, -1.0);
#else
        const float a = 1.0 / (thickness * thickness);
        const vec3 l = vec3(a * c.x, c.y, -1.0);
#endif

        const mat3 M_l = shearSymmetricMatrix(l);
        //const mat3 B = transpose(M_l) * A * M_l;

#if defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES)
        const mat3 B = mat3(
            b*l.z*l.z - l.y*l.y, l.x*l.y, -b*l.x*l.z,
            l.x*l.y, a*l.z*l.z - l.x*l.x, -a*l.y*l.z,
            -b*l.x*l.z, -a*l.y*l.z, a*l.y*l.y + b*l.x*l.x
        );
#else
        const mat3 B = mat3(
            l.z*l.z - l.y*l.y, l.x*l.y, -l.x*l.z,
            l.x*l.y, a*l.z*l.z - l.x*l.x, -a*l.y*l.z,
            -l.x*l.z, -a*l.y*l.z, a*l.y*l.y + l.x*l.x
        );
#endif

        const float EPSILON = 1e-4;
        float alpha = 0.0;
        float discr = 0.0;
        if (abs(l.z) > EPSILON) {
            discr = -B[0][0] * B[1][1] + B[0][1] * B[1][0];
            alpha = sqrt(discr) / l.z;
        } else if (abs(l.y) > EPSILON) {
            discr = -B[0][0] * B[2][2] + B[0][2] * B[2][0];
            alpha = sqrt(discr) / l.y;
        } else if (abs(l.x) > EPSILON) {
            discr = -B[1][1] * B[2][2] + B[1][2] * B[2][1];
            alpha = sqrt(discr) / l.x;
        }

        mat3 C = B + alpha * M_l;

        vec2 pointMax0 = vec2(0.0);
        vec2 pointMax1 = vec2(0.0);
        for (int i = 0; i < 2; ++i) {
            if (abs(C[i][i]) > EPSILON) {
                pointMax0 = C[i].xy / C[i].z; // column vector
                pointMax1 = vec2(C[0][i], C[1][i]) / C[2][i]; // row vector
            }
        }

#if defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES)
        vec2 p = vec2(thickness0 * cos(phi), thickness1 * sin(phi));
#else
        vec2 p = vec2(thickness * cos(phi), sin(phi));
#endif

        vec3 pLineHomogeneous = cross(l, cross(c, vec3(p, 1.0)));
        vec2 pLine = pLineHomogeneous.xy / pLineHomogeneous.z;

        ribbonPosition = length(pLine - pointMax0) / length(pointMax1 - pointMax0) * 2.0 - 1.0;
#endif

#else
        // Get the symmetric ribbon position (ribbon direction is perpendicular to line direction) between 0 and 1.
        // NOTE: len(cross(a, b)) == area of parallelogram spanned by a and b.
        vec3 crossProdVn = cross(newV, n);
        ribbonPosition = length(crossProdVn);

        // Side note: We can also use the code below, as for a, b with length 1:
        // sqrt(1 - dot^2(a,b)) = len(cross(a,b))
        // Due to:
        // - dot(a,b) = ||a|| ||b|| cos(phi)
        // - len(cross(a,b)) = ||a|| ||b|| |sin(phi)|
        // - sin^2(phi) + cos^2(phi) = 1
        //ribbonPosition = dot(newV, n);
        //ribbonPosition = sqrt(1 - ribbonPosition * ribbonPosition);

        // Get the winding of newV relative to n, taking into account that t is the normal of the plane both vectors lie in.
        // NOTE: dot(a, cross(b, c)) = det(a, b, c), which is the signed volume of the parallelepiped spanned by a, b, c.
        if (dot(t, crossProdVn) < 0.0) {
            ribbonPosition = -ribbonPosition;
        }
        // Normalize the ribbon position: [-1, 1] -> [0, 1].
        //ribbonPosition = ribbonPosition / 2.0 + 0.5;
        ribbonPosition = clamp(ribbonPosition, -1.0, 1.0);
#endif

#ifdef USE_CAPPED_TUBES
    }
#endif

#if !defined(USE_CAPPED_TUBES) && defined(USE_BANDS) && (defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES))
    if (useBand && dot(n, v) < 0.0) {
        ribbonPosition = 0.0;
    }
#endif

#endif // defined(USE_HALOS) || defined(USE_MULTI_VAR_RENDERING)
#endif // !defined(GENERAL_TRIANGLE_MESH)


#if defined(USE_DEPTH_CUES) || (defined(USE_AMBIENT_OCCLUSION) && !defined(STATIC_AMBIENT_OCCLUSION_PREBAKING))
    vec3 screenSpacePosition = (viewMatrix * vec4(fragmentPositionWorld, 1.0)).xyz;
#endif

#if defined(USE_MULTI_VAR_RENDERING) && !defined(USE_ROTATING_HELICITY_BANDS)
    int numSubdivisionsView = int(numSelectedAttributes);
    float varFraction = mod((ribbonPosition * 0.5 + 0.5) * float(numSubdivisionsView), 1.0);

    vec4 fragmentColor = vec4(vec3(0.5), 1.0);
    if (numSelectedAttributes > 0u) {
        uint attributeIdx = uint((ribbonPosition * 0.5 + 0.5) * float(numSubdivisionsView)) % numSelectedAttributes;
        uint attributeIdxReal = getRealAttributeIndex(attributeIdx);
        float sampledFragmentAttribute = sampleAttributeLinear(fragmentVertexId, attributeIdxReal);
        fragmentColor = transferFunction(sampledFragmentAttribute, attributeIdxReal);
    }

#if defined(USE_MLAT)
    if (fragmentColor.a == 0.0) {
        ignoreIntersectionEXT;
    }
#endif
#endif

#ifndef GENERAL_TRIANGLE_MESH
    fragmentColor = blinnPhongShadingTube(
            fragmentColor, fragmentPositionWorld,
#if defined(USE_DEPTH_CUES) || (defined(USE_AMBIENT_OCCLUSION) && !defined(STATIC_AMBIENT_OCCLUSION_PREBAKING))
            screenSpacePosition,
#endif
#ifdef USE_AMBIENT_OCCLUSION
            fragmentVertexId, phi,
#endif
#ifdef USE_BANDS
            useBand ? 1 : 0,
#endif
            n, t);
#else // defined(GENERAL_TRIANGLE_MESH)
    fragmentColor = blinnPhongShadingTriangleMesh(
            fragmentColor, fragmentPositionWorld,
#if defined(USE_DEPTH_CUES) || (defined(USE_AMBIENT_OCCLUSION) && !defined(STATIC_AMBIENT_OCCLUSION_PREBAKING))
            screenSpacePosition,
#endif
            n);
#endif // !defined(GENERAL_TRIANGLE_MESH)

#ifndef GENERAL_TRIANGLE_MESH
#if defined(USE_HALOS) || defined(USE_MULTI_VAR_RENDERING)
    float absCoords = abs(ribbonPosition);
#else
    float absCoords = 0.0;
#endif

    float fragmentDepth = length(fragmentPositionWorld - cameraPosition);
#ifdef USE_BANDS
    //float EPSILON_OUTLINE = clamp(fragmentDepth * 0.0005 / (useBand ? bandWidth : lineWidth), 0.0, 0.49);
    float EPSILON_OUTLINE = clamp(getAntialiasingFactor(fragmentDepth / (useBand ? bandWidth : lineWidth) * 0.25), 0.0, 0.49);
    float EPSILON_WHITE = clamp(getAntialiasingFactor(fragmentDepth / (useBand ? bandWidth : lineWidth) * 0.25), 0.0, 0.49);
#else
    //float EPSILON_OUTLINE = clamp(fragmentDepth * 0.0005 / lineWidth, 0.0, 0.49);
    float EPSILON_OUTLINE = clamp(getAntialiasingFactor(fragmentDepth / lineWidth * 0.05), 0.0, 0.49);
    float EPSILON_WHITE = clamp(getAntialiasingFactor(fragmentDepth / lineWidth * 2.0), 0.0, 0.49);
#endif

#ifdef USE_ROTATING_HELICITY_BANDS
    float separatorWidth = separatorBaseWidth;
#ifdef UNIFORM_HELICITY_BAND_WIDTH
    separatorWidth = separatorWidth / rotationSeparatorScale;
#endif
#ifdef USE_MULTI_VAR_RENDERING
    drawSeparatorStripe(
            fragmentColor, mod(phi + fragmentRotation + separatorWidth * 0.5, 2.0 / float(numSubdivisionsBands) * float(M_PI)),
            separatorWidth, EPSILON_OUTLINE * 0.5 * float(numSubdivisionsBands));
#else
#ifdef USE_HELICITY_BANDS_TEXTURE
    const float twoPi = 2.0 * float(M_PI);
    fragmentColor *= sampleHelicityBandsTexture(
            mod(phi + fragmentRotation + separatorWidth * 0.5, twoPi) / twoPi,
            (phi + fragmentRotation) / twoPi);
#else
    drawSeparatorStripe(
            fragmentColor, mod(phi + fragmentRotation + separatorWidth * 0.5, 2.0 / float(numSubdivisionsBands) * float(M_PI)),
            separatorWidth, EPSILON_OUTLINE);
#endif
#endif
#elif defined(USE_MULTI_VAR_RENDERING)
    float separatorWidth = numSelectedAttributes > 1 ? 0.4 / float(numSelectedAttributes) : separatorBaseWidth;
    if (numSelectedAttributes > 0) {
        drawSeparatorStripe(
                fragmentColor, mod((ribbonPosition * 0.5 + 0.5) * float(numSubdivisionsView) + 0.5 * separatorWidth, 1.0),
                separatorBaseWidth, EPSILON_OUTLINE * 0.5 * float(numSubdivisionsView));
    }
#endif

#if defined(USE_ROTATING_HELICITY_BANDS)
    const float WHITE_THRESHOLD = 0.8;
#elif defined(USE_MULTI_VAR_RENDERING)
    const float WHITE_THRESHOLD = max(1.0 - separatorWidth, 0.8);
#else
    const float WHITE_THRESHOLD = 0.7;
#endif

#if defined(USE_HALOS) || defined(USE_MULTI_VAR_RENDERING)
    float coverage = 1.0 - smoothstep(1.0 - EPSILON_OUTLINE, 1.0, absCoords);
#else
    float coverage = 1.0;
#endif

#if (!defined(USE_CAPPED_TUBES) && defined(USE_BANDS) && (defined(USE_NORMAL_STRESS_RATIO_TUBES) || defined(USE_HYPERSTREAMLINES))) \
        || (defined(USE_BANDS) && defined(ANALYTIC_ELLIPTIC_TUBE_INTERSECTIONS))
    if (useBand) {
        coverage = 1.0;
    }
#endif
    vec4 colorOut = vec4(
            mix(fragmentColor.rgb, foregroundColor.rgb,
            smoothstep(WHITE_THRESHOLD - EPSILON_WHITE, WHITE_THRESHOLD + EPSILON_WHITE, absCoords)),
            fragmentColor.a * coverage);
#else // defined(GENERAL_TRIANGLE_MESH)
    vec4 colorOut = fragmentColor;
#endif // !defined(GENERAL_TRIANGLE_MESH)

    // Code for debugging acceleration structure instances.
    // https://colorbrewer2.org/#type=qualitative&scheme=Set1&n=8
    /*vec3 colorMap[8] = {
        vec3(228.0, 26.0 , 28.0 ) / 255.0,
        vec3(55.0 , 126.0, 184.0) / 255.0,
        vec3(77.0 , 175.0, 74.0 ) / 255.0,
        vec3(152.0, 78.0 , 163.0) / 255.0,
        vec3(255.0, 127.0, 0.0  ) / 255.0,
        vec3(255.0, 255.0, 51.0 ) / 255.0,
        vec3(166.0, 86.0 , 40.0 ) / 255.0,
        vec3(247.0, 129.0, 191.0) / 255.0
    };
    colorOut.rgb = colorMap[gl_InstanceID % 8];*/

#ifdef VISUALIZE_SEEDING_PROCESS
    // For letting lines appear one after another in an animation showing the used seeding technique.
    if (int(lineAppearanceOrder) > currentSeedIdx) {
        colorOut.a = 0.0;
    }
#endif

#ifdef FRAGMENT_SHADER
    fragColor = colorOut;
#elif defined(USE_MLAT)
    insertNodeMlat(colorOut);
#else
    payload.hitColor = colorOut;
    payload.hitT = length(fragmentPositionWorld - cameraPosition);
    payload.hasHit = true;
#endif
}
