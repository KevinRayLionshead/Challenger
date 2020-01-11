/*
 * Copyright (c) 2018-2019 Confetti Interactive Inc.
 *
 * This file is part of The-Forge
 * (see https://github.com/ConfettiFX/The-Forge).
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
*/

#include "shader_defs.h"

#define SHADOW_PCF 0
#define SHADOW_ESM 1
#define SHADOW SHADOW_PCF
#define NUM_SHADOW_SAMPLES 32

static const float NUM_SHADOW_SAMPLES_INV = 0.03125;

#if NUM_SHADOW_SAMPLES == 16
static const float shadowSamples[NUM_SHADOW_SAMPLES * 2] =
{
	-0.1746646, -0.7913184,

	0.08863912, -0.898169,

	0.1748409, -0.5252063,

	0.4529319, -0.384986,

	0.3857658, -0.9096935,

	0.768011, -0.4906538,

	0.6946555, 0.1605866,

	0.7986544, 0.5325912,

	0.2847693, 0.2293397,

	-0.4357893, -0.3808875,

	-0.139129, 0.2394065,

	0.4287854, 0.899425,

	-0.6924323, -0.2203967,

	-0.2611724, 0.7359962,

	-0.850104, 0.1263935,

	-0.5380967, 0.6264234,

};
#else
static const float shadowSamples[NUM_SHADOW_SAMPLES * 2] =
{
	-0.1746646, -0.7913184,
	-0.129792, -0.4477116,
	0.08863912, -0.898169,
	-0.5891499, -0.6781639,
	0.1748409, -0.5252063,
	0.6483325, -0.752117,
	0.4529319, -0.384986,
	0.09757467, -0.1166954,
	0.3857658, -0.9096935,
	0.5613058, -0.1283066,
	0.768011, -0.4906538,
	0.8499438, -0.220937,
	0.6946555, 0.1605866,
	0.9614297, 0.05975229,
	0.7986544, 0.5325912,
	0.4513965, 0.5592551,
	0.2847693, 0.2293397,
	-0.2118996, -0.1609127,
	-0.4357893, -0.3808875,
	-0.4662672, -0.05288446,
	-0.139129, 0.2394065,
	0.1781853, 0.5254948,
	0.4287854, 0.899425,
	0.1289349, 0.8724155,
	-0.6924323, -0.2203967,
	-0.48997, 0.2795907,
	-0.2611724, 0.7359962,
	-0.7704172, 0.4233134,
	-0.850104, 0.1263935,
	-0.8345267, -0.4991361,
	-0.5380967, 0.6264234,
	-0.9769312, -0.1550569
};
#endif



static const float PI = 3.1415926535897932384626422832795028841971f;

float2 LightingFunGGX_FV(float dotLH, float roughness)
{
	float alpha = roughness * roughness;

	//F
	float F_a, F_b;
	float dotLH5 = pow(saturate(1.0f - dotLH), 5.0f);
	F_a = 1.0f;
	F_b = dotLH5;

	//V
	float vis;
	float k = alpha * 0.5f;
	float k2 = k * k;
	float invK2 = 1.0f - k2;
	vis = 1.0f / (dotLH*dotLH*invK2 + k2);

	return float2((F_a - F_b)*vis, F_b*vis);
}

float LightingFuncGGX_D(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alphaSqr = alpha * alpha;
	float denom = max(dotNH * dotNH * (alphaSqr - 1.0f) + 1.0f, 0.001f);

	return alphaSqr / (PI*denom*denom);
}

float3 GGX_Spec(float3 Normal, float3 HalfVec, float Roughness, float3 SpecularColor, float2 paraFV)
{
	float NoH = saturate(dot(Normal, HalfVec));
	float NoH2 = NoH * NoH;
	float NoH4 = NoH2 * NoH2;
	float D = LightingFuncGGX_D(NoH4, Roughness);
	float2 FV_helper = paraFV;

	//float3 F0 = SpecularColor;
	float3 FV = SpecularColor * FV_helper.x + float3(FV_helper.y, FV_helper.y, FV_helper.y);

	return D * FV;
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
	//return F0 + (max(float3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
	float3 ret = float3(0.0, 0.0, 0.0);
	float powTheta = pow(1.0 - cosTheta, 5.0);
	float oneMinusRough = float(1.0 - roughness);

	ret.x = F0.x + (max(oneMinusRough, F0.x) - F0.x) * powTheta;
	ret.y = F0.y + (max(oneMinusRough, F0.y) - F0.y) * powTheta;
	ret.z = F0.z + (max(oneMinusRough, F0.z) - F0.z) * powTheta;

	return ret;
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float a2, float NoH)
{
	float d = (NoH * a2 - NoH) * NoH + 1;	// 2 mad
	return a2 / (PI*d*d);					// 4 mul, 1 rcp
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
	float a = sqrt(a2);
	float Vis_SmithV = NoL * (NoV * (1.0f - a) + a);
	float Vis_SmithL = NoV * (NoL * (1.0f - a) + a);
	return 0.5 * rcp(max(Vis_SmithV + Vis_SmithL, 0.001));
}

