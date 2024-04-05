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

-- Header

struct Node {
    vec3 worldSpaceAabbMin;
    uint indexCount;
    vec3 worldSpaceAabbMax;
    uint firstChildOrPrimitiveIndex; ///< Meshlet index instead of primitive index for mesh shader.
};

layout(std430, binding = 0) readonly buffer NodeBuffer {
    Node nodes[];
};

layout(std430, binding = 1) coherent buffer QueueBuffer {
    uint nodeIndicesQueue[];
};

layout(std430, binding = 2) coherent buffer QueueStateBuffer {
    uint queueLock;
    int numWorkPending;
    uint queueReadIdx;
    uint queueWriteIdx;
};

// The second queue stores the elements that need to be re-checked in the second pass.
#if !defined(RECHECK_OCCLUDED_ONLY)
layout(std430, binding = 3) buffer QueueBufferRecheck {
    uint nodeIndicesQueueRecheck[];
};

layout(std430, binding = 4) coherent buffer QueueStateBufferRecheck {
    uint queueLockRecheck;
    int numWorkPendingRecheck;
    uint queueReadIdxRecheck;
    uint queueWriteIdxRecheck;
};
#endif


-- Initialize.Compute

#version 450 core

layout(local_size_x = 1) in;

#include "VisibilityCulling.glsl"
#include "VisualizeBvhHierarchy.glsl"
#import ".Header"

void main() {
    if (gl_LocalInvocationID.x == 0) {
        Node rootNode = nodes[rootNodeIdx];
        if (visibilityCulling(rootNode.worldSpaceAabbMin, rootNode.worldSpaceAabbMax)) {
            numWorkPending = 1;
            queueWriteIdx = 1u;
            nodeIndicesQueue[0] = rootNodeIdx;
        } else {
            numWorkPending = 0;
            queueWriteIdx = 0u;
        }
        queueLock = 0u;
        queueReadIdx = 0u;

        queueLockRecheck = 0u;
        numWorkPendingRecheck = 0;
        queueReadIdxRecheck = 0u;
        queueWriteIdxRecheck = 0u;

#ifdef VISUALIZE_BVH_HIERARCHY
        numNodeAabbs = 0u;
#endif
    }
}


-- Traverse.Compute

#version 450 core

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

layout(local_size_x = WORKGROUP_SIZE) in;

#include "VisibilityCulling.glsl"
#include "VisualizeBvhHierarchy.glsl"
#import ".Header"

// Buffers passed to vkCmdDrawIndexedIndirectCount.
#ifdef OUTPUT_DRAW_INDEXED_INDIRECT
struct VkDrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
layout(std430, binding = 5) writeonly buffer DrawIndexedIndirectCommandBuffer {
    VkDrawIndexedIndirectCommand commands[];
};
#elif defined(OUTPUT_MESH_SHADER)
layout(std430, binding = 5) writeonly buffer VisibleMeshletIndexArrayBuffer {
    uint visibleMeshletIndexArray[];
};
#endif

layout(std430, binding = 11) buffer IndirectDrawCountBuffer {
    uint drawCount;
};
layout(std430, binding = 13) buffer TestBuffer {
    int maxLeftWork;
};
layout(binding = 14) uniform QueueInfoBuffer {
    uint queueSize;
};

#ifdef VISUALIZE_BVH_HIERARCHY
layout(std430, binding = 17) readonly buffer NodeIdxToTreeHeightBuffer {
    uint nodeIdxToTreeHeightMap[];
};
#endif

#if WORKGROUP_SIZE != 1 && WORKGROUP_SIZE > SUBGROUP_SIZE
shared uint numWorkShared;
shared uint workIdxShared;
#endif

#define INVALID_TASK 0xFFFFFFFFu

// For debugging purposes: Stop if we have exceeded a maximum number of iterations (i.e., the invocation has hung).
#define LOOP_ITERATION_CHECK

