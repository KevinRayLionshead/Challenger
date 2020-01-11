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

struct UniformData
{
    float4x4 projView;
    float3 camPos;
};

struct VSData {
    constant UniformData& uniformBlock    [[id(0)]];
};

struct VSInput {
    float4 Position [[attribute(0)]];
};

struct VSOutput {
    float4 Position [[position]];
    float3 WorldPos;
};

vertex VSOutput stageMain(VSInput In                 [[stage_in]],
                          constant VSData& vsData    [[buffer(UPDATE_FREQ_PER_FRAME)]])
{
    VSOutput result;

    float4 p = vsData.uniformBlock.projView * In.Position;
    result.Position = p.xyww;
    result.WorldPos = In.Position.xyz;
    return result;
}
