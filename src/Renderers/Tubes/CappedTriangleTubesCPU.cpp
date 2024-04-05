/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2020, Christoph Neuhauser
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

#include <Math/Math.hpp>
#include <Utils/File/Logfile.hpp>
#include "Tubes.hpp"

void addHemisphereToMeshStart(
        const glm::vec3& center, glm::vec3 tangent, glm::vec3 normal,
        uint32_t indexOffset, uint32_t indexOffsetCap, uint32_t triOffsetCap,
        uint32_t vertexLinePointIndex, float tubeRadius, int numLongitudeSubdivisions, int numLatitudeSubdivisions,
        std::vector<uint32_t>& triangleIndices, std::vector<TubeTriangleVertexData>& vertexDataList) {
    glm::vec3 binormal = glm::cross(normal, tangent);
    glm::vec3 scaledTangent = tubeRadius * tangent;
    glm::vec3 scaledNormal = tubeRadius * normal;
    glm::vec3 scaledBinormal = tubeRadius * binormal;

    float theta; // azimuth;
    float phi; // zenith;

    uint32_t vertexOffsetCap = indexOffsetCap;
    for (int lat = numLatitudeSubdivisions; lat >= 1; lat--) {
        phi = sgl::HALF_PI * (1.0f - float(lat) / float(numLatitudeSubdivisions));
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            theta = sgl::TWO_PI * float(lon) / float(numLongitudeSubdivisions);

            glm::vec3 pt(
                    std::cos(theta) * std::sin(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(phi)
            );

            glm::vec3 transformedPoint(
                    pt.x * scaledNormal.x + pt.y * scaledBinormal.x + pt.z * scaledTangent.x + center.x,
                    pt.x * scaledNormal.y + pt.y * scaledBinormal.y + pt.z * scaledTangent.y + center.y,
                    pt.x * scaledNormal.z + pt.y * scaledBinormal.z + pt.z * scaledTangent.z + center.z
            );
            glm::vec3 vertexNormal = glm::normalize(glm::vec3(
                    pt.x * scaledNormal.x + pt.y * scaledBinormal.x + pt.z * scaledTangent.x,
                    pt.x * scaledNormal.y + pt.y * scaledBinormal.y + pt.z * scaledTangent.y,
                    pt.x * scaledNormal.z + pt.y * scaledBinormal.z + pt.z * scaledTangent.z
            ));

            TubeTriangleVertexData tubeTriangleVertexData{};
            tubeTriangleVertexData.vertexPosition = transformedPoint;
            tubeTriangleVertexData.vertexLinePointIndex = vertexLinePointIndex | 0x80000000u;
            tubeTriangleVertexData.vertexNormal = vertexNormal;
            tubeTriangleVertexData.phi = theta;
            vertexDataList.at(vertexOffsetCap++) = tubeTriangleVertexData;

            if (lat == numLatitudeSubdivisions) {
                break;
            }
        }
    }
    for (int lat = 0; lat < numLatitudeSubdivisions; lat++) {
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            if (lat > 0) {
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions
                        + (lat-1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat-1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat-1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
            } else {
                triangleIndices.at(triOffsetCap++) = indexOffsetCap;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions;
            }
        }
    }
}

void addHemisphereToMeshStop(
        const glm::vec3& center, glm::vec3 tangent, glm::vec3 normal,
        uint32_t indexOffset, uint32_t indexOffsetCap, uint32_t triOffsetCap,
        uint32_t vertexLinePointIndex, float tubeRadius, int numLongitudeSubdivisions, int numLatitudeSubdivisions,
        std::vector<uint32_t>& triangleIndices, std::vector<TubeTriangleVertexData>& vertexDataList) {
    glm::vec3 binormal = glm::cross(normal, tangent);
    glm::vec3 scaledTangent = tubeRadius * tangent;
    glm::vec3 scaledNormal = tubeRadius * normal;
    glm::vec3 scaledBinormal = tubeRadius * binormal;

    float theta; // azimuth;
    float phi; // zenith;

    uint32_t vertexIndexOffset = indexOffsetCap - indexOffset - numLongitudeSubdivisions;
    for (int lat = 1; lat <= numLatitudeSubdivisions; lat++) {
        phi = sgl::HALF_PI * (1.0f - float(lat) / float(numLatitudeSubdivisions));
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            theta = -sgl::TWO_PI * float(lon) / float(numLongitudeSubdivisions);

            glm::vec3 pt(
                    std::cos(theta) * std::sin(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(phi)
            );

            glm::vec3 transformedPoint(
                    pt.x * scaledNormal.x + pt.y * scaledBinormal.x + pt.z * scaledTangent.x + center.x,
                    pt.x * scaledNormal.y + pt.y * scaledBinormal.y + pt.z * scaledTangent.y + center.y,
                    pt.x * scaledNormal.z + pt.y * scaledBinormal.z + pt.z * scaledTangent.z + center.z
            );
            glm::vec3 vertexNormal = glm::normalize(glm::vec3(
                    pt.x * scaledNormal.x + pt.y * scaledBinormal.x + pt.z * scaledTangent.x,
                    pt.x * scaledNormal.y + pt.y * scaledBinormal.y + pt.z * scaledTangent.y,
                    pt.x * scaledNormal.z + pt.y * scaledBinormal.z + pt.z * scaledTangent.z
            ));

            TubeTriangleVertexData tubeTriangleVertexData{};
            tubeTriangleVertexData.vertexPosition = transformedPoint;
            tubeTriangleVertexData.vertexLinePointIndex = vertexLinePointIndex | 0x80000000u;
            tubeTriangleVertexData.vertexNormal = vertexNormal;
            tubeTriangleVertexData.phi = -theta;
            vertexDataList.at(indexOffsetCap++) = tubeTriangleVertexData;

            if (lat == numLatitudeSubdivisions) {
                break;
            }
        }
    }
    for (int lat = 0; lat < numLatitudeSubdivisions; lat++) {
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            if (lat < numLatitudeSubdivisions-1) {
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat+1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat+1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat+1)*numLongitudeSubdivisions;
            } else {
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + 0
                        + (lat+1)*numLongitudeSubdivisions;
            }
        }
    }
}

