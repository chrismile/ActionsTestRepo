/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2022, Felix Brendel, Christoph Neuhauser
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

#include <vector>
#include <variant>
#include <cstdint>
#include <cmath>

#include <Utils/Defer.hpp>
#include <Utils/SearchStructures/KdTree.hpp>

#include "DtPathTrace.hpp"

#define ERROR(...) printf("ERROR: "  __VA_ARGS__);
#define INFO(...)
// #define INFO(...) printf("INFO: "  __VA_ARGS__);
//
const float pi = 3.1415926535897932384626433832795f;
const float two_pi = 6.283185307179586476925286766559f;


inline int32_t max(int32_t a, int32_t b) {
    return a > b ? a : b;
}

inline double max(double a, double b) {
    return a > b ? a : b;
}

inline double min(double a, double b) {
    return a < b ? a : b;
}

inline float max(float a, float b) {
    return a > b ? a : b;
}

inline float min(float a, float b) {
    return a < b ? a : b;
}

inline float max(float a, float b, float c) {
    return max(a, max(b, c));
}

inline float min(float a, float b, float c) {
    return min(a, min(b, c));
}

inline uint32_t max(uint32_t a, uint32_t b) {
    return a > b ? a : b;
}

inline uint32_t min(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}


Image create_spherical_heatmap_image(sgl::KdTree<sgl::Empty>* kd_tree, uint32_t image_height) {
    const float search_radius = 0.1f;
    const float rbf_epsilon = 3.0f;


    Image out_image;
    out_image.width  = image_height * 2;
    out_image.height = image_height;
    out_image.allocate();

    const float sqrt_two     = std::sqrt(2.0f);
    const float two_sqrt_two = 2.0f * sqrt_two;

    assert(out_image.width > 0);
    assert(out_image.height > 0);

    std::vector<std::pair<glm::vec3, sgl::Empty>> searchCache;
    float max_rbf_value = 0.0f;

    for (uint32_t y = 0; y < out_image.height; ++y) {
        for (uint32_t x = 0; x < out_image.width; ++x) {
            float u = -1.0f + (1.0f * x / (out_image.width - 1)) * 2.0f; // from   -1 to 1
            float v = -0.5f + (1.0f * y / (out_image.height - 1));       // from -0.5 to 0.5

            Pixel* p = &out_image.pixels[y*out_image.width+x];

            if (glm::length(glm::vec2(u, 2*v)) <= 1) {
                // we are in the ellipse
                float x_inner = two_sqrt_two * u;
                float y_inner = two_sqrt_two * v;

                float z = float(std::sqrt(1 - std::pow(x_inner / 4, 2) - std::pow(y_inner / 2, 2)));

                float lambda = float(2 * std::atan((z * x_inner) / (2 * (2 * std::pow(z, 2) - 1)))); // from -pi to pi
                float phi    = std::asin(z * y_inner);                                               // from -pi/2 to pi/2

                const glm::vec4 X = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                const glm::vec3 Y = glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec3 Z = glm::vec3(0.0f, 0.0f, 1.0f);

                glm::vec3 point_on_sphere =
                    glm::rotate(lambda,  Y) *
                    glm::rotate(phi,     Z) *
                    X;


                searchCache.clear();
                kd_tree->findPointsAndDataInSphere(point_on_sphere, search_radius, searchCache);

                p->f32 = 0.0f;

                for (std::pair<glm::vec3, sgl::Empty>& hit : searchCache) {
                    glm::vec3 pos = hit.first;

                    float dist = glm::length(point_on_sphere - pos);

                    float rbf_param = dist / search_radius;

                    p->f32 += (float)std::exp(-(float)std::pow(rbf_epsilon * rbf_param,2));
                }

                // auto num_hits = kd_tree->getNumPointsInSphere(point_on_sphere, 0.025f, searchCache);

                max_rbf_value = std::max(p->f32, max_rbf_value);
            } else {
                // we are outside the ellipse
                p->f32 = -1.0f;
            }
        }
    }

    for (uint32_t y = 0; y < out_image.height; ++y) {
        for (uint32_t x = 0; x < out_image.width; ++x) {
            Pixel* p = &out_image.pixels[y*out_image.width+x];
            if (p->f32 == -1.0f) {
                p->r = 0;
                p->g = 0;
                p->b = 0;
                p->a = 0;
            } else {
                auto rbf_value = p->f32;
                sgl::Color result;
                // NOTE(Felix): poor man's transfer function
                if (rbf_value < max_rbf_value/2) {
                    // blue to green gradient
                    result = sgl::colorLerp({0,0,255,255}, {0,255,0,255}, 2.0f*rbf_value/max_rbf_value);
                } else {
                    // green to red gradient
                    result = sgl::colorLerp({0,255,0,255}, {255,0,0,255}, 2.0f*rbf_value/max_rbf_value-1.0f);
                }
                p->r = result.getR();
                p->g = result.getG();
                p->b = result.getB();
                p->a = 255;
            }
        }
    }

    return out_image;
}

