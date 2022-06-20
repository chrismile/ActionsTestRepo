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

-- Programmable.Vertex

#version 450 core

#include "LineUniformData.glsl"

struct LinePointData {
    vec3 vertexPosition;
    float vertexAttribute;
    vec3 vertexTangent;
    uint vertexPrincipalStressIndex;
};

layout (std430, binding = LINE_POINTS_BUFFER_BINDING) readonly buffer LinePoints {
    LinePointData linePoints[];
};
#ifdef USE_LINE_HIERARCHY_LEVEL
layout (std430, binding = LINE_HIERARCHY_LEVELS_BUFFER_BINDING) readonly buffer LineHierarchyLevels {
    float lineHierarchyLevels[];
};
#endif

layout(location = 0) out vec3 fragmentPositionWorld;
#ifdef USE_SCREEN_SPACE_POSITION
layout(location = 1) out vec3 screenSpacePosition;
#endif
layout(location = 2) out float fragmentAttribute;
layout(location = 3) out vec3 fragmentTangent;
layout(location = 4) out float fragmentNormalFloat; // Between -1 and 1
layout(location = 5) out vec3 normal0;
layout(location = 6) out vec3 normal1;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
layout(location = 7) flat out uint fragmentPrincipalStressIndex;
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
layout(location = 8) flat out float fragmentLineHierarchyLevel;
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
layout(location = 9) flat out uint fragmentLineAppearanceOrder;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 10) out float fragmentVertexId;
#endif

void main() {
    uint pointIndex = gl_VertexIndex / 2;
    LinePointData linePointData = linePoints[pointIndex];
    vec3 linePoint = (mMatrix * vec4(linePointData.vertexPosition, 1.0)).xyz;
    vec3 tangent = normalize(linePointData.vertexTangent);

    vec3 viewDirection = normalize(cameraPosition - linePoint);
    vec3 offsetDirection = normalize(cross(viewDirection, tangent));
    vec3 vertexPosition;
    float shiftSign = 1.0f;
    if (gl_VertexIndex % 2 == 0) {
        shiftSign = -1.0;
    }
    vertexPosition = linePoint + shiftSign * lineWidth * 0.5 * offsetDirection;
    fragmentNormalFloat = shiftSign;
    normal0 = normalize(cross(tangent, offsetDirection));
    normal1 = offsetDirection;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
    fragmentPrincipalStressIndex = linePointData.vertexPrincipalStressIndex;
#endif

#ifdef USE_LINE_HIERARCHY_LEVEL
    float lineHierarchyLevel = lineHierarchyLevels[pointIndex];
    fragmentLineHierarchyLevel = lineHierarchyLevel;
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    // Unsupported for now.
    fragmentLineAppearanceOrder = 0;
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(pointIndex);
#endif

    fragmentPositionWorld = vertexPosition;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentAttribute = linePointData.vertexAttribute;
    fragmentTangent = linePointData.vertexTangent;
    gl_Position = pMatrix * vMatrix * vec4(vertexPosition, 1.0);
}

-- VBO.Vertex

#version 450 core

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in float vertexAttribute;
layout(location = 2) in vec3 vertexTangent;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
layout(location = 4) in uint vertexPrincipalStressIndex;
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
layout(location = 5) in float vertexLineHierarchyLevel;
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
layout(location = 6) in uint vertexLineAppearanceOrder;
#endif

layout(location = 0) out vec3 linePosition;
layout(location = 1) out float lineAttribute;
layout(location = 2) out vec3 lineTangent;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
layout(location = 3) out uint linePrincipalStressIndex;
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
layout(location = 4) out float lineLineHierarchyLevel;
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
layout(location = 5) out uint lineLineAppearanceOrder;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 6) out uint lineVertexId;
#endif

#include "LineUniformData.glsl"
#include "TransferFunction.glsl"

void main() {
    linePosition = (mMatrix * vec4(vertexPosition, 1.0)).xyz;
    lineAttribute = vertexAttribute;
    lineTangent = vertexTangent;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
    linePrincipalStressIndex = vertexPrincipalStressIndex;
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
    lineLineHierarchyLevel = vertexLineHierarchyLevel;
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    lineLineAppearanceOrder = vertexLineAppearanceOrder;
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

#include "LineUniformData.glsl"

layout(location = 0) in vec3 linePosition[];
layout(location = 1) in float lineAttribute[];
layout(location = 2) in vec3 lineTangent[];
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
layout(location = 3) in uint linePrincipalStressIndex[];
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
layout(location = 4) in float lineLineHierarchyLevel[];
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
layout(location = 5) in uint lineLineAppearanceOrder[];
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 6) in uint lineVertexId[];
#endif

