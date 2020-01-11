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

/*
*Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
*
*Permission is hereby granted, free of charge, to any person obtaining a copy
*of this software and associated documentation files (the "Software"), to deal
*in the Software without restriction, including without limitation the rights
*to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*copies of the Software, and to permit persons to whom the Software is
*furnished to do so, subject to the following conditions:
*
*The above copyright notice and this permission notice shall be included in
*all copies or substantial portions of the Software.
*
*THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
*AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*THE SOFTWARE.
*/

#define EPSILON 1e-7f

StructuredBuffer<float4> GuideHairVertexPositions : register(t0, UPDATE_FREQ_PER_DRAW);
StructuredBuffer<float4> GuideHairVertexTangents : register(t1, UPDATE_FREQ_PER_DRAW);
StructuredBuffer<float> HairThicknessCoefficients : register(t2, UPDATE_FREQ_PER_DRAW);
SamplerState LinearSampler : register(s0);

#if defined(HAIR_SHADOW)
#define CB_CAMERA_SET UPDATE_FREQ_PER_BATCH
#else
#define CB_CAMERA_SET UPDATE_FREQ_PER_FRAME
#endif

cbuffer cbCamera : register(b0, CB_CAMERA_SET)
{
	float4x4 CamVPMatrix;
	float4x4 CamInvVPMatrix;
	float3 CamPos;
	float __dumm;
	int bUseEnvironmentLight;
}

cbuffer cbHair : register(b2, UPDATE_FREQ_PER_DRAW)
{
	float4x4 Transform;
	uint RootColor;
	uint StrandColor;
	float ColorBias;
	float Kd;
	float Ks1;
	float Ex1;
	float Ks2;
	float Ex2;
	float FiberRadius;
	float FiberSpacing;
	uint NumVerticesPerStrand;
}

cbuffer cbHairGlobal : register(b5)
{
	float4 Viewport;
	float4 Gravity;
	float4 Wind;
	float TimeStep;
}

float4 GetStrandColor(int index)
{
	float4 rootColor = float4(RootColor >> 24, (RootColor >> 16) & 0xFF, (RootColor >> 8) & 0xFF, RootColor & 0xFF) * (1.0f / 255.0f);
	float4 strandColor = float4(StrandColor >> 24, (StrandColor >> 16) & 0xFF, (StrandColor >> 8) & 0xFF, StrandColor & 0xFF) * (1.0f / 255.0f);

	float strandPos = (index % NumVerticesPerStrand) / float(NumVerticesPerStrand);
	float colorWeight = 1.0f - saturate(pow(1.0f - strandPos, ColorBias));

	return lerp(rootColor, strandColor, colorWeight);
}

#ifdef HAIR_SHADOW

struct VSOutput
{
	float4 Position : SV_POSITION;
};

VSOutput main(uint vertexID : SV_VertexID)
{
	uint index = vertexID / 2;

	float3 v = GuideHairVertexPositions[index].xyz;
	float3 t = GuideHairVertexTangents[index].xyz;

	v = mul(Transform, float4(v, 1.0f)).xyz;
	t = normalize(mul(Transform, float4(t, 0.0f)).xyz);

	float3 right = normalize(cross(t, normalize(v - CamPos)));
	float2 projRight = normalize(mul(CamVPMatrix, float4(right, 0)).xy);

	float thickness = HairThicknessCoefficients[index];

	float4 hairEdgePositions[2];
	hairEdgePositions[0] = float4(v + -right * thickness * FiberRadius, 1.0f);
	hairEdgePositions[1] = float4(v + right * thickness * FiberRadius, 1.0f);
	hairEdgePositions[0] = mul(CamVPMatrix, hairEdgePositions[0]);
	hairEdgePositions[1] = mul(CamVPMatrix, hairEdgePositions[1]);

	VSOutput output = (VSOutput)0;
	output.Position = hairEdgePositions[vertexID & 1];
	return output;
}
#else

struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 Tangent : TANGENT;
	float4 P0P1 : POINT;
	float4 Color : COLOR;
	float2 W0W1 : POINT1;
};

VSOutput main(uint vertexID : SV_VertexID)
{
	uint index = vertexID / 2;

	float3 v = GuideHairVertexPositions[index].xyz;
	float3 t = GuideHairVertexTangents[index].xyz;

	v = mul(Transform, float4(v, 1.0f)).xyz;
	t = normalize(mul(Transform, float4(t, 0.0f)).xyz);

	float3 right = normalize(cross(t, normalize(v - CamPos)));
	float2 projRight = normalize(mul(CamVPMatrix, float4(right, 0)).xy);

	float expandPixels = 0.71f;

	float thickness = HairThicknessCoefficients[index];

	float4 hairEdgePositions[2];
	hairEdgePositions[0] = float4(v + -right * thickness * FiberRadius, 1.0f);
	hairEdgePositions[1] = float4(v + right * thickness * FiberRadius, 1.0f);
	hairEdgePositions[0] = mul(CamVPMatrix, hairEdgePositions[0]);
	hairEdgePositions[1] = mul(CamVPMatrix, hairEdgePositions[1]);

	float dir = (vertexID & 1) ? 1.0f : -1.0f;

	VSOutput output = (VSOutput)0;
	output.Position = hairEdgePositions[vertexID & 1] + dir * float4(projRight * expandPixels / Viewport.w, 0.0f, 0.0f) * hairEdgePositions[vertexID & 1].w;
	output.Tangent = float4(t, thickness);
	output.P0P1 = float4(hairEdgePositions[0].xy, hairEdgePositions[1].xy);
	output.Color = GetStrandColor(index);
	output.W0W1 = float2(hairEdgePositions[0].w, hairEdgePositions[1].w);
	return output;
}
#endif