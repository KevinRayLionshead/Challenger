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

// Shader for ground plane in Unit Tests Animation

cbuffer uniformBlock : register(b0, UPDATE_FREQ_PER_DRAW)
{
    float4x4 mvp;
    float4x4 toWorld;
};

struct VSInput
{
  float4 Position : POSITION;
  float2 TexCoord : TEXCOORD0;
};

struct VSOutput {
	float4 Position : SV_POSITION;
  float2 TexCoord : TEXCOORD;
};

VSOutput main(VSInput input)
{
	  VSOutput result;
    float4x4 tempMat = mul(mvp, toWorld);
    result.Position = mul(tempMat, input.Position);

    result.TexCoord = input.TexCoord;

  	return result;
}
