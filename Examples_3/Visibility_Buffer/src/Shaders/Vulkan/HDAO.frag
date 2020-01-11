#version 450 core
#if !defined(WINDOWS) && !defined(ANDROID) && !defined(LINUX)
#define WINDOWS 	// Assume windows if no platform define has been added to the shader
#endif

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

// USERMACRO: SAMPLE_COUNT [1,2,4]
// USERMACRO: AO_QUALITY [1,2,3,4]

//--------------------------------------------------------------------------------------
// Gather pattern
//--------------------------------------------------------------------------------------

// Gather defines
#define RING_1    (1)
#define RING_2    (2)
#define RING_3    (3)
#define RING_4    (4)
#define NUM_RING_1_GATHERS    (2)
#define NUM_RING_2_GATHERS    (6)
#define NUM_RING_3_GATHERS    (12)
#define NUM_RING_4_GATHERS    (20)

// Ring sample pattern
const vec2 g_f2HDAORingPattern[NUM_RING_4_GATHERS] =
{
	// Ring 1
	{ 1, -1 },
	{ 0, 1 },

	// Ring 2
	{ 0, 3 },
	{ 2, 1 },
	{ 3, -1 },
	{ 1, -3 },

	// Ring 3
	{ 1, -5 },
	{ 3, -3 },
	{ 5, -1 },
	{ 4, 1 },
	{ 2, 3 },
	{ 0, 5 },

	// Ring 4
	{ 0, 7 },
	{ 2, 5 },
	{ 4, 3 },
	{ 6, 1 },
	{ 7, -1 },
	{ 5, -3 },
	{ 3, -5 },
	{ 1, -7 },
};

// Ring weights
const vec4 g_f4HDAORingWeight[NUM_RING_4_GATHERS] =
{
	// Ring 1 (Sum = 5.30864)
	{ 1.00000, 0.50000, 0.44721, 0.70711 },
	{ 0.50000, 0.44721, 0.70711, 1.00000 },

	// Ring 2 (Sum = 6.08746)
	{ 0.30000, 0.29104, 0.37947, 0.40000 },
	{ 0.42426, 0.33282, 0.37947, 0.53666 },
	{ 0.40000, 0.30000, 0.29104, 0.37947 },
	{ 0.53666, 0.42426, 0.33282, 0.37947 },

	// Ring 3 (Sum = 6.53067)
	{ 0.31530, 0.29069, 0.24140, 0.25495 },
	{ 0.36056, 0.29069, 0.26000, 0.30641 },
	{ 0.26000, 0.21667, 0.21372, 0.25495 },
	{ 0.29069, 0.24140, 0.25495, 0.31530 },
	{ 0.29069, 0.26000, 0.30641, 0.36056 },
	{ 0.21667, 0.21372, 0.25495, 0.26000 },

	// Ring 4 (Sum = 7.00962)
	{ 0.17500, 0.17365, 0.19799, 0.20000 },
	{ 0.22136, 0.20870, 0.24010, 0.25997 },
	{ 0.24749, 0.21864, 0.24010, 0.28000 },
	{ 0.22136, 0.19230, 0.19799, 0.23016 },
	{ 0.20000, 0.17500, 0.17365, 0.19799 },
	{ 0.25997, 0.22136, 0.20870, 0.24010 },
	{ 0.28000, 0.24749, 0.21864, 0.24010 },
	{ 0.23016, 0.22136, 0.19230, 0.19799 },
};

const float g_fRingWeightsTotal[RING_4] =
{
	5.30864,
	11.39610,
	17.92677,
	24.93639,
};

layout(push_constant, std430) uniform HDAOData
{
	vec2 g_f2RTSize;                  // Used by HDAO shaders for scaling texture coords
	float g_fHDAORejectRadius;          // HDAO param
	float g_fHDAOIntensity;             // HDAO param
	float g_fHDAOAcceptRadius;          // HDAO param
	float g_fQ;                         // far / (far - near)
	float g_fQTimesZNear;               // Q * near
} HDAORootConstants;

#if SAMPLE_COUNT > 1
layout(UPDATE_FREQ_NONE, binding = 0) uniform texture2DMS g_txDepth;
#else
layout(UPDATE_FREQ_NONE, binding = 0) uniform texture2D g_txDepth;
#endif

layout(UPDATE_FREQ_NONE, binding = 1) uniform sampler g_SamplePoint;
//----------------------------------------------------------------------------------------
// Helper function to Gather samples
//----------------------------------------------------------------------------------------
vec4 GatherSamples(
#if SAMPLE_COUNT > 1
	texture2DMS Tex,
#else
	texture2D Tex,
#endif
	vec2 f2TexCoord)
{
	vec4 f4Ret = vec4(0, 0, 0, 0);

#if SAMPLE_COUNT > 1
	ivec2 loc = ivec2(f2TexCoord * HDAORootConstants.g_f2RTSize);
    f4Ret.x = texelFetch(sampler2DMS(g_txDepth, g_SamplePoint), loc + ivec2(0,1), 0).x;
    f4Ret.y = texelFetch(sampler2DMS(g_txDepth, g_SamplePoint), loc + ivec2(1,1), 0).x;
    f4Ret.z = texelFetch(sampler2DMS(g_txDepth, g_SamplePoint), loc + ivec2(1,0), 0).x;
    f4Ret.w = texelFetch(sampler2DMS(g_txDepth, g_SamplePoint), loc + ivec2(0,0), 0).x;
	//f4Ret /= SAMPLE_COUNT.xxxx;
#else
	f4Ret = textureGather(sampler2D(Tex, g_SamplePoint), f2TexCoord);
#endif

	return f4Ret;
}

