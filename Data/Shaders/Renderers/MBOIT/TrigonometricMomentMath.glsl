/**
 * This file is part of an Vulkan GLSL port of the HLSL code accompanying the paper "Moment-Based Order-Independent
 * Transparency" by Münstermann, Krumpen, Klein, and Peters (http://momentsingraphics.de/?page_id=210).
 * The original code was released in accordance to CC0 (https://creativecommons.org/publicdomain/zero/1.0/).
 *
 * This port is released under the terms of BSD 2-Clause License. For more details please see the LICENSE file in the
 * root directory of this project.
 *
 * Changes for the Vulkan GLSL port: Copyright 2018-2022 Christoph Neuhauser
 */

/*! \file
    This header provides the utility functions to reconstruct the transmittance 
    from a given vector of trigonometric moments (2, 3 or 4 trigonometric 
    moments) at a specified depth.*/
#ifndef TRIGONOMETRIC_MOMENT_MATH_GLSL
#define TRIGONOMETRIC_MOMENT_MATH_GLSL
#include "ComplexAlgebra.glsl"

/*! This utility function turns a point on the unit circle into a scalar 
    parameter. It is guaranteed to grow monotonically for (cos(phi),sin(phi)) 
    with phi in 0 to 2*pi. There are no other guarantees. In particular it is 
    not an arclength parametrization. If you change this function, you must 
    also change circleToParameter() in MomentOIT.cpp.*/
float circleToParameter(vec2 circle_point){
    float result=abs(circle_point.y)-abs(circle_point.x);
    result=(circle_point.x<0.0f)?(2.0f-result):result;
    return (circle_point.y<0.0f)?(6.0f-result):result;
}

/*! This utility function returns the appropriate weight factor for a root at 
    the given location. Both inputs are supposed to be unit vectors. If a 
    circular arc going counter clockwise from (1.0,0.0) meets root first, it 
    returns 1.0, otherwise 0.0 or a linear ramp in the wrapping zone.*/
float getRootWeightFactor(float reference_parameter,float root_parameter,vec4 wrapping_zone_parameters){
    float binary_weigth_factor=(root_parameter<reference_parameter)?1.0f:0.0f;
    float linear_weigth_factor=clamp(fma(root_parameter,wrapping_zone_parameters.z,wrapping_zone_parameters.w), 0.0, 1.0);
    return binary_weigth_factor+linear_weigth_factor;
}

/*! This function reconstructs the transmittance at the given depth from two 
    normalized trigonometric moments.*/