void createCappedTriangleTubesRenderDataCPU(
        const std::vector<std::vector<glm::vec3>>& lineCentersList,
        float tubeRadius, int numCircleSubdivisions, bool tubeClosed,
        std::vector<uint32_t>& triangleIndices,
        std::vector<TubeTriangleVertexData>& vertexDataList,
        std::vector<LinePointReference>& linePointReferenceList,
        uint32_t linePointOffset,
        std::vector<glm::vec3>& lineTangents,
        std::vector<glm::vec3>& lineNormals) {
    numCircleSubdivisions = std::max(numCircleSubdivisions, 4);
    if (size_t(numCircleSubdivisions) != globalCircleVertexPositions.size() || tubeRadius != globalTubeRadius) {
        initGlobalCircleVertexPositions(numCircleSubdivisions, tubeRadius);
    }

    /*
     * If the tube is open, close it with two hemisphere caps at the ends.
     */
    int numLongitudeSubdivisions = numCircleSubdivisions; // azimuth
    int numLatitudeSubdivisions = int(std::ceil(numCircleSubdivisions / 2)); // zenith
    uint32_t numCapVertices =
            numLongitudeSubdivisions * (numLatitudeSubdivisions - 1) + 1;
    uint32_t numCapIndices =
            numLongitudeSubdivisions * (numLatitudeSubdivisions - 1) * 6 + numLongitudeSubdivisions * 3;

    for (size_t lineId = 0; lineId < lineCentersList.size(); lineId++) {
        const std::vector<glm::vec3>& lineCenters = lineCentersList.at(lineId);
        size_t n = lineCenters.size();
        auto lineIndexOffset = uint32_t(lineTangents.size());

        // Assert that we have a valid input data range
        if (tubeClosed && n < 3) {
            continue;
        }
        if (!tubeClosed && n < 2) {
            continue;
        }

        auto indexOffsetCapStart = uint32_t(vertexDataList.size());
        auto triOffsetCapStart = uint32_t(triangleIndices.size());
        if (!tubeClosed) {
            vertexDataList.resize(vertexDataList.size() + numCapVertices);
            triangleIndices.resize(triangleIndices.size() + numCapIndices);
        }
        auto indexOffset = uint32_t(vertexDataList.size());

        glm::vec3 lastLineNormal(1.0f, 0.0f, 0.0f);
        int firstIdx = int(n) - 2;
        int lastIdx = 1;
        int numValidLinePoints = 0;
        for (size_t i = 0; i < n; i++) {
            glm::vec3 tangent;
            if (!tubeClosed && i == 0) {
                tangent = lineCenters[i + 1] - lineCenters[i];
            } else if (!tubeClosed && i == n - 1) {
                tangent = lineCenters[i] - lineCenters[i - 1];
            } else {
                tangent = (lineCenters[(i + 1) % n] - lineCenters[(i + n - 1) % n]);
            }
            float lineSegmentLength = glm::length(tangent);

            if (lineSegmentLength < 0.0001f) {
                // In case the two vertices are almost identical, just skip this path line segment
                continue;
            }
            firstIdx = std::min(int(i), firstIdx);
            lastIdx = std::max(int(i), lastIdx);
            tangent = glm::normalize(tangent);

            insertOrientedCirclePoints(
                    lineCenters.at(i), tangent, lastLineNormal,
                    linePointOffset + uint32_t(linePointReferenceList.size()),
                    vertexDataList);
            lineTangents.push_back(tangent);
            lineNormals.push_back(lastLineNormal);
            linePointReferenceList.emplace_back(lineId, i);
            numValidLinePoints++;
        }

        if (numValidLinePoints == 1) {
            // Only one vertex left -> output nothing (tube consisting only of one point).
            vertexDataList.resize(indexOffsetCapStart);
            linePointReferenceList.pop_back();
            lineTangents.pop_back();
            lineNormals.pop_back();
        }
        if (numValidLinePoints <= 1) {
            continue;
        }

        for (int i = 0; i < numValidLinePoints-1; i++) {
            for (int j = 0; j < numCircleSubdivisions; j++) {
                // Build two CCW triangles (one quad) for each side
                // Triangle 1
                triangleIndices.push_back(
                        indexOffset + i*numCircleSubdivisions+j);
                triangleIndices.push_back(
                        indexOffset + i*numCircleSubdivisions+(j+1)%numCircleSubdivisions);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numCircleSubdivisions+(j+1)%numCircleSubdivisions);

                // Triangle 2
                triangleIndices.push_back(
                        indexOffset + i*numCircleSubdivisions+j);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numCircleSubdivisions+(j+1)%numCircleSubdivisions);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numCircleSubdivisions+j);
            }
        }

        auto indexOffsetCapEnd = uint32_t(vertexDataList.size());
        auto triOffsetCapEnd = uint32_t(triangleIndices.size());
        if (!tubeClosed) {
            vertexDataList.resize(vertexDataList.size() + numCapVertices);
            triangleIndices.resize(triangleIndices.size() + numCapIndices);
        }

        if (tubeClosed) {
            /*
             * The tube is supposed to be closed. However, as we iteratively construct an artificial normal for
             * each line point perpendicular to the approximated line tangent, the normals at the begin and the
             * end of the tube do not match (i.e. the normal is not continuous).
             * Thus, the idea is to connect the begin and the end of the tube in such a way that the length of
             * the connecting edges is minimized. This is done by computing the angle between the two line
             * normals and shifting the edge indices by a necessary offset.
             */
            glm::vec3 normalA = lineNormals[lineIndexOffset + numValidLinePoints - 1];
            glm::vec3 normalB = lineNormals[lineIndexOffset];
            float normalAngleDifference = std::atan2(
                    glm::length(glm::cross(normalA, normalB)), glm::dot(normalA, normalB));
            normalAngleDifference = std::fmod(normalAngleDifference + sgl::TWO_PI, sgl::TWO_PI);
            int jOffset = int(std::round(normalAngleDifference / (sgl::TWO_PI) * float(numCircleSubdivisions)));
            for (int j = 0; j < numCircleSubdivisions; j++) {
                // Build two CCW triangles (one quad) for each side
                // Triangle 1
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numCircleSubdivisions+(j)%numCircleSubdivisions);
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numCircleSubdivisions+(j+1)%numCircleSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numCircleSubdivisions+(j+1+jOffset)%numCircleSubdivisions);

                // Triangle 2
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numCircleSubdivisions+(j)%numCircleSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numCircleSubdivisions+(j+1+jOffset)%numCircleSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numCircleSubdivisions+(j+jOffset)%numCircleSubdivisions);
            }
        } else {
            // Hemisphere at the start
            glm::vec3 center0 = lineCenters[firstIdx];
            glm::vec3 tangent0 = lineCenters[firstIdx] - lineCenters[firstIdx + 1];
            tangent0 = glm::normalize(tangent0);
            glm::vec3 normal0 = lineNormals[lineIndexOffset];

            // Hemisphere at the end
            glm::vec3 center1 = lineCenters[lastIdx];
            glm::vec3 tangent1 = lineCenters[lastIdx] - lineCenters[lastIdx - 1];
            tangent1 = glm::normalize(tangent1);
            glm::vec3 normal1 = lineNormals[lineIndexOffset + numValidLinePoints - 1];

            addHemisphereToMeshStart(
                    center0, tangent0, normal0, indexOffset, indexOffsetCapStart, triOffsetCapStart,
                    linePointOffset + uint32_t(lineIndexOffset),
                    tubeRadius, numLongitudeSubdivisions, numLatitudeSubdivisions,
                    triangleIndices, vertexDataList);
            addHemisphereToMeshStop(
                    center1, tangent1, normal1, indexOffset, indexOffsetCapEnd, triOffsetCapEnd,
                    linePointOffset + uint32_t(lineTangents.size() - 1),
                    tubeRadius, numLongitudeSubdivisions, numLatitudeSubdivisions,
                    triangleIndices, vertexDataList);
        }
    }
}