float Pow5(float x)
{
	float xx = x * x;
	return xx * xx * x;
}

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 SpecularColor, float VoH)
{
	float Fc = Pow5(1.0f - VoH);		// 1 sub, 3 mul
										//return Fc + (1 - Fc) * SpecularColor;		// 1 add, 3 mad

	// Anything less than 2% is physically impossible and is instead considered to be shadowing
	//return saturate(50.0f * SpecularColor.g) * Fc + (1.0f - Fc) * SpecularColor;
	return saturate(SpecularColor.g) * Fc + (1.0f - Fc) * SpecularColor;

}

float3 SpecularGGX(float Roughness, inout float3 SpecularColor, float NoL, float Nov, float NoH, float VoH)
{
	float a = Roughness * Roughness;
	float a2 = a * a;
	//float Energy = EnergyNormalization(a2, Context.VoH, AreaLight);

	// Generalized microfacet specular
	//float D = D_GGX(a2, Context.NoH) * Energy;
	float D = D_GGX(a2, NoH);
	float Vis = Vis_SmithJointApprox(a2, Nov, NoL);
	SpecularColor = F_Schlick(SpecularColor, VoH);

	return (D * Vis) * SpecularColor;
}

float3 PBR(float NoL, float NoV, float3 LightVec, float3 ViewVec,
	float3 HalfVec, float3 NormalVec, float3 ReflectVec, inout float3 albedo,
	inout float3 specColor, float Roughness, float Metallic, bool isBackface, float shadowFactor)
{
	//float3 diffuseTerm = float3(0.0f, 0.0f, 0.0f);
	float3 specularTerm = float3(0.0f, 0.0f, 0.0f);

	//float LoH = clamp(dot(LightVec, HalfVec), 0.0f, 1.0f);

	float NoH = saturate(dot(NormalVec, HalfVec));
	float VoH = saturate(dot(ViewVec, HalfVec));

	//DIFFUSE
	specColor = lerp(0.08 * specColor.rgb, albedo, Metallic);
	albedo = (1.0 - Metallic) * albedo;


	//SPECULAR
	if (!isBackface)
		specularTerm = lerp(float3(0.0f, 0.0f, 0.0f), SpecularGGX(Roughness, specColor, NoL, NoV, NoH, VoH), shadowFactor);

	return (albedo + specularTerm);
}

float3 PBR(float NoL, float NoV, float3 LightVec, float3 ViewVec,
	float3 HalfVec, float3 NormalVec, float3 ReflectVec, inout float3 albedo,
	inout float3 specColor, float Roughness, float Metallic, bool isBackface)
{
	//float3 diffuseTerm = float3(0.0f, 0.0f, 0.0f);
	float3 specularTerm = float3(0.0f, 0.0f, 0.0f);

	//float LoH = clamp(dot(LightVec, HalfVec), 0.0f, 1.0f);

	float NoH = saturate(dot(NormalVec, HalfVec));
	float VoH = saturate(dot(ViewVec, HalfVec));

	//DIFFUSE
	specColor = lerp(0.08 * specColor.rgb, albedo, Metallic);
	albedo = (1.0 - Metallic) * albedo;


	//SPECULAR
	if (!isBackface)
		specularTerm = SpecularGGX(Roughness, specColor, NoL, NoV, NoH, VoH);

	return (albedo + specularTerm);
}

float random(float3 seed, float3 freq)
{
	// project seed on random constant vector
	float dt = dot(floor(seed * freq), float3(53.1215, 21.1352, 9.1322));
	// return only the fractional part
	return frac(sin(dt) * 2105.2354);
}

float3 calculateSpecular(float3 specularColor, float3 camPos, float3 pixelPos, float3 normalizedDirToLight, float3 normal)
{
	float3 viewVec = normalize(camPos - pixelPos);
	float3 halfVec = normalize(viewVec + normalizedDirToLight);
	float specIntensity = 128;
	float specular = pow(saturate(dot(halfVec, normal)), specIntensity);
	return specularColor * specular;
}

float linearDepth(float depth)
{
	//float f = 2000.0;
	//float n = 10.0;
	return (20.0f) / (2010.0f - depth * (1990.0f));
}