void main() {
    /*
     * Uses persistent thread model.
     */
    uint workIdx = INVALID_TASK;
    uint nodeIdx = INVALID_TASK;
    int maxNumIteration = 100000;
    int it = 0;
    //bool hasChildrenToWrite = false;
    uint childIdx0, childIdx1;
    while (true) {
#ifdef LOOP_ITERATION_CHECK
        it++;
        if (it > maxNumIteration) {
            atomicMax(maxLeftWork, it);
            break;
        }
#endif

        if (workIdx == INVALID_TASK) {
#if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
            workIdx = atomicAdd(queueReadIdx, 1u);
#else
            uint numActiveInvocationsFetchTask = subgroupAdd(1u);
            if (subgroupElect()) {
                workIdx = atomicAdd(queueReadIdx, numActiveInvocationsFetchTask);
            }
            workIdx = subgroupBroadcastFirst(workIdx) + subgroupExclusiveAdd(1u);
#endif
        }
#ifndef USE_RINGBUFFER
        if (workIdx >= queueSize) {
#ifdef LOOP_ITERATION_CHECK
            atomicMax(maxLeftWork, it);
#endif
            break;
        }
#endif

#ifdef USE_RINGBUFFER
        workIdx = workIdx % queueSize;
#endif

        nodeIdx = nodeIndicesQueue[workIdx];
        if (nodeIdx != INVALID_TASK) {
            nodeIndicesQueue[workIdx] = INVALID_TASK;
            Node node = nodes[nodeIdx];

            if (visibilityCulling(node.worldSpaceAabbMin, node.worldSpaceAabbMax)) {
#ifdef VISUALIZE_BVH_HIERARCHY
#if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
                uint writePositionNodeAabb = atomicAdd(numNodeAabbs, 1u);
#else
                uint numActiveInvocationsVisibleNode = subgroupAdd(1u);
                uint writePositionNodeAabb;
                if (subgroupElect()) {
                    writePositionNodeAabb = atomicAdd(numNodeAabbs, numActiveInvocationsVisibleNode);
                }
                writePositionNodeAabb = subgroupBroadcastFirst(writePositionNodeAabb) + subgroupExclusiveAdd(1u);
#endif
                NodeAabb nodeAabb;
                nodeAabb.worldSpaceAabbMin = node.worldSpaceAabbMin;
                nodeAabb.worldSpaceAabbMax = node.worldSpaceAabbMax;
                nodeAabb.normalizedHierarchyLevel = float(nodeIdxToTreeHeightMap[nodeIdx]) / float(treeHeight - 1);
#ifdef RECHECK_OCCLUDED_ONLY
                nodeAabb.passIdx = 1u;
#else
                nodeAabb.passIdx = 0u;
#endif
                nodeAabbs[writePositionNodeAabb] = nodeAabb;
#endif

                if (node.indexCount != 0u) {
#ifdef OUTPUT_DRAW_INDEXED_INDIRECT
#if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
                    uint writePosition = atomicAdd(drawCount, 1u);
#else
                    uint numActiveInvocationsLeaf = subgroupAdd(1u);
                    uint writePosition;
                    if (subgroupElect()) {
                        writePosition = atomicAdd(drawCount, numActiveInvocationsLeaf);
                    }
                    writePosition = subgroupBroadcastFirst(writePosition) + subgroupExclusiveAdd(1u);
#endif
                    VkDrawIndexedIndirectCommand cmd;
                    cmd.indexCount = node.indexCount;
                    cmd.instanceCount = 1u;
                    cmd.firstIndex = node.firstChildOrPrimitiveIndex;
                    cmd.vertexOffset = 0;
                    cmd.firstInstance = 0u;
                    commands[writePosition] = cmd;
#elif defined(OUTPUT_MESH_SHADER)
#if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
                    uint writePosition = atomicAdd(drawCount, node.indexCount);
#else
                    uint numActiveInvocationsLeaf = subgroupAdd(node.indexCount);
                    uint writePosition;
                    if (subgroupElect()) {
                        writePosition = atomicAdd(drawCount, numActiveInvocationsLeaf);
                    }
                    writePosition = subgroupBroadcastFirst(writePosition) + subgroupExclusiveAdd(node.indexCount);
#endif
                    for (uint i = 0; i < node.indexCount; i++) {
                        visibleMeshletIndexArray[writePosition + i] = node.firstChildOrPrimitiveIndex + i;
                    }
#endif
                } else {
                    childIdx0 = node.firstChildOrPrimitiveIndex;
                    childIdx1 =  childIdx0 % 2 == 1 ? childIdx0 + 1 : childIdx0 - 1;//node.firstChildOrPrimitiveIndex + 1;
 #if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
                    atomicAdd(numWorkPending, 2);
                    // This is an inner node. Enqueue all children.
                    uint queueIdx = atomicAdd(queueWriteIdx, 2u);
#else
                    uint numChildren = subgroupAdd(2u);
                    uint queueIdx;
                    if (subgroupElect()) {
                        atomicAdd(numWorkPending, int(numChildren));
                        queueIdx = atomicAdd(queueWriteIdx, numChildren);
                    }
                    queueIdx = subgroupBroadcastFirst(queueIdx) + subgroupExclusiveAdd(2u);
#endif
#ifdef USE_RINGBUFFER
                    queueIdx = queueIdx % queueSize;
#endif
                    nodeIndicesQueue[queueIdx] = childIdx0;
                    nodeIndicesQueue[queueIdx + 1u] = childIdx1;
                }
            }
#if !defined(RECHECK_OCCLUDED_ONLY)
            else {
                // Add node to queue that gets re-checked later.
#if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
                uint writeIdxRecheck = atomicAdd(queueWriteIdxRecheck, 1u);
                atomicAdd(numWorkPendingRecheck, 1);
#else
                uint numActiveInvocationsRecheck = subgroupAdd(1u);
                uint writeIdxRecheck;
                if (subgroupElect()) {
                    atomicAdd(numWorkPendingRecheck, int(numActiveInvocationsRecheck));
                    writeIdxRecheck = atomicAdd(queueWriteIdxRecheck, numActiveInvocationsRecheck);
                }
                writeIdxRecheck = subgroupBroadcastFirst(writeIdxRecheck) + subgroupExclusiveAdd(1u);
#endif
#ifdef USE_RINGBUFFER
                writeIdxRecheck = writeIdxRecheck % queueSize;
#endif
                nodeIndicesQueueRecheck[writeIdxRecheck] = nodeIdx;
            }
#endif

#if WORKGROUP_SIZE == 1 || !defined(USE_SUBGROUP_OPS)
            atomicAdd(numWorkPending, -1);
#else
            int numActiveInvocations = subgroupAdd(-1);
            if (subgroupElect()) {
                atomicAdd(numWorkPending, numActiveInvocations);
            }
#endif
            workIdx = INVALID_TASK;
        }

        memoryBarrierBuffer();
        if (numWorkPending == 0) {
#ifdef LOOP_ITERATION_CHECK
            atomicMax(maxLeftWork, it);
#endif
            break;
        }
    }
}