void addEllipticHemisphereToMeshStart(
        const glm::vec3& center, glm::vec3 tangent, glm::vec3 normal,
        uint32_t indexOffset, uint32_t indexOffsetCap, uint32_t triOffsetCap,
        uint32_t vertexLinePointIndex, float tubeNormalRadius, float tubeBinormalRadius,
        int numLongitudeSubdivisions, int numLatitudeSubdivisions,
        std::vector<uint32_t>& triangleIndices, std::vector<TubeTriangleVertexData>& vertexDataList) {
    glm::vec3 binormal = glm::cross(normal, tangent);
    glm::vec3 scaledTangent = std::min(tubeNormalRadius, tubeBinormalRadius) * tangent;
    glm::vec3 scaledNormal = tubeNormalRadius * normal;
    glm::vec3 scaledBinormal = tubeBinormalRadius * binormal;

    float theta; // azimuth;
    float phi; // zenith;

    glm::mat3 frameMatrix(scaledNormal, scaledBinormal, scaledTangent);
    glm::mat3 normalFrameMatrix = glm::transpose(glm::inverse(frameMatrix));

    uint32_t vertexOffsetCap = indexOffsetCap;
    for (int lat = numLatitudeSubdivisions; lat >= 1; lat--) {
        phi = sgl::HALF_PI * (1.0f - float(lat) / float(numLatitudeSubdivisions));
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            theta = sgl::TWO_PI * float(lon) / float(numLongitudeSubdivisions);

            glm::vec3 pt(
                    std::cos(theta) * std::sin(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(phi)
            );

            glm::vec3 transformedPoint(
                    pt.x * scaledNormal.x + pt.y * scaledBinormal.x + pt.z * scaledTangent.x + center.x,
                    pt.x * scaledNormal.y + pt.y * scaledBinormal.y + pt.z * scaledTangent.y + center.y,
                    pt.x * scaledNormal.z + pt.y * scaledBinormal.z + pt.z * scaledTangent.z + center.z
            );
            glm::vec3 vertexNormal = glm::normalize(normalFrameMatrix * pt);

            TubeTriangleVertexData tubeTriangleVertexData{};
            tubeTriangleVertexData.vertexPosition = transformedPoint;
            tubeTriangleVertexData.vertexLinePointIndex = vertexLinePointIndex | 0x80000000u;
            tubeTriangleVertexData.vertexNormal = vertexNormal;
            //tubeTriangleVertexData.phi = theta;
            tubeTriangleVertexData.phi = phi;
            vertexDataList.at(vertexOffsetCap++) = tubeTriangleVertexData;

            if (lat == numLatitudeSubdivisions) {
                break;
            }
        }
    }
    for (int lat = 0; lat < numLatitudeSubdivisions; lat++) {
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            if (lat > 0) {
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions
                        + (lat-1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat-1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat-1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
            } else {
                triangleIndices.at(triOffsetCap++) = indexOffsetCap;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon+1)%numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffsetCap + 1
                        + (lon)%numLongitudeSubdivisions;
            }
        }
    }
}