float3 calculateIllumination(
	float3 normal,
	float3 ViewVec,
	float3 HalfVec,
	float3 ReflectVec,
	float NoL,
	float NoV,
	float3 camPos, float esmControl,
	float3 normalizedDirToLight, float4 posLS, float3 position,
	Texture2D shadowMap,
	float3 albedo,
	float3 specularColor,
	float Roughness,
	float Metallic,
	SamplerState sh,
	bool isBackface,
	float isPBR,
	out float shadowFactor)
{
	// Project pixel position post-perspective division coordinates and map to [0..1] range to access the shadow map
	posLS /= posLS.w;
	posLS.y *= -1;
	posLS.xy = posLS.xy * 0.5 + float2(0.5, 0.5);

	float2 HalfGaps = float2(0.00048828125, 0.00048828125);
	float2 Gaps = float2(0.0009765625, 0.0009765625);

	posLS.xy += HalfGaps;

	shadowFactor = 0.0f;

	float isInShadow = 0.0f;

	if (all(posLS.xy > 0) && all(posLS.xy < 1))
	{
#if SHADOW == SHADOW_PCF
		// waste of shader cycles
		// Perform percentage-closer shadows with randomly rotated poisson kernel
		float shadowFilterSize = 0.0016;
		float angle = random(position, 20.0);
		float s = sin(angle);
		float c = cos(angle);

		for (int i = 0; i < NUM_SHADOW_SAMPLES; i++)
		{
			float2 offset = float2(shadowSamples[i * 2], shadowSamples[i * 2 + 1]);
			offset = float2(offset.x * c + offset.y * s, offset.x * -s + offset.y * c);
			offset *= shadowFilterSize;
			float shadowMapValue = shadowMap.SampleLevel(sh, posLS.xy + offset, 0);
			shadowFactor += (shadowMapValue < posLS.z - 0.002 ? 0 : 1);
		}
		shadowFactor *= NUM_SHADOW_SAMPLES_INV;
#elif SHADOW == SHADOW_ESM
		// ESM
		float CShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy, 0).r);

		float LShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(-Gaps.x, 0.0), 0).r);
		float RShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(Gaps.x, 0.0), 0).r);

		float LTShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(-Gaps.x, Gaps.y), 0).r);
		float RTShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(Gaps.x, Gaps.y), 0).r);

		float TShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(0.0, Gaps.y), 0).r);
		float BShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(0.0, -Gaps.y), 0).r);

		float LBShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(-Gaps.x, -Gaps.y), 0).r);
		float RBShadow = linearDepth(shadowMap.SampleLevel(sh, posLS.xy + float2(Gaps.x, -Gaps.y), 0).r);

		float nbShadow = LShadow + RShadow + TShadow + BShadow + LTShadow + RTShadow + LBShadow + RBShadow;

		float avgShadowDepthSample = (CShadow + nbShadow) / 9.0f;
		//float avgShadowDepthSample = CShadow;

		float linearD = linearDepth(posLS.z);
		float esm = exp(-esmControl * linearD) * exp(esmControl * avgShadowDepthSample);

		shadowFactor = saturate(esm);
#endif
	}


	float3 finalColor;

	if (isPBR > 0.5f)
	{
		finalColor = PBR(NoL, NoV, -normalizedDirToLight, ViewVec, HalfVec, normal, ReflectVec, albedo, specularColor, Roughness, Metallic, isBackface, shadowFactor);
	}
	else
	{
		specularColor = calculateSpecular(specularColor, camPos, position, -normalizedDirToLight, normal);

		finalColor = albedo + lerp(float3(0.0, 0.0, 0.0), specularColor, shadowFactor);
	}

	finalColor *= shadowFactor;

	return finalColor;
}

float3 pointLightShade(
	float3 normal,
	float3 ViewVec,
	float3 HalfVec,
	float3 ReflectVec,
	float NoL,
	float NoV,
	float3 lightPos,
	float3 lightCol,
	float3 camPos,
	float3 normalizedDirToLight, float4 posLS, float3 position,
	float3 albedo,
	float3 specularColor,
	float Roughness,
	float Metallic,
	bool isBackface,
	float isPBR)
{
	float3 lVec = (lightPos - position) * (1.0 / LIGHT_SIZE);
	float3 lightVec = normalize(lVec);
	float atten = saturate(1.0f - dot(lVec, lVec));
	
	float3 finalColor;

	if (isPBR > 0.5f)
	{
		finalColor = PBR(NoL, NoV, -normalizedDirToLight, ViewVec, HalfVec, normal, ReflectVec, albedo, specularColor, Roughness, Metallic, isBackface);
	}
	else
	{
		specularColor = calculateSpecular(specularColor, camPos, position, -normalizedDirToLight, normal);

		finalColor = albedo + specularColor;
	}
	   
	return lightCol * finalColor * atten;
}