namespace Random {
    static struct {
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;
    } RNG_STATE;


    uint32_t taus_step(uint32_t z, int S1, int S2, int S3, uint32_t M) {
        uint32_t b = (((z << S1) ^ z) >> S2);
        return ((z & M) << S3) ^ b;
    }

    uint32_t lcg_step(uint32_t z, uint32_t A, uint32_t C) {
        return A * z + C;
    }

    float hybrid_taus() {
        RNG_STATE.x = taus_step(RNG_STATE.x, 13, 19, 12, 4294967294);
        RNG_STATE.y = taus_step(RNG_STATE.y, 2, 25, 4, 4294967288);
        RNG_STATE.z = taus_step(RNG_STATE.z, 3, 11, 17, 4294967280);
        RNG_STATE.w = lcg_step(RNG_STATE.w, 1664525, 1013904223);

        return float(2.3283064365387e-10 * (RNG_STATE.x ^ RNG_STATE.y ^ RNG_STATE.z ^ RNG_STATE.w));
    }

    float random() {
        return hybrid_taus();
    }

    void init(uint32_t seed) {

        RNG_STATE.x = seed;
        RNG_STATE.y = seed;
        RNG_STATE.z = seed;
        RNG_STATE.w = seed;

        for (uint32_t i = 0; i < 23 + seed % 13; i++)
            random();
    }

}

void create_orthonormal_basis(glm::vec3 D, glm::vec3& B, glm::vec3& T) {
    glm::vec3 other =
        fabs(D.z) >= 0.999
        ? glm::vec3{1, 0, 0}
        : glm::vec3{0, 0, 1};

    B = glm::normalize(glm::cross(other, D));
    T = glm::normalize(glm::cross(D, B));
}

void create_orthonormal_basis2(glm::vec3 D, glm::vec3& B, glm::vec3& T) {
    glm::vec3 other =
        fabs(D.z) >= 0.999
        ? glm::vec3{1, 0, 0}
        : glm::vec3{0, 0, 1};

    B = glm::normalize(glm::cross(other, D));
    T = glm::normalize(glm::cross(D, B));
}

glm::vec3 random_direction(glm::vec3 D) {
    float r1 = Random::random();
    float r2 = Random::random() * 2 - 1;
    float sqrR2 = r2 * r2;
    float two_pi_by_r1 = two_pi * r1;
    float sqrt_of_one_minus_sqrR2 = std::sqrt(1.0f - sqrR2);
    float x = std::cos(two_pi_by_r1) * sqrt_of_one_minus_sqrR2;
    float y = std::sin(two_pi_by_r1) * sqrt_of_one_minus_sqrR2;
    float z = r2;

    glm::vec3 t0, t1;
    create_orthonormal_basis2(D, t0, t1);

    return t0 * x + t1 * y + D * z;
}

float invert_cdf(float g_factor, float xi) {
#define one_minus_g2 (1.0f - (g_factor) * (g_factor))
#define one_plus_g2  (1.0f + (g_factor) * (g_factor))
#define one_over_2g  (0.5f / (g_factor))

    float t = (one_minus_g2) / (1.0f - g_factor + 2.0f * g_factor * xi);
    return one_over_2g * (one_plus_g2 - t * t);

#undef one_minus_g2
#undef one_plus_g2
#undef one_over_2g
}

glm::vec3 importance_sample_phase(float g_factor, glm::vec3 D) {
    if (std::fabs(g_factor) < 0.001f) {
        return random_direction(-D);
    }

    float phi = Random::random() * 2 * pi;
    float cosTheta = invert_cdf(g_factor, Random::random());
    float sinTheta = std::sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));

    glm::vec3 t0, t1;
    create_orthonormal_basis(D, t0, t1);

    return sinTheta * std::sin(phi) * t0
         + sinTheta * std::cos(phi) * t1
         + cosTheta * D;
}

void get_grid_box(Texture3D grid, glm::vec3& minim, glm::vec3& maxim) {
    float maxDim = max(float(grid.size_x), float(grid.size_y), float(grid.size_z));
    maxim = glm::vec3{
        (float)grid.size_x,
        (float)grid.size_y,
        (float)grid.size_z
    } / maxDim * 0.25f;
    minim = -maxim;
}


glm::vec3 abs(glm::vec3 v) {
    glm::vec3 result;
    result.x = std::fabs(v.x);
    result.y = std::fabs(v.y);
    result.z = std::fabs(v.z);
    return result;
}

glm::mat2x3 abs(glm::mat2x3 M) {
    glm::mat2x3 result;
    result[0] = abs(M[0]);
    result[1] = abs(M[1]);
    return result;
}

bool operator<=(glm::vec3 v, float s) {
    return
        v.x <= s &&
        v.y <= s &&
        v.z <= s;
}

