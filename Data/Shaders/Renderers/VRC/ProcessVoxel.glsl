/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2018 - 2021, Christoph Neuhauser
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

#ifndef PROCESS_VOXEL_GLSL
#define PROCESS_VOXEL_GLSL

#undef STRESS_LINE_DATA
#undef USE_AMBIENT_OCCLUSION
#define VOXEL_RAY_CASTING
#define cameraPosition cameraPositionVoxelGrid
#include "Lighting.glsl"
#undef cameraPosition

float distanceSqr(vec3 v0, vec3 v1) {
    return squareVec(v0 - v1);
}

struct RayHit {
    vec4 color;
    float distance;
    uint lineID;
};

void insertHitSorted(in RayHit insertHit, inout int numHits, inout RayHit hits[MAX_NUM_HITS]) {
    bool inserted = false;
    uint lineID = insertHit.lineID;
    int i;
    for (i = 0; i < numHits; i++) {
        if (insertHit.distance < hits[i].distance) {
            inserted = true;
            RayHit temp = insertHit;
            insertHit = hits[i];
            hits[i] = temp;
        }
        if (!inserted && hits[i].lineID == lineID) {
            return;
        }
        if (inserted && insertHit.lineID == lineID) {
            return;
        }
    }
    if (i != MAX_NUM_HITS) {
        hits[i] = insertHit;
        numHits++;
    }
}

void processLineSegment(
        vec3 rayOrigin, vec3 rayDirection, ivec3 centerVoxelIndex, ivec3 voxelIndex, LineSegment currVoxelLine,
        inout RayHit hits[MAX_NUM_HITS], inout int numHits, inout uint blendedLineIDs, inout uint newBlendedLineIDs) {
    vec3 centerVoxelPosMin = vec3(centerVoxelIndex);
    vec3 centerVoxelPosMax = vec3(centerVoxelIndex) + vec3(1.0);

    uint lineID = currVoxelLine.lineID;
    uint lineBit = 1u << lineID;
    if ((blendedLineIDs & lineBit) != 0u) {
        return;
    }

    vec3 tubePoint0 = currVoxelLine.v0, tubePoint1 = currVoxelLine.v1;

    bool hasIntersection = false;
    vec3 intersectionNormal = vec3(0.0);
    float intersectionAttribute = 0.0f;
    float intersectionDistance = 1e7;
    vec3 intersectionWorld = vec3(0);

    vec3 tubeIntersection, sphereIntersection0, sphereIntersection1;
    bool hasTubeIntersection, hasSphereIntersection0 = false, hasSphereIntersection1 = false;
    hasTubeIntersection = rayTubeIntersection(
            rayOrigin, rayDirection, tubePoint0, tubePoint1, lineRadius,
            tubeIntersection, centerVoxelPosMin, centerVoxelPosMax);
    if (hasTubeIntersection) {
        // Tube
        vec3 v = tubePoint1 - tubePoint0;
        vec3 u = tubeIntersection - tubePoint0;
        float t = dot(v, u) / dot(v, v);
        vec3 centerPt = tubePoint0 + t * v;
        intersectionAttribute = (1.0 - t) * currVoxelLine.a0 + t * currVoxelLine.a1;
        intersectionNormal =  normalize(tubeIntersection - centerPt);
        intersectionDistance = distanceSqr(rayOrigin, tubeIntersection);
        intersectionWorld = tubeIntersection;
        hasIntersection = true;
    } else {
        hasSphereIntersection0 = raySphereIntersection(
                rayOrigin, rayDirection, tubePoint0, lineRadius,
                sphereIntersection0, centerVoxelPosMin, centerVoxelPosMax);
        hasSphereIntersection1 = raySphereIntersection(
                rayOrigin, rayDirection, tubePoint1, lineRadius,
                sphereIntersection1, centerVoxelPosMin, centerVoxelPosMax);

        int closestIntersectionIdx = 0;
        vec3 intersection = tubeIntersection;
        intersectionWorld = tubeIntersection;
        float distSphere0 = distanceSqr(rayOrigin, sphereIntersection0);
        float distSphere1 = distanceSqr(rayOrigin, sphereIntersection1);

        if (hasSphereIntersection0 && distSphere0 < intersectionDistance) {
            closestIntersectionIdx = 0;
            intersection = sphereIntersection0;
            intersectionWorld = sphereIntersection0;
            intersectionDistance = distSphere0;
            hasIntersection = true;
        }
        if (hasSphereIntersection1 && distSphere1 < intersectionDistance) {
            closestIntersectionIdx = 1;
            intersection = sphereIntersection1;
            intersectionWorld = sphereIntersection1;
            intersectionDistance = distSphere1;
            hasIntersection = true;
        }

        vec3 sphereCenter;
        if (closestIntersectionIdx == 0) {
            sphereCenter = tubePoint0;
            intersectionAttribute = currVoxelLine.a0;
        } else {
            sphereCenter = tubePoint1;
            intersectionAttribute = currVoxelLine.a1;
        }
        intersectionNormal = normalize(intersection - sphereCenter);
    }


    if (hasIntersection) {
        vec4 intersectionColor = transferFunction(intersectionAttribute);

#ifdef USE_DEPTH_CUES
        vec3 screenSpacePosition = (viewMatrix * voxelSpaceToWorldSpace * vec4(intersectionWorld, 1.0)).xyz;
#endif

        const vec3 n = normalize(intersectionNormal);
        const vec3 v = normalize(cameraPositionVoxelGrid - intersectionWorld);
        const vec3 t = normalize(tubePoint1 - tubePoint0);
        intersectionColor = blinnPhongShadingTube(
                intersectionColor, intersectionWorld,
#ifdef USE_DEPTH_CUES
                screenSpacePosition,
#endif
                n, t);

        // Project v into plane perpendicular to t to get newV.
        vec3 helperVec = normalize(cross(t, v));
        vec3 newV = normalize(cross(helperVec, t));

        // Get the symmetric ribbon position (ribbon direction is perpendicular to line direction) between 0 and 1.
        // NOTE: len(cross(a, b)) == area of parallelogram spanned by a and b.
        vec3 crossProdVn = cross(newV, n);
        float ribbonPosition = length(crossProdVn);

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

        float absCoords = abs(ribbonPosition);
        const float WHITE_THRESHOLD = 0.7;
        //float EPSILON_OUTLINE = 0.0;//clamp(fragmentDepth * 0.0005 / lineWidth, 0.0, 0.49);
        //float EPSILON_WHITE = 0.0;//fwidth(ribbonPosition);
        //float coverage = 1.0 - smoothstep(1.0 - EPSILON_OUTLINE, 1.0, absCoords);
        intersectionColor = vec4(
                mix(intersectionColor.rgb, foregroundColor.rgb, smoothstep(WHITE_THRESHOLD, WHITE_THRESHOLD, absCoords)),
                intersectionColor.a);

        RayHit hit;
        hit.distance = intersectionDistance;
        hit.color = vec4(intersectionColor);
        hit.lineID = lineID;
        if (hit.color.a >= 1.0 / 255.0) {
            insertHitSorted(hit, numHits, hits);
            newBlendedLineIDs |= lineBit;
        }
    }
}