layout(location = 0) out vec3 fragmentPositionWorld;
#ifdef USE_SCREEN_SPACE_POSITION
layout(location = 1) out vec3 screenSpacePosition;
#endif
layout(location = 2) out float fragmentAttribute;
layout(location = 3) out vec3 fragmentTangent;
layout(location = 4) out float fragmentNormalFloat; // Between -1 and 1
layout(location = 5) out vec3 normal0;
layout(location = 6) out vec3 normal1;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
layout(location = 7) flat out uint fragmentPrincipalStressIndex;
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
layout(location = 8) flat out float fragmentLineHierarchyLevel;
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
layout(location = 9) flat out uint fragmentLineAppearanceOrder;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 10) out float fragmentVertexId;
#endif

void main() {
    vec3 linePosition0 = (mMatrix * vec4(linePosition[0], 1.0)).xyz;
    vec3 linePosition1 = (mMatrix * vec4(linePosition[1], 1.0)).xyz;
    vec3 tangent0 = normalize(lineTangent[0]);
    vec3 tangent1 = normalize(lineTangent[1]);

    vec3 viewDirection0 = normalize(cameraPosition - linePosition0);
    vec3 viewDirection1 = normalize(cameraPosition - linePosition1);
    vec3 offsetDirection0 = normalize(cross(viewDirection0, tangent0));
    vec3 offsetDirection1 = normalize(cross(viewDirection1, tangent1));
    vec3 vertexPosition;

    const float lineRadius = lineWidth * 0.5;
    const mat4 pvMatrix = pMatrix * vMatrix;

    // Vertex 0
    vec3 v0Normal0 = normalize(cross(tangent0, offsetDirection0));
    fragmentAttribute = lineAttribute[0];
    fragmentTangent = tangent0;
    normal0 = v0Normal0;
    normal1 = offsetDirection0;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
    fragmentPrincipalStressIndex = linePrincipalStressIndex[0];
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
    fragmentLineHierarchyLevel = lineLineHierarchyLevel[0];
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    fragmentLineAppearanceOrder = lineLineAppearanceOrder[0];
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
    fragmentTangent = tangent0;
    normal0 = v0Normal0;
    normal1 = offsetDirection0;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
    fragmentPrincipalStressIndex = linePrincipalStressIndex[0];
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
    fragmentLineHierarchyLevel = lineLineHierarchyLevel[0];
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    fragmentLineAppearanceOrder = lineLineAppearanceOrder[0];
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
    fragmentTangent = tangent1;
    normal0 = v1Normal0;
    normal1 = offsetDirection1;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
    fragmentPrincipalStressIndex = linePrincipalStressIndex[1];
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
    fragmentLineHierarchyLevel = lineLineHierarchyLevel[1];
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    fragmentLineAppearanceOrder = lineLineAppearanceOrder[1];
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(lineVertexId[1]);
#endif

    vertexPosition = linePosition1 - lineRadius * offsetDirection1;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentPositionWorld = vertexPosition;
    fragmentNormalFloat = -1.0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    fragmentAttribute = lineAttribute[1];
    fragmentTangent = tangent1;
    normal0 = v1Normal0;
    normal1 = offsetDirection1;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
    fragmentPrincipalStressIndex = linePrincipalStressIndex[1];
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
    fragmentLineHierarchyLevel = lineLineHierarchyLevel[1];
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    fragmentLineAppearanceOrder = lineLineAppearanceOrder[1];
#endif
#ifdef USE_AMBIENT_OCCLUSION
    fragmentVertexId = float(lineVertexId[1]);
#endif

    vertexPosition = linePosition1 + lineRadius * offsetDirection1;
#ifdef USE_SCREEN_SPACE_POSITION
    screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
#endif
    fragmentPositionWorld = vertexPosition;
    fragmentNormalFloat = 1.0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    EndPrimitive();
}

-- Fragment

#version 450 core

#include "LineUniformData.glsl"