void addEllipticHemisphereToMeshStop(
        const glm::vec3& center, glm::vec3 tangent, glm::vec3 normal,
        uint32_t indexOffset, uint32_t indexOffsetCap, uint32_t triOffsetCap,
        uint32_t vertexLinePointIndex, float tubeNormalRadius, float tubeBinormalRadius,
        int numLongitudeSubdivisions, int numLatitudeSubdivisions,
        std::vector<uint32_t>& triangleIndices, std::vector<TubeTriangleVertexData>& vertexDataList) {
    glm::vec3 binormal = glm::cross(normal, tangent);
    glm::vec3 scaledTangent = std::min(tubeNormalRadius, tubeBinormalRadius) * tangent;
    glm::vec3 scaledNormal = tubeNormalRadius * normal;
    glm::vec3 scaledBinormal = tubeBinormalRadius * binormal;

    float theta; // azimuth;
    float phi; // zenith;

    glm::mat3 frameMatrix(scaledNormal, scaledBinormal, scaledTangent);
    glm::mat3 normalFrameMatrix = glm::transpose(glm::inverse(frameMatrix));

    uint32_t vertexIndexOffset = indexOffsetCap - indexOffset - numLongitudeSubdivisions;
    for (int lat = 1; lat <= numLatitudeSubdivisions; lat++) {
        phi = sgl::HALF_PI * (1.0f - float(lat) / float(numLatitudeSubdivisions));
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            theta = -sgl::TWO_PI * float(lon) / float(numLongitudeSubdivisions);

            glm::vec3 pt(
                    std::cos(theta) * std::sin(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(phi)
            );

            glm::vec3 transformedPoint(
                    pt.x * scaledNormal.x + pt.y * scaledBinormal.x + pt.z * scaledTangent.x + center.x,
                    pt.x * scaledNormal.y + pt.y * scaledBinormal.y + pt.z * scaledTangent.y + center.y,
                    pt.x * scaledNormal.z + pt.y * scaledBinormal.z + pt.z * scaledTangent.z + center.z
            );
            glm::vec3 vertexNormal = glm::normalize(normalFrameMatrix * pt);

            TubeTriangleVertexData tubeTriangleVertexData{};
            tubeTriangleVertexData.vertexPosition = transformedPoint;
            tubeTriangleVertexData.vertexLinePointIndex = vertexLinePointIndex | 0x80000000u;
            tubeTriangleVertexData.vertexNormal = vertexNormal;
            //tubeTriangleVertexData.phi = -theta;
            tubeTriangleVertexData.phi = phi;
            vertexDataList.at(indexOffsetCap++) = tubeTriangleVertexData;

            if (lat == numLatitudeSubdivisions) {
                break;
            }
        }
    }
    for (int lat = 0; lat < numLatitudeSubdivisions; lat++) {
        for (int lon = 0; lon < numLongitudeSubdivisions; lon++) {
            if (lat < numLatitudeSubdivisions-1) {
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat+1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat+1)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat+1)*numLongitudeSubdivisions;
            } else {
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + (lon+1)%numLongitudeSubdivisions
                        + (lat)*numLongitudeSubdivisions;
                triangleIndices.at(triOffsetCap++) =
                        indexOffset + vertexIndexOffset
                        + 0
                        + (lat+1)*numLongitudeSubdivisions;
            }
        }
    }
}