-- TraverseSpinlock.Compute

#version 450 core

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

layout(local_size_x = WORKGROUP_SIZE) in;

#include "VisibilityCulling.glsl"
#import ".Header"

// Buffers passed to vkCmdDrawIndexedIndirectCount.
struct VkDrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
// Uses stride of 8 bytes, or scalar layout otherwise.
layout(std430, binding = 5) writeonly buffer DrawIndexedIndirectCommandBuffer {
    VkDrawIndexedIndirectCommand commands[];
};
layout(std430, binding = 11) buffer IndirectDrawCountBuffer {
    uint drawCount;
};
layout(std430, binding = 13) buffer TestBuffer {
    int maxLeftWork;
};

#if WORKGROUP_SIZE != 1 && WORKGROUP_SIZE > SUBGROUP_SIZE
shared uint numWorkShared;
shared uint workIdxShared;
#endif

void main() {
    /*
     * Uses persistent thread model.
     */
#if WORKGROUP_SIZE != 1
    uint numWork = 0u;
#endif
    uint workIdx = 0u;
    int maxNumIteration = 100000;
    int it = 0;
    //bool hasChildrenToWrite = false;
    uint childIdx0, childIdx1;
    while (true) {
        it++;
        if (it > maxNumIteration) {
            atomicMax(maxLeftWork, it);
            break;
        }
        bool isItemValid = false;
#if WORKGROUP_SIZE == 1
        if (atomicCompSwap(queueLock, 0u, 1u) == 0u) {
            if (queueReadIdx < queueWriteIdx) {
                workIdx = atomicAdd(queueReadIdx, 1u);
                isItemValid = true;
            }
            atomicExchange(queueLock, 0u);
        }
#elif WORKGROUP_SIZE <= SUBGROUP_SIZE
        uint threadWritesChild = 0u;
        numWork = 0u;
        if (subgroupElect()) {
            if (atomicCompSwap(queueLock, 0u, 1u) == 0u) {
                if (queueReadIdx < queueWriteIdx) {
                    numWork = min(queueWriteIdx - queueReadIdx, WORKGROUP_SIZE);
                    workIdx = atomicAdd(queueReadIdx, numWork);
                }
                atomicExchange(queueLock, 0u);
            }
        }
        numWork = subgroupBroadcastFirst(numWork);
        workIdx = subgroupBroadcastFirst(workIdx);
        if (gl_SubgroupInvocationID < numWork) {
            workIdx += gl_SubgroupInvocationID;
            isItemValid = true;
        }
#else
        if (gl_LocalInvocationID.x == 0u) {
            numWorkShared = 0u;
            if (atomicCompSwap(queueLock, 0u, 1u) == 0u) {
                if (queueReadIdx < queueWriteIdx) {
                    numWorkShared = min(queueWriteIdx - queueReadIdx, gl_WorkGroupSize.x);
                    workIdxShared = atomicAdd(queueReadIdx, numWork);
                }
                atomicExchange(queueLock, 0u);
            }
        }
        memoryBarrierShared();
        barrier();
        numWork = numWorkShared;
        if (gl_LocalInvocationID.x < numWork) {
            workIdx = workIdxShared + gl_LocalInvocationID.x;
            isItemValid = true;
        }
#endif
        memoryBarrierBuffer();

        if (!isItemValid) {
#if WORKGROUP_SIZE != 1
            if (numWorkPending == 0 && numWork == 0 /* && !hasChildrenToWrite */)
#else
            if (numWorkPending == 0 /* && !hasChildrenToWrite */)
#endif
            {
                atomicMax(maxLeftWork, it);
                return;
            }
            continue;
        }

        // Get the node.
        uint nodeIdx = nodeIndicesQueue[workIdx];
        Node node = nodes[nodeIdx];

        //int numWorkPendingChange = -1;
        if (visibilityCulling(node.worldSpaceAabbMin, node.worldSpaceAabbMax)) {
            if (node.indexCount != 0u) {
                uint writePosition = atomicAdd(drawCount, 1u);
                VkDrawIndexedIndirectCommand cmd;
                cmd.indexCount = node.indexCount;
                cmd.instanceCount = 1u;
                cmd.firstIndex = node.firstChildOrPrimitiveIndex;
                cmd.vertexOffset = 0;
                cmd.firstInstance = 0u;
                commands[writePosition] = cmd;
            } else {
                childIdx0 = node.firstChildOrPrimitiveIndex;
                childIdx1 =  childIdx0 % 2 == 1 ? childIdx0 + 1 : childIdx0 - 1;//node.firstChildOrPrimitiveIndex + 1;
                //numWorkPendingChange += 2;
                //hasChildrenToWrite = true;
                // This is an inner node. Enqueue all children.
                //uint childIdx0 = node.firstChildOrPrimitiveIndex;
                //uint childIdx1 = node.firstChildOrPrimitiveIndex + 1;
                //uint childIdx1 = childIdx0 % 2 == 1 ? childIdx0 + 1 : childIdx0 - 1;
#if WORKGROUP_SIZE == 1
                while (true) {
                    if (atomicCompSwap(queueLock, 0u, 1u) == 0u) {
                        uint queueIdx = atomicAdd(queueWriteIdx, 2u);
                        nodeIndicesQueue[queueIdx] = childIdx0;
                        nodeIndicesQueue[queueIdx + 1u] = childIdx1;
                        atomicAdd(numWorkPending, 2);

                        atomicExchange(queueLock, 0u);
                        break;
                    }
                }
#elif WORKGROUP_SIZE <= SUBGROUP_SIZE
                bool hasLock = false;
                threadWritesChild = 2u;
                uint numWrite = subgroupAdd(threadWritesChild);
                uint writeOffset = subgroupExclusiveAdd(threadWritesChild);
                uint queueIdx;
                do {
                    if (subgroupElect()) {
                        if (atomicCompSwap(queueLock, 0u, 1u) == 0u) {
                            hasLock = true;
                            queueIdx = atomicAdd(queueWriteIdx, numWrite);
                            atomicAdd(numWorkPending, int(numWrite));
                        }
                    }
                    hasLock = subgroupBroadcastFirst(hasLock);
                } while (!hasLock);
                queueIdx = subgroupBroadcastFirst(queueIdx);
                uint writePos = queueIdx + writeOffset;
                nodeIndicesQueue[writePos] = childIdx0;
                nodeIndicesQueue[writePos + 1u] = childIdx1;
                if (subgroupElect()) {
                    atomicExchange(queueLock, 0u);
                }
                memoryBarrierBuffer();
#endif
            }
        }
#if !defined(RECHECK_OCCLUDED_ONLY)
        else {
            // Add node to queue that gets re-checked later.
            uint writeIdxRecheck = atomicAdd(queueWriteIdxRecheck, 1u);
            atomicAdd(numWorkPendingRecheck, 1);
            nodeIndicesQueueRecheck[writeIdxRecheck] = nodeIdx;
        }
#endif

        //atomicAdd(numWorkPending, numWorkPendingChange);
        atomicAdd(numWorkPending, -1);
        //memoryBarrierBuffer();
    }
}
