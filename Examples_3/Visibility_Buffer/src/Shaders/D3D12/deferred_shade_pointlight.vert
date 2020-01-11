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

#include "shading.h"

struct VSInput
{
    float4 position : POSITION;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 color : TEXCOORD0;
    float3 lightPos : TEXCOORD1;
};

ConstantBuffer<PerFrameConstants> uniforms : register(b0, UPDATE_FREQ_PER_FRAME);
StructuredBuffer<LightData> lights : register(t1);

VSOutput main(VSInput input, uint instanceId : SV_InstanceID)
{
    VSOutput output;
    output.lightPos = lights[instanceId].position;
    output.color = lights[instanceId].color;
    output.position = mul(uniforms.transform[VIEW_CAMERA].mvp, float4((input.position.xyz * LIGHT_SIZE) + output.lightPos, 1));
    return output;
}