/**
 * Processes the intersections with the geometry of the voxel at "voxelIndex".
 * Returns the color of the voxel (or completely transparent color if no intersection with geometry stored in voxel).
 */
vec4 nextVoxel(
        vec3 rayOrigin, vec3 rayDirection, ivec3 voxelIndex, //ivec3 nextVoxelIndex,
        inout uint blendedLineIDs, inout uint newBlendedLineIDs) {
    RayHit hits[MAX_NUM_HITS];
    for (int i = 0; i < MAX_NUM_HITS; i++) {
        hits[i].color = vec4(0.0);
        hits[i].distance = 1e6;
        hits[i].lineID = -1;
    }
    int numHits = 0;

    //float distance = length(rayOrigin - vec3(voxelIndex));


    #define MAX_LINE_SEGMENTS_TO_TEST 32

    ivec3 currentVoxel;
    uint numLineSegments = 0;
    ivec3 voxelLocations[MAX_LINE_SEGMENTS_TO_TEST];
    uint voxelMemoryLocations[MAX_LINE_SEGMENTS_TO_TEST];
    /*for (int z = -1; z <= 1 && numLineSegments < MAX_LINE_SEGMENTS_TO_TEST; z++) {
        for (int y = -1; y <= 1 && numLineSegments < MAX_LINE_SEGMENTS_TO_TEST; y++) {
            for (int x = -1; x <= 1 && numLineSegments < MAX_LINE_SEGMENTS_TO_TEST; x++) {
                //if (x == 0 && y == 0 && z == 0) { continue; }
                ivec3 currentVoxel = voxelIndex + ivec3(x, y, z);

                if (any(lessThan(currentVoxel, ivec3(0))) || any(greaterThanEqual(currentVoxel, gridResolution))) {
                    continue;
                }

                uint currentVoxelIndex1D = getVoxelIndex1D(currentVoxel);
                uint currVoxelNumLines = getNumLinesInVoxel(currentVoxelIndex1D);
                uint voxelLineListOffset = getLineListOffset(currentVoxelIndex1D);

                for (uint i = 0; i < currVoxelNumLines; i++) {
                    voxelLocations[numLineSegments] = currentVoxel;
                    voxelMemoryLocations[numLineSegments] = voxelLineListOffset + i;
                    numLineSegments++;
                    if (numLineSegments == MAX_LINE_SEGMENTS_TO_TEST) {
                        break;
                    }
                }
            }
        }
    }*/
    for (int neighborIndex = 0; neighborIndex < 27 && numLineSegments < MAX_LINE_SEGMENTS_TO_TEST; neighborIndex++) {
        currentVoxel.x = voxelIndex.x + neighborIndex % 3 - 1;
        currentVoxel.y = voxelIndex.y + (neighborIndex / 3) % 3 - 1;
        currentVoxel.z = voxelIndex.z + neighborIndex / 9 - 1;

        if (any(lessThan(currentVoxel, ivec3(0))) || any(greaterThanEqual(currentVoxel, gridResolution))) {
            continue;
        }

        uint currentVoxelIndex1D = getVoxelIndex1D(currentVoxel);
        uint currVoxelNumLines = getNumLinesInVoxel(currentVoxelIndex1D);
        uint voxelLineListOffset = getLineListOffset(currentVoxelIndex1D);

        for (uint i = 0; i < currVoxelNumLines; i++) {
            voxelLocations[numLineSegments] = currentVoxel;
            voxelMemoryLocations[numLineSegments] = voxelLineListOffset + i;
            numLineSegments++;
            if (numLineSegments == MAX_LINE_SEGMENTS_TO_TEST) {
                break;
            }
        }
    }

    LineSegment currVoxelLine;
    for (int i = 0; i < numLineSegments; i++) {
        ivec3 currentVoxel = voxelLocations[i];
        uint voxelMemoryLocation = voxelMemoryLocations[i];
        decompressLine(currentVoxel, lineSegments[voxelMemoryLocation], currVoxelLine);
        processLineSegment(
                rayOrigin, rayDirection, voxelIndex, currentVoxel, currVoxelLine, hits, numHits,
                blendedLineIDs, newBlendedLineIDs);
    }

//#ifndef FAST_NEIGHBOR_SEARCH
    //if (isClose) {
    //    for (int z = -1; z <= 1; z++) {
    //        for (int y = -1; y <= 1; y++) {
    //            for (int x = -1; x <= 1; x++) {
    //                //if (x == 0 && y == 0 && z == 0) { continue; }
//
    //                ivec3 processedVoxelIndex = voxelIndex + ivec3(x,y,z);
    //                processVoxel(
    //                        rayOrigin, rayDirection, voxelIndex, processedVoxelIndex, isClose, hits, numHits,
    //                        blendedLineIDs, newBlendedLineIDs);
    //            }
    //        }
    //    }
    //}
//#else
//    if (isClose) {
//        vec3 entrancePoint, exitPoint;
//        vec3 lower = vec3(voxelIndex);
//        rayBoxIntersection(rayOrigin, rayDirection, lower, lower+vec3(1.0), entrancePoint, exitPoint);
//        vec3 voxelExit = exitPoint - vec3(voxelIndex);
//
//
//        int offsetTableCounter = 0;
//        ivec3 offsetTable[6];
//
//        if (voxelExit.x <= 0.2) {
//            offsetTable[offsetTableCounter++] = ivec3(-1, 0, 0);
//        }
//        if (voxelExit.x >= 0.8) {
//            offsetTable[offsetTableCounter++] = ivec3(1, 0, 0);
//        }
//        if (voxelExit.y <= 0.2) {
//            offsetTable[offsetTableCounter++] = ivec3(0, -1, 0);
//        }
//        if (voxelExit.y >= 0.8) {
//            offsetTable[offsetTableCounter++] = ivec3(0, 1, 0);
//        }
//        if (voxelExit.z <= 0.2) {
//            offsetTable[offsetTableCounter++] = ivec3(0, 0, -1);
//        }
//        if (voxelExit.z >= 0.8) {
//            offsetTable[offsetTableCounter++] = ivec3(0, 0, 1);
//        }
//
//        for (int i = 0; i < offsetTableCounter; i++) {
//            processVoxel(rayOrigin, rayDirection, voxelIndex, voxelIndex + offsetTable[i],
//            isClose, hits, numHits, blendedLineIDs, newBlendedLineIDs, currOpacity);
//        }
//    }
//#endif

    vec4 color = vec4(0.0);
    for (int i = 0; i < MAX_NUM_HITS; i++) {
        if (blend(hits[i].color, color)) {
            return color; // Early ray termination
        }
    }

    return color;
}

#endif