//--------------------------------------------------------------------------------------
// Helper function to gather Z values in 10.1 and 10.0 modes
//--------------------------------------------------------------------------------------
vec4 GatherZSamples(
#if SAMPLE_COUNT > 1
	texture2DMS Tex,
#else
	texture2D Tex,
#endif 
	vec2 f2TexCoord)
{
	vec4 f4Ret;
	vec4 f4Gather;

	f4Gather = GatherSamples(Tex, f2TexCoord);
	f4Ret = -HDAORootConstants.g_fQTimesZNear.xxxx / (f4Gather - HDAORootConstants.g_fQ.xxxx);

	return f4Ret;
}

#if AO_QUALITY == 2
const int iNumRings = RING_2;
const int iNumRingGathers = NUM_RING_2_GATHERS;
#elif AO_QUALITY == 3
const int iNumRings = RING_3;
const int iNumRingGathers = NUM_RING_3_GATHERS;
#elif AO_QUALITY == 4
const int iNumRings = RING_4;
const int iNumRingGathers = NUM_RING_4_GATHERS;
#else
const int iNumRings = RING_1;
const int iNumRingGathers = NUM_RING_1_GATHERS;
#endif

layout(location = 0) out float oColor;

void main()
{
	// Locals
	ivec2  i2ScreenCoord;
	vec2 f2ScreenCoord;
	vec2 f2TexCoord;
	vec2 f2MirrorTexCoord;
	vec2 f2InvRTSize;
	vec4 f4SampledZ[2];
	vec4 f4Diff;
	vec4 f4Compare[2];
	vec4 f4Occlusion = vec4(0, 0, 0, 0);
	float fOcclusion;
	int iGather;
	vec2 f2KernelScale = vec2(HDAORootConstants.g_f2RTSize.x / 1024.0f, HDAORootConstants.g_f2RTSize.y / 1024.0f);

	// Compute integer screen coord, and store off the inverse of the RT Size
	f2InvRTSize = 1.0f / HDAORootConstants.g_f2RTSize;
	f2ScreenCoord = gl_FragCoord.xy;
	i2ScreenCoord = ivec2(f2ScreenCoord);

	{
		f2TexCoord = vec2(f2ScreenCoord * f2InvRTSize);
		// Sample the center pixel for camera Z
		float fDepth = 0;
#if SAMPLE_COUNT > 1
	    fDepth = texelFetch(sampler2DMS(g_txDepth, g_SamplePoint), i2ScreenCoord, 0).x;
#else
		fDepth = textureLod(sampler2D(g_txDepth, g_SamplePoint), f2TexCoord, 0).x;
		f2ScreenCoord = vec2(i2ScreenCoord);
#endif
		float fCenterZ = -HDAORootConstants.g_fQTimesZNear / (fDepth - HDAORootConstants.g_fQ);

		// Loop through each gather location, and compare with its mirrored location
		for (iGather = 0; iGather < iNumRingGathers; iGather++)
		{
			vec2 f2SampleOffSet = f2KernelScale * g_f2HDAORingPattern[iGather];
			vec2 f2MirrorSampleOffSet = (f2SampleOffSet + vec2(1.0f, 1.0f)) * vec2(-1.0f, -1.0f);

			// Sample

			f2TexCoord = vec2(f2ScreenCoord + f2SampleOffSet)  * f2InvRTSize;
			f2MirrorTexCoord = vec2(f2ScreenCoord + f2MirrorSampleOffSet) * f2InvRTSize;

#if SAMPLE_COUNT > 1
			f4SampledZ[0] = GatherZSamples(g_txDepth, f2TexCoord);
			f4SampledZ[1] = GatherZSamples(g_txDepth, f2MirrorTexCoord);
#else
			f4SampledZ[0] = GatherZSamples(g_txDepth, f2TexCoord + f2InvRTSize);
			f4SampledZ[1] = GatherZSamples(g_txDepth, f2MirrorTexCoord + f2InvRTSize);
#endif

			// Detect valleys
			f4Diff = -fCenterZ.xxxx + f4SampledZ[0];
			f4Compare[0] = mix(vec4(1, 1, 1, 1), vec4(0, 0, 0, 0), greaterThan(f4Diff, HDAORootConstants.g_fHDAORejectRadius.xxxx));
			f4Compare[0] *= mix(vec4(1, 1, 1, 1), vec4(0, 0, 0, 0), lessThan(f4Diff, HDAORootConstants.g_fHDAOAcceptRadius.xxxx));

			f4Diff = -fCenterZ.xxxx + f4SampledZ[1];
			f4Compare[1] = mix(vec4(1, 1, 1, 1), vec4(0, 0, 0, 0), greaterThan(f4Diff, HDAORootConstants.g_fHDAORejectRadius.xxxx));
			f4Compare[1] *= mix(vec4(1, 1, 1, 1), vec4(0, 0, 0, 0), lessThan(f4Diff, HDAORootConstants.g_fHDAOAcceptRadius.xxxx));

			f4Occlusion.xyzw += (g_f4HDAORingWeight[iGather].xyzw * (f4Compare[0].xyzw * f4Compare[1].zwxy));
		}
	}

	// Finally calculate the HDAO occlusion value
	fOcclusion = ((f4Occlusion.x + f4Occlusion.y + f4Occlusion.z + f4Occlusion.w) / (2.0f * g_fRingWeightsTotal[iNumRings - 1]));

	fOcclusion *= (HDAORootConstants.g_fHDAOIntensity);
	fOcclusion = 1.0f - clamp(fOcclusion, 0, 1);

	oColor = fOcclusion;
}
