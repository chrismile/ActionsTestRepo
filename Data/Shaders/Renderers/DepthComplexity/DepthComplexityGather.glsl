/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018 - 2022, Christoph Neuhauser
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

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexTangent;

struct LinePointData {
    vec3 vertexPosition;
    float vertexAttribute;
    vec3 vertexTangent;
    float padding;
};

layout (std430, binding = LINE_POINTS_BUFFER_BINDING) buffer LinePoints {
    LinePointData linePoints[];
};

void main() {
    uint pointIndex = gl_VertexIndex / 2;
    LinePointData linePointData = linePoints[pointIndex];
    vec3 linePoint = (mMatrix * vec4(linePointData.vertexPosition, 1.0)).xyz;

    vec3 viewDirection = normalize(cameraPosition - linePoint);
    vec3 offsetDirection = normalize(cross(viewDirection, normalize(linePointData.vertexTangent)));
    vec3 vertexPosition;
    float shiftSign = 1.0f;
    if (gl_VertexIndex % 2 == 0) {
        shiftSign = -1.0;
    }
    vertexPosition = linePoint + shiftSign * lineWidth * 0.5 * offsetDirection;

    //screenSpacePosition = (vMatrix * vec4(vertexPosition, 1.0)).xyz;
    gl_Position = pMatrix * vMatrix * vec4(vertexPosition, 1.0);
}

-- VBO.Vertex

#version 450 core

#include "LineUniformData.glsl"
#include "TransferFunction.glsl"

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexTangent;

layout(location = 0) out vec3 linePosition;
layout(location = 1) out vec3 lineTangent;

void main() {
    linePosition = (mMatrix * vec4(vertexPosition, 1.0)).xyz;
    lineTangent = vertexTangent;
    gl_Position = mvpMatrix * vec4(vertexPosition, 1.0);
}

-- VBO.Geometry

#version 450 core

layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

#include "LineUniformData.glsl"

layout(location = 0) in vec3 linePosition[];
layout(location = 1) in vec3 lineTangent[];

void main() {
    vec3 linePosition0 = linePosition[0];
    vec3 linePosition1 = linePosition[1];

    vec3 viewDirection0 = normalize(cameraPosition - linePosition0);
    vec3 viewDirection1 = normalize(cameraPosition - linePosition1);
    vec3 offsetDirection0 = normalize(cross(viewDirection0, normalize(lineTangent[0])));
    vec3 offsetDirection1 = normalize(cross(viewDirection1, normalize(lineTangent[1])));
    vec3 vertexPosition;

    const float lineRadius = lineWidth * 0.5;
    const mat4 pvMatrix = pMatrix * vMatrix;

    vertexPosition = linePosition0 - lineRadius * offsetDirection0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    vertexPosition = linePosition1 - lineRadius * offsetDirection1;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    vertexPosition = linePosition0 + lineRadius * offsetDirection0;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    vertexPosition = linePosition1 + lineRadius * offsetDirection1;
    gl_Position = pvMatrix * vec4(vertexPosition, 1.0);
    EmitVertex();

    EndPrimitive();
}

-- Fragment

#version 450 core

#include "DepthComplexityHeader.glsl"

void main() {
    int x = int(gl_FragCoord.x);
    int y = int(gl_FragCoord.y);
    uint pixelIndex = addrGen(uvec2(x,y));
    atomicAdd(fragmentCounterBuffer[pixelIndex], 1u);
}