float computeTransmittanceAtDepthFrom2TrigonometricMoments(float b_0, vec2 trig_b[2], float depth, float bias, float overestimation, vec4 wrapping_zone_parameters)
{
    // Apply biasing and reformat the inputs a little bit
    float moment_scale = 1.0f - bias;
    vec2 b[3] = {
        vec2(1.0f, 0.0f),
        trig_b[0] * moment_scale,
        trig_b[1] * moment_scale
    };
    // Compute a Cholesky factorization of the Toeplitz matrix
    float D00=RealPart(b[0]);
    float InvD00=1.0f/D00;
    vec2 L10=(b[1])*InvD00;
    float D11=RealPart(b[0]-D00*Multiply(L10,Conjugate(L10)));
    float InvD11=1.0f/D11;
    vec2 L20=(b[2])*InvD00;
    vec2 L21=(b[1]-D00*Multiply(L20,Conjugate(L10)))*InvD11;
    float D22=RealPart(b[0]-D00*Multiply(L20,Conjugate(L20))-D11*Multiply(L21,Conjugate(L21)));
    float InvD22=1.0f/D22;
    // Solve a linear system to get the relevant polynomial
    float phase = fma(depth, wrapping_zone_parameters.y, wrapping_zone_parameters.y);
    vec2 circle_point = vec2(cos(phase), sin(phase));
    vec2 c[3] = {
        vec2(1.0f,0.0f),
        circle_point,
        Multiply(circle_point, circle_point)
    };
    c[1]-=Multiply(L10,c[0]);
    c[2]-=Multiply(L20,c[0])+Multiply(L21,c[1]);
    c[0]*=InvD00;
    c[1]*=InvD11;
    c[2]*=InvD22;
    c[1]-=Multiply(Conjugate(L21),c[2]);
    c[0]-=Multiply(Conjugate(L10),c[1])+Multiply(Conjugate(L20),c[2]);
    // Compute roots of the polynomial
    vec2 pRoot[2];
    SolveQuadratic(pRoot, Conjugate(c[2]), Conjugate(c[1]), Conjugate(c[0]));
    // Figure out how to weight the weights
    float depth_parameter = circleToParameter(circle_point);
    vec3 weigth_factor;
    weigth_factor[0] = overestimation;
    //[unroll]
    for(int i = 0; i != 2; ++i)
    {
        float root_parameter = circleToParameter(pRoot[i]);
        weigth_factor[i+1] = getRootWeightFactor(depth_parameter, root_parameter, wrapping_zone_parameters);
    }
    // Compute the appropriate linear combination of weights
    vec2 z[3] = {circle_point, pRoot[0], pRoot[1]};
    float f0=weigth_factor[0];
    float f1=weigth_factor[1];
    float f2=weigth_factor[2];
    vec2 f01=Divide(f1-f0,z[1]-z[0]);
    vec2 f12=Divide(f2-f1,z[2]-z[1]);
    vec2 f012=Divide(f12-f01,z[2]-z[0]);
    vec2 polynomial[3];
    polynomial[0]=f012;
    polynomial[1]=polynomial[0];
    polynomial[0]=f01-Multiply(polynomial[0],z[1]);
    polynomial[2]=polynomial[1];
    polynomial[1]=polynomial[0]-Multiply(polynomial[1],z[0]);
    polynomial[0]=f0-Multiply(polynomial[0],z[0]);
    float weight_sum=0.0f;
    weight_sum+=RealPart(Multiply(b[0],polynomial[0]));
    weight_sum+=RealPart(Multiply(b[1],polynomial[1]));
    weight_sum+=RealPart(Multiply(b[2],polynomial[2]));
    // Turn the normalized absorbance into transmittance
    return exp(-b_0 * weight_sum);
}

/*! This function reconstructs the transmittance at the given depth from three 
    normalized trigonometric moments. */