layout(location = 0) in vec3 fragmentPositionWorld;
#ifdef USE_SCREEN_SPACE_POSITION
layout(location = 1) in vec3 screenSpacePosition;
#endif
layout(location = 2) in float fragmentAttribute;
layout(location = 3) in vec3 fragmentTangent;
layout(location = 4) in float fragmentNormalFloat;
layout(location = 5) in vec3 normal0;
layout(location = 6) in vec3 normal1;
#if defined(USE_PRINCIPAL_STRESS_DIRECTION_INDEX) || defined(USE_LINE_HIERARCHY_LEVEL)
layout(location = 7) flat in uint fragmentPrincipalStressIndex;
#endif
#ifdef USE_LINE_HIERARCHY_LEVEL
layout(location = 8) flat in float fragmentLineHierarchyLevel;
#ifdef USE_TRANSPARENCY
layout(binding = LINE_HIERARCHY_IMPORTANCE_MAP_BINDING) uniform sampler1DArray lineHierarchyImportanceMap;
#endif
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
layout(location = 9) flat in uint fragmentLineAppearanceOrder;
#endif
#ifdef USE_AMBIENT_OCCLUSION
layout(location = 10) in float fragmentVertexId;
float phi;
#endif

#if defined(DIRECT_BLIT_GATHER)
layout(location = 0) out vec4 fragColor;
#endif

#define M_PI 3.14159265358979323846

#include "TransferFunction.glsl"

#if !defined(DIRECT_BLIT_GATHER)
#include OIT_GATHER_HEADER
#endif

#define DEPTH_HELPER_USE_PROJECTION_MATRIX
#include "DepthHelper.glsl"
#include "Lighting.glsl"
#include "Antialiasing.glsl"

void main() {
#if defined(USE_LINE_HIERARCHY_LEVEL) && !defined(USE_TRANSPARENCY)
    float slider = lineHierarchySlider[fragmentPrincipalStressIndex];
    if (slider > fragmentLineHierarchyLevel) {
        discard;
    }
#endif
#ifdef VISUALIZE_SEEDING_PROCESS
    if (int(fragmentLineAppearanceOrder) > currentSeedIdx) {
        discard;
    }
#endif

    // Compute the normal of the billboard tube for shading.
    vec3 fragmentNormal;
    float interpolationFactor = fragmentNormalFloat;
    vec3 normalCos = normalize(normal0);
    vec3 normalSin = normalize(normal1);
    //if (interpolationFactor < 0.0) {
    //    normalSin = -normalSin;
    //    interpolationFactor = -interpolationFactor;
    //}
    float angle = asin(interpolationFactor);
    fragmentNormal = cos(angle) * normalCos + sin(angle) * normalSin;
#ifdef USE_AMBIENT_OCCLUSION
    phi = angle;
#endif

#ifdef USE_PRINCIPAL_STRESS_DIRECTION_INDEX
    vec4 fragmentColor = transferFunction(fragmentAttribute, fragmentPrincipalStressIndex);
#else
    vec4 fragmentColor = transferFunction(fragmentAttribute);
#endif

#if defined(USE_LINE_HIERARCHY_LEVEL) && defined(USE_TRANSPARENCY)
    fragmentColor.a *= texture(
            lineHierarchyImportanceMap, vec2(fragmentLineHierarchyLevel, float(fragmentPrincipalStressIndex))).r;
#endif

    fragmentColor = blinnPhongShadingTube(fragmentColor, fragmentNormal, fragmentTangent);

    float absCoords = abs(fragmentNormalFloat);
    float fragmentDepth = length(fragmentPositionWorld - cameraPosition);
    const float WHITE_THRESHOLD = 0.7;
    float EPSILON = clamp(getAntialiasingFactor(fragmentDepth / lineWidth * 2.0), 0.0, 0.49);
    float EPSILON_WHITE = fwidth(absCoords);
    float coverage = 1.0 - smoothstep(1.0 - EPSILON, 1.0, absCoords);
    //float coverage = 1.0 - smoothstep(1.0, 1.0, abs(fragmentNormalFloat));
    vec4 colorOut = vec4(mix(fragmentColor.rgb, foregroundColor.rgb,
            smoothstep(WHITE_THRESHOLD - EPSILON_WHITE, WHITE_THRESHOLD + EPSILON_WHITE, absCoords)),
            fragmentColor.a * coverage);

#include "LinePassGather.glsl"
}
