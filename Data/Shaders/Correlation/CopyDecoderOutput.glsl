/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2023, Christoph Neuhauser
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

-- Header

layout(local_size_x = BLOCK_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(binding = 0) uniform UniformBuffer {
    uint paddingFactor;
};

layout(binding = 1, std430) readonly buffer InputBuffer {
    real decodedValues[];
};

layout(binding = 2, std430) writeonly buffer OutputBuffer {
    float outputBuffer[];
};

layout(push_constant) uniform PushConstants {
    uint batchOffset, batchSize;
};


-- MutualInformation.Compute

#version 450 core

#import ".Header"

void main() {
    uint globalThreadIdx = gl_GlobalInvocationID.x;
    if (globalThreadIdx >= batchSize) {
        return;
    }
    outputBuffer[globalThreadIdx + batchOffset] = max(float(decodedValues[globalThreadIdx * paddingFactor]), 0.0);
}


-- CorrelationCoefficient.Compute

#version 450 core

#import ".Header"

void main() {
    uint globalThreadIdx = gl_GlobalInvocationID.x;
    if (globalThreadIdx >= batchSize) {
        return;
    }
    outputBuffer[globalThreadIdx + batchOffset] = float(decodedValues[globalThreadIdx * paddingFactor]);
}


-- CorrelationCoefficientAbs.Compute

#version 450 core

#import ".Header"

void main() {
    uint globalThreadIdx = gl_GlobalInvocationID.x;
    if (globalThreadIdx >= batchSize) {
        return;
    }
    outputBuffer[globalThreadIdx + batchOffset] = abs(float(decodedValues[globalThreadIdx * paddingFactor]));
}