bool operator<=(glm::mat2x3 M, float s) {
    return M[0] <= s && M[1] <= s;
}

glm::mat2x3 operator/(glm::mat2x3 M1, glm::mat2x3 M2) {
    glm::mat2x3 result;
    result[0] = M1[0] / M2[0];
    result[1] = M1[1] / M2[1];
    return result;
}


bool box_intersect(glm::vec3 b_min, glm::vec3 b_max, glm::vec3 P,
                   glm::vec3 D, float& t_min, float& t_max)
{

    glm::mat2x3 C  = glm::mat2x3{ b_min - P, b_max - P };
    glm::mat2x3 D2 = glm::mat2x3{ D, D };

    glm::mat2x3 T =
        abs(D2) <= 0.000001f
        ? glm::mat2x3{glm::vec3{-1000, -1000, -1000},
                      glm::vec3{ 1000,  1000,  1000}}
        : C / D2;

    t_min = max(min(T[0][0], T[1][0]),
                min(T[0][1], T[1][1]),
                min(T[0][2], T[1][2]));

    t_min = max(0.0f, t_min);

    t_max = min(max(T[0][0], T[1][0]),
                max(T[0][1], T[1][1]),
                max(T[0][2], T[1][2]));

    if (t_max < t_min || t_max < 0) {
        return false;
    }
    return true;
}

// Polar_Coordinate to_polar(glm::vec3 pos) {
//     Polar_Coordinate result;
//     result.theta = acos(glm::dot(glm::normalize(glm::vec2(pos.x, pos.z)), glm::vec2(1.0f,0.0f)));
//     result.phi   = acos(glm::dot(glm::normalize(pos),
//                                  glm::normalize(glm::vec3(pos.x, 0.0f, pos.z))));

//     if (pos.z < 0)
//         result.theta = -result.theta;

//     if (pos.z < 0)
//         result.phi = -result.phi;

//     return result;
// }

void dt_path_trace(PathInfo path_info, VolumeInfo volume_info,
                   Trajectories* trajis, Exit_Directions* exit_dirs)
{
    int pass_number = path_info.pass_number;
    glm::vec3 x = path_info.camera_pos;
    glm::vec3 w = path_info.ray_direction;

    float density = volume_info.extinction[pass_number % 3]; // extinction coefficient multiplier.
    INFO("  density: %f\n", density);

    glm::vec3 b_min, b_max;
    get_grid_box(volume_info.grid, b_min, b_max);

    glm::vec3 W { 0 }; // weight
    W[pass_number % 3] = 3; // Wavelenght dependent importance multiplier

    float t_min, t_max;
    if (!box_intersect(b_min, b_max, x, w, t_min, t_max))
        return;

    Trajectory trajectory;
    std::vector<float> attribs;

    defer {
        trajectory.attributes.push_back(attribs);
        trajis->push_back(trajectory);
        // TODO(Felix): probably `w' is already normalized, so we don't need to
        //   normalize it again
        exit_dirs->push_back(glm::normalize(w));
    };

    trajectory.positions.push_back(x);
    attribs.push_back({1});

    float d = t_max - t_min;
    x += w * t_min;

    trajectory.positions.push_back(x);
    attribs.push_back({1});


    while (true) {
        float t = // majorant of all volume data
            density <= 0.00001
            ? 10000000
            : -std::log(std::max(0.00000000001f, 1.0f - Random::random())) / density;

        INFO("  t: %f\n", t);
        INFO("  d: %f\n", d);

        x += w * t; // move to next event position or border

        if (t >= d) {
            INFO("->Ray left the volume\n");
            trajectory.positions.push_back(x);
            attribs.push_back({1});

            break;
        }

        trajectory.positions.push_back(x);
        attribs.push_back({1});

        glm::vec3 tSamplePosition = (x - b_min) / (b_max - b_min);
        float probExt = volume_info.grid.sample_at(tSamplePosition);
        INFO("  sample pos: %f %f %f\n", tSamplePosition.x, tSamplePosition.y, tSamplePosition.z);
        INFO("  density there: %f\n", probExt);

        float m_t = probExt * density; // extinction coef
        float m_s = m_t * volume_info.scattering_albedo[pass_number % 3]; // scattering coef
        float m_a = m_t - m_s; // absorption coef
        float m_n = density - m_t; // null coef

        float xi = Random::random();

        float Pa = m_a / density;
        //float Ps = m_s / density;
        float Pn = m_n / density;

        if (xi < Pa) { // absorption
            INFO("->absorbtion\n");
            break;
        }

        if (xi < 1 - Pn) { // scattering
            INFO("->scatter\n");
            w = importance_sample_phase(volume_info.g, w); // scattering event...

            if (!box_intersect(b_min, b_max, x, w, t_min, t_max)) {
                break;
            }

            d = t_max - t_min;
            x += w * t_min;
        } else {
            INFO("->null collision\n");
            // if no absorption and no scattering null collision occurred
            d -= t;
        }
    }

}
