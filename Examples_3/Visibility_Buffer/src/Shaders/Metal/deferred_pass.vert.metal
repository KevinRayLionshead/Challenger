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

// This shader performs the Deferred rendering pass: store per pixel geometry data.

#include <metal_stdlib>
using namespace metal;

#include "shader_defs.h"

struct PackedVertexPosData {
    packed_float3 position;
};

struct PackedVertexTexcoord {
    packed_float2 texCoord;
};

struct PackedVertexNormal {
    packed_float3 normal;
};

struct PackedVertexTangent {
    packed_float3 tangent;
};

struct VSInput
{
	float4 position [[attribute(0)]];
	float2 texCoord [[attribute(1)]];
	float3 normal   [[attribute(2)]];
	float3 tangent  [[attribute(3)]];
};

struct VSOutput {
	float4 position [[position]];
    float2 texCoord;
    float3 normal;
    float3 tangent;
};

struct PerBatchUniforms {
    uint drawId;
    uint twoSided;  // possible values: 0/1
};

struct IndirectDrawArguments
{
    uint vertexCount;
    uint instanceCount;
    uint startVertex;
    uint startInstance;
};

struct VSData {
    constant PerFrameConstants& uniforms              [[buffer(4)]];
};

// Vertex shader
vertex VSOutput stageMain(
    VSInput input                                     [[stage_in]],
    constant PerFrameConstants& uniforms    [[buffer(UNIT_VBPASS_UNIFORMS)]]
)
{
	VSOutput Out;
	Out.position = uniforms.transform[VIEW_CAMERA].mvp * input.position;
	Out.texCoord = input.texCoord;
	Out.normal = input.normal;
	Out.tangent = input.tangent;
	return Out;
}
