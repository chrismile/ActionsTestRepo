/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2020 - 2021, Christoph Neuhauser
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

-- VBO.Vertex

#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in float vertexAttribute;
layout(location = 2) in vec3 vertexTangent;
layout(location = 3) in float vertexOpacity;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
layout(location = 4) in uint vertexPrincipalStressIndex;
#endif

layout(location = 0) out vec3 linePosition;
layout(location = 1) out float lineAttribute;
layout(location = 2) out vec3 lineTangent;
layout(location = 3) out float lineOpacity;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
layout(location = 4) out uint linePrincipalStressIndex;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 5) out uint lineVertexId;
#endif

void main() {
    linePosition = (mMatrix * vec4(vertexPosition, 1.0)).xyz;
    lineAttribute = vertexAttribute;
    lineTangent = vertexTangent;
    lineOpacity = vertexOpacity;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    linePrincipalStressIndex = vertexPrincipalStressIndex;
#endif
#ifdef USE_AMBIENT_OCCLUSION
    lineVertexId = uint(gl_VertexIndex);
#endif
    gl_Position = mvpMatrix * vec4(vertexPosition, 1.0);
}

-- VBO.Geometry

#version 450 core

layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

layout(location = 0) in vec3 linePosition[];
layout(location = 1) in float lineAttribute[];
layout(location = 2) in vec3 lineTangent[];
layout(location = 3) in float lineOpacity[];
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
layout(location = 4) in uint linePrincipalStressIndex[];
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 5) in uint lineVertexId[];
#endif

layout(location = 0) out vec3 fragmentPositionWorld;
#ifdef USE_SCREEN_SPACE_POSITION
layout(location = 1) out vec3 screenSpacePosition;
#endif
layout(location = 2) out float fragmentAttribute;
layout(location = 3) out float fragmentOpacity;
layout(location = 4) out float fragmentNormalFloat; // Between -1 and 1
layout(location = 5) out vec3 normal0;
layout(location = 6) out vec3 normal1;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
layout(location = 7) flat out uint fragmentPrincipalStressIndex;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 8) out float fragmentVertexId;
#endif

#include "LineUniformData.glsl"

void main() {
    vec3 linePosition0 = linePosition[0];
    vec3 linePosition1 = linePosition[1];
    vec3 tangent0 = normalize(lineTangent[0]);
    vec3 tangent1 = normalize(lineTangent[1]);

    vec3 viewDirection0 = normalize(cameraPosition - linePosition0);
    vec3 viewDirection1 = normalize(cameraPosition - linePosition1);
    vec3 offsetDirection0 = normalize(cross(tangent0, viewDirection0));
    vec3 offsetDirection1 = normalize(cross(tangent1, viewDirection1));
    vec3 vertexPosition;

    const float lineRadius = lineWidth * 0.5;
    const mat4 pvMatrix = pMatrix * vMatrix;

    // Vertex 0
    vec3 v0Normal0 = normalize(cross(tangent0, offsetDirection0));
    fragmentAttribute = lineAttribute[0];
    fragmentOpacity = lineOpacity[0];
    normal0 = v0Normal0;
    normal1 = offsetDirection0;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    fragmentPrincipalStressIndex = linePrincipalStressIndex[0];
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(lineVertexId[0]);
#endif

    vertexPosition = linePosition0 - lineRadius * offsetDirection0;
    fragmentPositionWorld = vertexPosition;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentNormalFloat = -1.0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    fragmentAttribute = lineAttribute[0];
    fragmentOpacity = lineOpacity[0];
    normal0 = v0Normal0;
    normal1 = offsetDirection0;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    fragmentPrincipalStressIndex = linePrincipalStressIndex[0];
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(lineVertexId[0]);
#endif

    vertexPosition = linePosition0 + lineRadius * offsetDirection0;
    fragmentPositionWorld = vertexPosition;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentNormalFloat = 1.0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    // Vertex 1
    vec3 v1Normal0 = normalize(cross(tangent1, offsetDirection1));
    fragmentAttribute = lineAttribute[1];
    fragmentOpacity = lineOpacity[1];
    normal0 = v1Normal0;
    normal1 = offsetDirection1;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    fragmentPrincipalStressIndex = linePrincipalStressIndex[1];
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(lineVertexId[1]);
#endif

    vertexPosition = linePosition1 - lineRadius * offsetDirection1;
    fragmentPositionWorld = vertexPosition;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentNormalFloat = -1.0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    fragmentAttribute = lineAttribute[1];
    fragmentOpacity = lineOpacity[1];
    normal0 = v1Normal0;
    normal1 = offsetDirection1;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    fragmentPrincipalStressIndex = linePrincipalStressIndex[1];
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(lineVertexId[1]);
#endif

    vertexPosition = linePosition1 + lineRadius * offsetDirection1;
    fragmentPositionWorld = vertexPosition;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentNormalFloat = 1.0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    EndPrimitive();
}

