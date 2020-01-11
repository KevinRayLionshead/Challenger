/*
 * Copyright (c) 2018 Kostas Anagnostou (https://twitter.com/KostasAAA).
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

Texture2D sceneTexture		: register(t6);
SamplerState clampMiplessLinearSampler : register(s7);

cbuffer cbPerFrame : register(b3, UPDATE_FREQ_PER_FRAME) 
{
	float4x4	worldMat;
	float4x4	projViewMat;
	float4		screenSize;
}

struct VSOutput
{
	float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD;
};

float4 main(VSOutput input) : SV_TARGET
{
	float4 src = sceneTexture.Sample(clampMiplessLinearSampler, input.TexCoord);

	if(screenSize.a > 0.5)
	{
		float2 uv = input.TexCoord;
		float2 coord = (uv - 0.5) * (screenSize.x/screenSize.y) * 2.0;
		float rf = sqrt(dot(coord, coord)) * 0.2;
		float rf2_1 = rf * rf + 1.0;
		float e = 1.0 / (rf2_1 * rf2_1);
		//e = pow(e, 2.0);
		//e = saturate(e + 0.5);
		return float4(src.rgb* e, 1.0f);
	}
	else
		return float4(src.rgb, 1.0f);	
}