float computeTransmittanceAtDepthFrom3TrigonometricMoments(float b_0, vec2 trig_b[3], float depth, float bias, float overestimation, vec4 wrapping_zone_parameters)
{
    // Apply biasing and reformat the inputs a little bit
    float moment_scale = 1.0f - bias;
    vec2 b[4] = {
        vec2(1.0f, 0.0f),
        trig_b[0] * moment_scale,
        trig_b[1] * moment_scale,
        trig_b[2] * moment_scale
    };
    // Compute a Cholesky factorization of the Toeplitz matrix
    float D00=RealPart(b[0]);
    float InvD00=1.0f/D00;
    vec2 L10=(b[1])*InvD00;
    float D11=RealPart(b[0]-D00*Multiply(L10,Conjugate(L10)));
    float InvD11=1.0f/D11;
    vec2 L20=(b[2])*InvD00;
    vec2 L21=(b[1]-D00*Multiply(L20,Conjugate(L10)))*InvD11;
    float D22=RealPart(b[0]-D00*Multiply(L20,Conjugate(L20))-D11*Multiply(L21,Conjugate(L21)));
    float InvD22=1.0f/D22;
    vec2 L30=(b[3])*InvD00;
    vec2 L31=(b[2]-D00*Multiply(L30,Conjugate(L10)))*InvD11;
    vec2 L32=(b[1]-D00*Multiply(L30,Conjugate(L20))-D11*Multiply(L31,Conjugate(L21)))*InvD22;
    float D33=RealPart(b[0]-D00*Multiply(L30,Conjugate(L30))-D11*Multiply(L31,Conjugate(L31))-D22*Multiply(L32,Conjugate(L32)));
    float InvD33=1.0f/D33;
    // Solve a linear system to get the relevant polynomial
    float phase = fma(depth, wrapping_zone_parameters.y, wrapping_zone_parameters.y);
    vec2 circle_point = vec2(cos(phase), sin(phase));
    vec2 circle_point_pow2 = Multiply(circle_point, circle_point);
    vec2 c[4] = {
        vec2(1.0f,0.0f),
        circle_point,
        circle_point_pow2,
        Multiply(circle_point, circle_point_pow2)
    };
    c[1]-=Multiply(L10,c[0]);
    c[2]-=Multiply(L20,c[0])+Multiply(L21,c[1]);
    c[3]-=Multiply(L30,c[0])+Multiply(L31,c[1])+Multiply(L32,c[2]);
    c[0]*=InvD00;
    c[1]*=InvD11;
    c[2]*=InvD22;
    c[3]*=InvD33;
    c[2]-=Multiply(Conjugate(L32),c[3]);
    c[1]-=Multiply(Conjugate(L21),c[2])+Multiply(Conjugate(L31),c[3]);
    c[0]-=Multiply(Conjugate(L10),c[1])+Multiply(Conjugate(L20),c[2])+Multiply(Conjugate(L30),c[3]);
    // Compute roots of the polynomial
    vec2 pRoot[3];
    SolveCubicBlinn(pRoot, Conjugate(c[3]), Conjugate(c[2]), Conjugate(c[1]), Conjugate(c[0]));
    // The roots are known to be normalized but for reasons of numerical 
    // stability it can be better to enforce that
    //pRoot[0]=normalize(pRoot[0]);
    //pRoot[1]=normalize(pRoot[1]);
    //pRoot[2]=normalize(pRoot[2]);
    // Figure out how to weight the weights
    float depth_parameter = circleToParameter(circle_point);
    vec4 weigth_factor;
    weigth_factor[0] = overestimation;
    //[unroll]
    for(int i = 0; i != 3; ++i)
    {
        float root_parameter = circleToParameter(pRoot[i]);
        weigth_factor[i+1] = getRootWeightFactor(depth_parameter, root_parameter, wrapping_zone_parameters);
    }
    // Compute the appropriate linear combination of weights
    vec2 z[4] = {circle_point, pRoot[0], pRoot[1], pRoot[2]};
    float f0=weigth_factor[0];
    float f1=weigth_factor[1];
    float f2=weigth_factor[2];
    float f3=weigth_factor[3];
    vec2 f01=Divide(f1-f0,z[1]-z[0]);
    vec2 f12=Divide(f2-f1,z[2]-z[1]);
    vec2 f23=Divide(f3-f2,z[3]-z[2]);
    vec2 f012=Divide(f12-f01,z[2]-z[0]);
    vec2 f123=Divide(f23-f12,z[3]-z[1]);
    vec2 f0123=Divide(f123-f012,z[3]-z[0]);
    vec2 polynomial[4];
    polynomial[0]=f0123;
    polynomial[1]=polynomial[0];
    polynomial[0]=f012-Multiply(polynomial[0],z[2]);
    polynomial[2]=polynomial[1];
    polynomial[1]=polynomial[0]-Multiply(polynomial[1],z[1]);
    polynomial[0]=f01-Multiply(polynomial[0],z[1]);
    polynomial[3]=polynomial[2];
    polynomial[2]=polynomial[1]-Multiply(polynomial[2],z[0]);
    polynomial[1]=polynomial[0]-Multiply(polynomial[1],z[0]);
    polynomial[0]=f0-Multiply(polynomial[0],z[0]);
    float weight_sum=0;
    weight_sum+=RealPart(Multiply(b[0],polynomial[0]));
    weight_sum+=RealPart(Multiply(b[1],polynomial[1]));
    weight_sum+=RealPart(Multiply(b[2],polynomial[2]));
    weight_sum+=RealPart(Multiply(b[3],polynomial[3]));
    // Turn the normalized absorbance into transmittance
    return exp(-b_0 * weight_sum);
}