-- Fragment

#version 450 core

layout(location = 0) in vec3 fragmentPositionWorld;
#ifdef USE_SCREEN_SPACE_POSITION
layout(location = 1) in vec3 screenSpacePosition;
#endif
layout(location = 2) in float fragmentAttribute;
layout(location = 3) in float fragmentOpacity;
layout(location = 4) in float fragmentNormalFloat;
layout(location = 5) in vec3 normal0;
layout(location = 6) in vec3 normal1;
#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
layout(location = 7) flat in uint fragmentPrincipalStressIndex;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 8) in float fragmentVertexId;
float phi;
#endif

#ifdef USE_COVERAGE_MASK
in int gl_SampleMaskIn[];
#endif

layout(location = 0) out vec4 fragColor;

#define M_PI 3.14159265358979323846

#include "LineUniformData.glsl"
#include "TransferFunction.glsl"

#define DEPTH_HELPER_USE_PROJECTION_MATRIX
#include "DepthHelper.glsl"
#include "FloatPack.glsl"
#include "Lighting.glsl"
#include "LinkedListHeaderFinal.glsl"

void gatherFragmentCustomDepth(vec4 color, float fragmentDepth) {
    if (color.a < 0.001) {
        discard;
    }

    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    uint pixelIndex = addrGen(uvec2(x,y));

    LinkedListFragmentNode frag;
    frag.color = packUnorm4x8(color);
    frag.next = -1;

    //float depthNormalized = gl_FragCoord.z;
    float depthNormalized = convertLinearDepthToDepthBufferValue(
            convertDepthBufferValueToLinearDepth(gl_FragCoord.z) + fragmentDepth - length(fragmentPositionWorld - cameraPosition) - 0.0001);

#ifdef USE_COVERAGE_MASK
    //packFloat24Uint8(frag.depth, gl_FragCoord.z, gl_SampleMaskIn[0]);
    float coverageRatio = float(bitCount(gl_SampleMaskIn[0])) / float(gl_NumSamples);
    packFloat24Float8(frag.depth, depthNormalized, coverageRatio);
#else
    frag.depth = convertNormalizedFloatToUint32(depthNormalized);
#endif

    uint insertIndex = atomicAdd(fragCounter, 1u);

    if (insertIndex < linkedListSize) {
        // Insert the fragment into the linked list
        frag.next = atomicExchange(startOffset[pixelIndex], insertIndex);
        fragmentBuffer[insertIndex] = frag;
    }
}

void main() {
    // Compute the normal of the billboard tube for shading.
    vec3 fragmentNormal;
    float interpolationFactor = fragmentNormalFloat;
    vec3 normalCos = normalize(normal0);
    vec3 normalSin = normalize(normal1);
    if (interpolationFactor < 0.0) {
        normalSin = -normalSin;
        interpolationFactor = -interpolationFactor;
    }
    float angle = interpolationFactor * M_PI * 0.5;
    fragmentNormal = cos(angle) * normalCos + sin(angle) * normalSin;
#ifdef USE_AMBIENT_OCCLUSION
    phi = angle;
#endif

#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    vec4 fragmentColor = transferFunction(fragmentAttribute, fragmentPrincipalStressIndex);
#else
    vec4 fragmentColor = transferFunction(fragmentAttribute);
#endif
    fragmentColor.a = 1.0; // Ignore transparency mapping.
    fragmentColor = blinnPhongShading(fragmentColor, fragmentNormal);

    float absCoords = abs(fragmentNormalFloat);
    float fragmentDepth = length(fragmentPositionWorld - cameraPosition);
    const float WHITE_THRESHOLD = 0.7;
    float EPSILON = clamp(fragmentDepth * 0.0015 / lineWidth, 0.0, 0.49);
    float coverage = 1.0 - smoothstep(1.0 - 2.0*EPSILON, 1.0, absCoords);
    //float coverage = 1.0 - smoothstep(1.0, 1.0, abs(fragmentNormalFloat));
    vec4 colorOut = vec4(mix(fragmentColor.rgb, foregroundColor.rgb,
            smoothstep(WHITE_THRESHOLD - EPSILON, WHITE_THRESHOLD + EPSILON, absCoords)),
            fragmentColor.a * coverage);

    colorOut.a *= fragmentOpacity;
    gatherFragmentCustomDepth(colorOut, fragmentDepth);

    fragColor = vec4(0.0);
}