void createCappedTriangleEllipticTubesRenderDataCPU(
        const std::vector<std::vector<glm::vec3>>& lineCentersList,
        const std::vector<std::vector<glm::vec3>>& lineRightVectorsList,
        float tubeNormalRadius, float tubeBinormalRadius, int numEllipseSubdivisions, bool tubeClosed,
        std::vector<uint32_t>& triangleIndices,
        std::vector<TubeTriangleVertexData>& vertexDataList,
        std::vector<LinePointReference>& linePointReferenceList,
        uint32_t linePointOffset,
        std::vector<glm::vec3>& lineTangents,
        std::vector<glm::vec3>& lineNormals) {
    numEllipseSubdivisions = std::max(numEllipseSubdivisions, 4);
    if (size_t(numEllipseSubdivisions) != globalEllipseVertexPositions.size()
            || tubeNormalRadius != globalTubeNormalRadius
            || tubeBinormalRadius != globalTubeBinormalRadius) {
        initGlobalEllipseVertexPositions(numEllipseSubdivisions, tubeNormalRadius, tubeBinormalRadius);
    }

    /*
     * If the tube is open, close it with two hemisphere caps at the ends.
     */
    int numLongitudeSubdivisions = numEllipseSubdivisions; // azimuth
    int numLatitudeSubdivisions = int(std::ceil(numEllipseSubdivisions/2)); // zenith
    uint32_t numCapVertices =
            numLongitudeSubdivisions * (numLatitudeSubdivisions - 1) + 1;
    uint32_t numCapIndices =
            numLongitudeSubdivisions * (numLatitudeSubdivisions - 1) * 6 + numLongitudeSubdivisions * 3;

    for (size_t lineId = 0; lineId < lineCentersList.size(); lineId++) {
        const std::vector<glm::vec3>& lineCenters = lineCentersList.at(lineId);
        const std::vector<glm::vec3>& lineRightVectors = lineRightVectorsList.at(lineId);
        size_t n = lineCenters.size();
        auto lineIndexOffset = uint32_t(lineTangents.size());

        // Assert that we have a valid input data range
        if (tubeClosed && n < 3) {
            continue;
        }
        if (!tubeClosed && n < 2) {
            continue;
        }

        auto indexOffsetCapStart = uint32_t(vertexDataList.size());
        auto triOffsetCapStart = uint32_t(triangleIndices.size());
        if (!tubeClosed) {
            vertexDataList.resize(vertexDataList.size() + numCapVertices);
            triangleIndices.resize(triangleIndices.size() + numCapIndices);
        }
        auto indexOffset = uint32_t(vertexDataList.size());

        int firstIdx = int(n) - 2;
        int lastIdx = 1;
        int numValidLinePoints = 0;
        for (size_t i = 0; i < n; i++) {
            glm::vec3 tangent;
            if (!tubeClosed && i == 0) {
                tangent = lineCenters[i + 1] - lineCenters[i];
            } else if (!tubeClosed && i == n - 1) {
                tangent = lineCenters[i] - lineCenters[i - 1];
            } else {
                tangent = (lineCenters[(i + 1) % n] - lineCenters[(i + n - 1) % n]);
            }
            float lineSegmentLength = glm::length(tangent);

            if (lineSegmentLength < 0.0001f) {
                // In case the two vertices are almost identical, just skip this path line segment
                continue;
            }
            firstIdx = std::min(int(i), firstIdx);
            lastIdx = std::max(int(i), lastIdx);
            tangent = glm::normalize(tangent);
            glm::vec3 normal = glm::cross(lineRightVectors.at(i), tangent);

            insertOrientedEllipsePoints(
                    lineCenters.at(i), tangent, normal,
                    linePointOffset + uint32_t(linePointReferenceList.size()),
                    vertexDataList);
            lineTangents.push_back(tangent);
            lineNormals.push_back(normal);
            linePointReferenceList.emplace_back(lineId, i);
            numValidLinePoints++;
        }

        if (numValidLinePoints == 1) {
            // Only one vertex left -> output nothing (tube consisting only of one point).
            vertexDataList.resize(indexOffsetCapStart);
            linePointReferenceList.pop_back();
            lineTangents.pop_back();
            lineNormals.pop_back();
        }
        if (numValidLinePoints <= 1) {
            continue;
        }

        for (int i = 0; i < numValidLinePoints-1; i++) {
            for (int j = 0; j < numEllipseSubdivisions; j++) {
                // Build two CCW triangles (one quad) for each side
                // Triangle 1
                triangleIndices.push_back(
                        indexOffset + i*numEllipseSubdivisions+j);
                triangleIndices.push_back(
                        indexOffset + i*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);

                // Triangle 2
                triangleIndices.push_back(
                        indexOffset + i*numEllipseSubdivisions+j);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numEllipseSubdivisions+j);
            }
        }

        auto indexOffsetCapEnd = uint32_t(vertexDataList.size());
        auto triOffsetCapEnd = uint32_t(triangleIndices.size());
        if (!tubeClosed) {
            vertexDataList.resize(vertexDataList.size() + numCapVertices);
            triangleIndices.resize(triangleIndices.size() + numCapIndices);
        }

        if (tubeClosed) {
            /*
             * The tube is supposed to be closed. However, as we iteratively construct an artificial normal for
             * each line point perpendicular to the approximated line tangent, the normals at the begin and the
             * end of the tube do not match (i.e. the normal is not continuous).
             * Thus, the idea is to connect the begin and the end of the tube in such a way that the length of
             * the connecting edges is minimized. This is done by computing the angle between the two line
             * normals and shifting the edge indices by a necessary offset.
             */
            glm::vec3 normalA = lineNormals[lineIndexOffset + numValidLinePoints - 1];
            glm::vec3 normalB = lineNormals[lineIndexOffset];
            float normalAngleDifference = std::atan2(
                    glm::length(glm::cross(normalA, normalB)), glm::dot(normalA, normalB));
            normalAngleDifference = std::fmod(normalAngleDifference + sgl::TWO_PI, sgl::TWO_PI);
            int jOffset = int(std::round(normalAngleDifference / (sgl::TWO_PI) * float(numEllipseSubdivisions)));
            for (int j = 0; j < numEllipseSubdivisions; j++) {
                // Build two CCW triangles (one quad) for each side
                // Triangle 1
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numEllipseSubdivisions+(j)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numEllipseSubdivisions+(j+1+jOffset)%numEllipseSubdivisions);

                // Triangle 2
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numEllipseSubdivisions+(j)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numEllipseSubdivisions+(j+1+jOffset)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numEllipseSubdivisions+(j+jOffset)%numEllipseSubdivisions);
            }
        } else {
            // Hemisphere at the start
            glm::vec3 center0 = lineCenters[firstIdx];
            glm::vec3 tangent0 = lineCenters[firstIdx] - lineCenters[firstIdx + 1];
            tangent0 = glm::normalize(tangent0);
            glm::vec3 normal0 = lineNormals[lineIndexOffset];

            // Hemisphere at the end
            glm::vec3 center1 = lineCenters[lastIdx];
            glm::vec3 tangent1 = lineCenters[lastIdx] - lineCenters[lastIdx - 1];
            tangent1 = glm::normalize(tangent1);
            glm::vec3 normal1 = lineNormals[lineIndexOffset + numValidLinePoints - 1];

            addEllipticHemisphereToMeshStart(
                    center0, tangent0, normal0, indexOffset, indexOffsetCapStart, triOffsetCapStart,
                    linePointOffset + uint32_t(lineIndexOffset),
                    tubeNormalRadius, tubeBinormalRadius, numLongitudeSubdivisions, numLatitudeSubdivisions,
                    triangleIndices, vertexDataList);
            addEllipticHemisphereToMeshStop(
                    center1, tangent1, normal1, indexOffset, indexOffsetCapEnd, triOffsetCapEnd,
                    linePointOffset + uint32_t(lineTangents.size() - 1),
                    tubeNormalRadius, tubeBinormalRadius, numLongitudeSubdivisions, numLatitudeSubdivisions,
                    triangleIndices, vertexDataList);
        }
    }
}