/*! This function reconstructs the transmittance at the given depth from four 
    normalized trigonometric moments.*/
float computeTransmittanceAtDepthFrom4TrigonometricMoments(float b_0, vec2 trig_b[4], float depth, float bias, float overestimation, vec4 wrapping_zone_parameters)
{
    // Apply biasing and reformat the inputs a little bit
    float moment_scale = 1.0f - bias;
    vec2 b[5] = {
        vec2(1.0f, 0.0f),
        trig_b[0] * moment_scale,
        trig_b[1] * moment_scale,
        trig_b[2] * moment_scale,
        trig_b[3] * moment_scale
    };
    // Compute a Cholesky factorization of the Toeplitz matrix
    float D00=RealPart(b[0]);
    float InvD00=1.0/D00;
    vec2 L10=(b[1])*InvD00;
    float D11=RealPart(b[0]-D00*Multiply(L10,Conjugate(L10)));
    float InvD11=1.0/D11;
    vec2 L20=(b[2])*InvD00;
    vec2 L21=(b[1]-D00*Multiply(L20,Conjugate(L10)))*InvD11;
    float D22=RealPart(b[0]-D00*Multiply(L20,Conjugate(L20))-D11*Multiply(L21,Conjugate(L21)));
    float InvD22=1.0/D22;
    vec2 L30=(b[3])*InvD00;
    vec2 L31=(b[2]-D00*Multiply(L30,Conjugate(L10)))*InvD11;
    vec2 L32=(b[1]-D00*Multiply(L30,Conjugate(L20))-D11*Multiply(L31,Conjugate(L21)))*InvD22;
    float D33=RealPart(b[0]-D00*Multiply(L30,Conjugate(L30))-D11*Multiply(L31,Conjugate(L31))-D22*Multiply(L32,Conjugate(L32)));
    float InvD33=1.0/D33;
    vec2 L40=(b[4])*InvD00;
    vec2 L41=(b[3]-D00*Multiply(L40,Conjugate(L10)))*InvD11;
    vec2 L42=(b[2]-D00*Multiply(L40,Conjugate(L20))-D11*Multiply(L41,Conjugate(L21)))*InvD22;
    vec2 L43=(b[1]-D00*Multiply(L40,Conjugate(L30))-D11*Multiply(L41,Conjugate(L31))-D22*Multiply(L42,Conjugate(L32)))*InvD33;
    float D44=RealPart(b[0]-D00*Multiply(L40,Conjugate(L40))-D11*Multiply(L41,Conjugate(L41))-D22*Multiply(L42,Conjugate(L42))-D33*Multiply(L43,Conjugate(L43)));
    float InvD44=1.0/D44;
    // Solve a linear system to get the relevant polynomial
    float phase = fma(depth, wrapping_zone_parameters.y, wrapping_zone_parameters.y);
    vec2 circle_point = vec2(cos(phase), sin(phase));
    vec2 circle_point_pow2 = Multiply(circle_point, circle_point);
    vec2 c[5] = {
        vec2(1.0f,0.0f),
        circle_point,
        circle_point_pow2,
        Multiply(circle_point, circle_point_pow2),
        Multiply(circle_point_pow2, circle_point_pow2)
    };
    c[1]-=Multiply(L10,c[0]);
    c[2]-=Multiply(L20,c[0])+Multiply(L21,c[1]);
    c[3]-=Multiply(L30,c[0])+Multiply(L31,c[1])+Multiply(L32,c[2]);
    c[4]-=Multiply(L40,c[0])+Multiply(L41,c[1])+Multiply(L42,c[2])+Multiply(L43,c[3]);
    c[0]*=InvD00;
    c[1]*=InvD11;
    c[2]*=InvD22;
    c[3]*=InvD33;
    c[4]*=InvD44;
    c[3]-=Multiply(Conjugate(L43),c[4]);
    c[2]-=Multiply(Conjugate(L32),c[3])+Multiply(Conjugate(L42),c[4]);
    c[1]-=Multiply(Conjugate(L21),c[2])+Multiply(Conjugate(L31),c[3])+Multiply(Conjugate(L41),c[4]);
    c[0]-=Multiply(Conjugate(L10),c[1])+Multiply(Conjugate(L20),c[2])+Multiply(Conjugate(L30),c[3])+Multiply(Conjugate(L40),c[4]);
    // Compute roots of the polynomial
    vec2 pRoot[4];
    SolveQuarticNeumark(pRoot, Conjugate(c[4]), Conjugate(c[3]), Conjugate(c[2]), Conjugate(c[1]), Conjugate(c[0]));
    // Figure out how to weight the weights
    float depth_parameter = circleToParameter(circle_point);
    float weigth_factor[5];
    weigth_factor[0] = overestimation;
    //[unroll]
    for(int i = 0; i != 4; ++i)
    {
        float root_parameter = circleToParameter(pRoot[i]);
        weigth_factor[i+1] = getRootWeightFactor(depth_parameter, root_parameter, wrapping_zone_parameters);
    }
    // Compute the appropriate linear combination of weights
    vec2 z[5] = {circle_point, pRoot[0], pRoot[1], pRoot[2], pRoot[3]};
    float f0=weigth_factor[0];
    float f1=weigth_factor[1];
    float f2=weigth_factor[2];
    float f3=weigth_factor[3];
    float f4=weigth_factor[4];
    vec2 f01=Divide(f1-f0,z[1]-z[0]);
    vec2 f12=Divide(f2-f1,z[2]-z[1]);
    vec2 f23=Divide(f3-f2,z[3]-z[2]);
    vec2 f34=Divide(f4-f3,z[4]-z[3]);
    vec2 f012=Divide(f12-f01,z[2]-z[0]);
    vec2 f123=Divide(f23-f12,z[3]-z[1]);
    vec2 f234=Divide(f34-f23,z[4]-z[2]);
    vec2 f0123=Divide(f123-f012,z[3]-z[0]);
    vec2 f1234=Divide(f234-f123,z[4]-z[1]);
    vec2 f01234=Divide(f1234-f0123,z[4]-z[0]);
    vec2 polynomial[5];
    polynomial[0]=f01234;
    polynomial[1]=polynomial[0];
    polynomial[0]=f0123-Multiply(polynomial[0],z[3]);
    polynomial[2]=polynomial[1];
    polynomial[1]=polynomial[0]-Multiply(polynomial[1],z[2]);
    polynomial[0]=f012-Multiply(polynomial[0],z[2]);
    polynomial[3]=polynomial[2];
    polynomial[2]=polynomial[1]-Multiply(polynomial[2],z[1]);
    polynomial[1]=polynomial[0]-Multiply(polynomial[1],z[1]);
    polynomial[0]=f01-Multiply(polynomial[0],z[1]);
    polynomial[4]=polynomial[3];
    polynomial[3]=polynomial[2]-Multiply(polynomial[3],z[0]);
    polynomial[2]=polynomial[1]-Multiply(polynomial[2],z[0]);
    polynomial[1]=polynomial[0]-Multiply(polynomial[1],z[0]);
    polynomial[0]=f0-Multiply(polynomial[0],z[0]);
    float weight_sum=0;
    weight_sum+=RealPart(Multiply(b[0],polynomial[0]));
    weight_sum+=RealPart(Multiply(b[1],polynomial[1]));
    weight_sum+=RealPart(Multiply(b[2],polynomial[2]));
    weight_sum+=RealPart(Multiply(b[3],polynomial[3]));
    weight_sum+=RealPart(Multiply(b[4],polynomial[4]));
    // Turn the normalized absorbance into transmittance
    return exp(-b_0 * weight_sum);
}

#endif