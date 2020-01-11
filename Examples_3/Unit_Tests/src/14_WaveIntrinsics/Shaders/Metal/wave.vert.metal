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
#include <metal_stdlib>
using namespace metal;

struct Vertex_Shader
{
    struct Uniforms_SceneConstantBuffer
    {
        float4x4 orthProjMatrix;
        float2 mousePosition;
        float2 resolution;
        float time;
        uint renderMode;
        uint laneSize;
        uint padding;
    };
    constant Uniforms_SceneConstantBuffer & SceneConstantBuffer;
    struct PSInput
    {
        float4 position [[position]];
        float4 color;
    };
	struct VSInput
	{
		float3 position [[attribute(0)]];
		float4 color [[attribute(1)]];
	};
    PSInput main(float3 position, float4 color)
    {
        PSInput result;
        (result.position = ((SceneConstantBuffer.orthProjMatrix)*(float4(position, 1.0))));
        (result.color = color);
        return result;
    };

    Vertex_Shader(
constant Uniforms_SceneConstantBuffer & SceneConstantBuffer) :
SceneConstantBuffer(SceneConstantBuffer) {}
};

struct VSDataPerFrame {
    constant Vertex_Shader::Uniforms_SceneConstantBuffer& SceneConstantBuffer [[id(0)]];
};

vertex Vertex_Shader::PSInput stageMain(
	Vertex_Shader::VSInput vsInput [[stage_in]],
    constant VSDataPerFrame& vsDataPerFrame     [[buffer(UPDATE_FREQ_PER_FRAME)]]
)
{
    float3 position0;
    position0 = vsInput.position;
    float4 color0;
    color0 = vsInput.color;
    Vertex_Shader main(vsDataPerFrame.SceneConstantBuffer);
    return main.main(position0, color0);
}