void createCappedTrianglePrincipalStressTubesRenderDataCPU(
        const std::vector<std::vector<glm::vec3>>& lineCentersList,
        const std::vector<std::vector<glm::vec3>>& lineRightVectorsList,
        const std::vector<uint32_t>& linePrincipalStressIndexList,
        const std::vector<std::vector<float>>& lineMajorStressesList,
        const std::vector<std::vector<float>>& lineMediumStressesList,
        const std::vector<std::vector<float>>& lineMinorStressesList,
        float tubeRadius, int numEllipseSubdivisions, bool tubeClosed,
        bool hyperstreamline, // Hyperstreamline or normal stress ratio tube?
        float minimumHyperstreamlineWidth,
        std::vector<uint32_t>& triangleIndices,
        std::vector<TubeTriangleVertexData>& vertexDataList,
        std::vector<LinePointReference>& linePointReferenceList,
        uint32_t linePointOffset,
        std::vector<glm::vec3>& lineTangents,
        std::vector<glm::vec3>& lineNormals) {
    numEllipseSubdivisions = std::max(numEllipseSubdivisions, 4);

    /*
     * If the tube is open, close it with two hemisphere caps at the ends.
     */
    int numLongitudeSubdivisions = numEllipseSubdivisions; // azimuth
    int numLatitudeSubdivisions = int(std::ceil(numEllipseSubdivisions/2)); // zenith
    uint32_t numCapVertices =
            numLongitudeSubdivisions * (numLatitudeSubdivisions - 1) + 1;
    uint32_t numCapIndices =
            numLongitudeSubdivisions * (numLatitudeSubdivisions - 1) * 6 + numLongitudeSubdivisions * 3;

    for (size_t lineId = 0; lineId < lineCentersList.size(); lineId++) {
        const std::vector<glm::vec3>& lineCenters = lineCentersList.at(lineId);
        const std::vector<glm::vec3> &lineRightVectors = lineRightVectorsList.at(lineId);
        const std::vector<float> &lineMajorStresses = lineMajorStressesList.at(lineId);
        const std::vector<float> &lineMediumStresses = lineMediumStressesList.at(lineId);
        const std::vector<float> &lineMinorStresses = lineMinorStressesList.at(lineId);
        size_t n = lineCenters.size();
        auto lineIndexOffset = uint32_t(lineTangents.size());
        uint32_t principalStressIndex = linePrincipalStressIndexList.at(lineId);

        // Assert that we have a valid input data range
        if (tubeClosed && n < 3) {
            continue;
        }
        if (!tubeClosed && n < 2) {
            continue;
        }

        bool radiusStartSet = false;
        float tubeNormalRadiusStart = tubeRadius, tubeBinormalRadiusStart = tubeRadius;
        float tubeNormalRadiusEnd = tubeRadius, tubeBinormalRadiusEnd = tubeRadius;

        auto indexOffsetCapStart = uint32_t(vertexDataList.size());
        auto triOffsetCapStart = uint32_t(triangleIndices.size());
        if (!tubeClosed) {
            vertexDataList.resize(vertexDataList.size() + numCapVertices);
            triangleIndices.resize(triangleIndices.size() + numCapIndices);
        }
        auto indexOffset = uint32_t(vertexDataList.size());

        int numValidLinePoints = 0;
        int firstIdx = int(n) - 2;
        int lastIdx = 1;
        for (size_t i = 0; i < n; i++) {
            glm::vec3 tangent;
            if (!tubeClosed && i == 0) {
                tangent = lineCenters[i + 1] - lineCenters[i];
            } else if (!tubeClosed && i == n - 1) {
                tangent = lineCenters[i] - lineCenters[i - 1];
            } else {
                tangent = (lineCenters[(i + 1) % n] - lineCenters[(i + n - 1) % n]);
            }
            float lineSegmentLength = glm::length(tangent);

            if (lineSegmentLength < 0.0001f) {
                // In case the two vertices are almost identical, just skip this path line segment
                continue;
            }
            firstIdx = std::min(int(i), firstIdx);
            lastIdx = std::max(int(i), lastIdx);
            tangent = glm::normalize(tangent);
            glm::vec3 normal = glm::cross(lineRightVectors.at(i), tangent);

            float majorStress = lineMajorStresses.at(i);
            float mediumStress = lineMediumStresses.at(i);
            float minorStress = lineMinorStresses.at(i);

            float stressX;
            float stressZ;
            if (principalStressIndex == 0) {
                stressX = mediumStress;
                stressZ = minorStress;
            } else if (principalStressIndex == 1) {
                stressX = minorStress;
                stressZ = majorStress;
            } else {
                stressX = mediumStress;
                stressZ = majorStress;
            }

            float thickness0, thickness1;
            if (hyperstreamline) {
                stressX = std::max(glm::abs(stressX), minimumHyperstreamlineWidth);
                stressZ = std::max(glm::abs(stressZ), minimumHyperstreamlineWidth);
                thickness0 = stressX;
                thickness1 = stressZ;
            } else {
                float factorX = glm::clamp(glm::abs(stressX / stressZ), 0.0f, 1.0f);
                float factorZ = glm::clamp(glm::abs(stressZ / stressX), 0.0f, 1.0f);
                thickness0 = factorX;
                thickness1 = factorZ;
            }

            float tubeNormalRadius = tubeRadius * thickness0;
            float tubeBinormalRadius = tubeRadius * thickness1;

            if (!radiusStartSet) {
                tubeNormalRadiusStart = tubeNormalRadius;
                tubeBinormalRadiusStart = tubeBinormalRadius;
                radiusStartSet = true;
            }
            tubeNormalRadiusEnd = tubeNormalRadius;
            tubeBinormalRadiusEnd = tubeBinormalRadius;

            uint32_t vertexLinePointIndex = linePointOffset + uint32_t(linePointReferenceList.size());
            glm::vec3 center = lineCenters.at(i);
            glm::vec3 binormal = glm::cross(tangent, normal);
            for (int j = 0; j < numEllipseSubdivisions; j++) {
                float t = float(j) / float(numEllipseSubdivisions) * sgl::TWO_PI;
                float cosAngle = std::cos(t);
                float sinAngle = std::sin(t);
                glm::vec3 pt = glm::vec3(tubeNormalRadius * cosAngle, tubeBinormalRadius * sinAngle, 0.0f);
                glm::vec3 ptNormal = glm::normalize(glm::vec3(
                        tubeBinormalRadius * cosAngle, tubeNormalRadius * sinAngle, 0.0f));

                glm::vec3 transformedPoint(
                        pt.x * normal.x + pt.y * binormal.x + pt.z * tangent.x + center.x,
                        pt.x * normal.y + pt.y * binormal.y + pt.z * tangent.y + center.y,
                        pt.x * normal.z + pt.y * binormal.z + pt.z * tangent.z + center.z
                );
                glm::vec3 transformedNormal(
                        ptNormal.x * normal.x + ptNormal.y * binormal.x + ptNormal.z * tangent.x,
                        ptNormal.x * normal.y + ptNormal.y * binormal.y + ptNormal.z * tangent.y,
                        ptNormal.x * normal.z + ptNormal.y * binormal.z + ptNormal.z * tangent.z
                );

                TubeTriangleVertexData tubeTriangleVertexData{};
                tubeTriangleVertexData.vertexPosition = transformedPoint;
                tubeTriangleVertexData.vertexLinePointIndex = vertexLinePointIndex;
                tubeTriangleVertexData.vertexNormal = transformedNormal;
                tubeTriangleVertexData.phi = float(j) / float(numEllipseSubdivisions) * sgl::TWO_PI;
                vertexDataList.push_back(tubeTriangleVertexData);
            }

            lineTangents.push_back(tangent);
            lineNormals.push_back(normal);
            linePointReferenceList.emplace_back(lineId, i);
            numValidLinePoints++;
        }

        if (numValidLinePoints == 1) {
            // Only one vertex left -> output nothing (tube consisting only of one point).
            vertexDataList.resize(indexOffsetCapStart);
            linePointReferenceList.pop_back();
            lineTangents.pop_back();
            lineNormals.pop_back();
        }
        if (numValidLinePoints <= 1) {
            continue;
        }

        for (int i = 0; i < numValidLinePoints-1; i++) {
            for (int j = 0; j < numEllipseSubdivisions; j++) {
                // Build two CCW triangles (one quad) for each side
                // Triangle 1
                triangleIndices.push_back(
                        indexOffset + i*numEllipseSubdivisions+j);
                triangleIndices.push_back(
                        indexOffset + i*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);

                // Triangle 2
                triangleIndices.push_back(
                        indexOffset + i*numEllipseSubdivisions+j);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);
                triangleIndices.push_back(
                        indexOffset + ((i+1)%numValidLinePoints)*numEllipseSubdivisions+j);
            }
        }

        auto indexOffsetCapEnd = uint32_t(vertexDataList.size());
        auto triOffsetCapEnd = uint32_t(triangleIndices.size());
        if (!tubeClosed) {
            vertexDataList.resize(vertexDataList.size() + numCapVertices);
            triangleIndices.resize(triangleIndices.size() + numCapIndices);
        }

        if (tubeClosed) {
            /*
             * The tube is supposed to be closed. However, as we iteratively construct an artificial normal for
             * each line point perpendicular to the approximated line tangent, the normals at the begin and the
             * end of the tube do not match (i.e. the normal is not continuous).
             * Thus, the idea is to connect the begin and the end of the tube in such a way that the length of
             * the connecting edges is minimized. This is done by computing the angle between the two line
             * normals and shifting the edge indices by a necessary offset.
             */
            glm::vec3 normalA = lineNormals[lineIndexOffset + numValidLinePoints - 1];
            glm::vec3 normalB = lineNormals[lineIndexOffset];
            float normalAngleDifference = std::atan2(
                    glm::length(glm::cross(normalA, normalB)), glm::dot(normalA, normalB));
            normalAngleDifference = std::fmod(normalAngleDifference + sgl::TWO_PI, sgl::TWO_PI);
            int jOffset = int(std::round(normalAngleDifference / (sgl::TWO_PI) * float(numEllipseSubdivisions)));
            for (int j = 0; j < numEllipseSubdivisions; j++) {
                // Build two CCW triangles (one quad) for each side
                // Triangle 1
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numEllipseSubdivisions+(j)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numEllipseSubdivisions+(j+1)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numEllipseSubdivisions+(j+1+jOffset)%numEllipseSubdivisions);

                // Triangle 2
                triangleIndices.push_back(indexOffset + (numValidLinePoints-1)*numEllipseSubdivisions+(j)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numEllipseSubdivisions+(j+1+jOffset)%numEllipseSubdivisions);
                triangleIndices.push_back(indexOffset + 0*numEllipseSubdivisions+(j+jOffset)%numEllipseSubdivisions);
            }
        } else {
            // Hemisphere at the start
            glm::vec3 center0 = lineCenters[firstIdx];
            glm::vec3 tangent0 = lineCenters[firstIdx] - lineCenters[firstIdx + 1];
            tangent0 = glm::normalize(tangent0);
            glm::vec3 normal0 = lineNormals[lineIndexOffset];

            // Hemisphere at the end
            glm::vec3 center1 = lineCenters[lastIdx];
            glm::vec3 tangent1 = lineCenters[lastIdx] - lineCenters[lastIdx - 1];
            tangent1 = glm::normalize(tangent1);
            glm::vec3 normal1 = lineNormals[lineIndexOffset + numValidLinePoints - 1];

            addEllipticHemisphereToMeshStart(
                    center0, tangent0, normal0, indexOffset, indexOffsetCapStart, triOffsetCapStart,
                    linePointOffset + uint32_t(lineIndexOffset),
                    tubeNormalRadiusStart, tubeBinormalRadiusStart,
                    numLongitudeSubdivisions, numLatitudeSubdivisions,
                    triangleIndices, vertexDataList);
            addEllipticHemisphereToMeshStop(
                    center1, tangent1, normal1, indexOffset, indexOffsetCapEnd, triOffsetCapEnd,
                    linePointOffset + uint32_t(lineTangents.size() - 1),
                    tubeNormalRadiusEnd, tubeBinormalRadiusEnd,
                    numLongitudeSubdivisions, numLatitudeSubdivisions,
                    triangleIndices, vertexDataList);
        }
    }
}
