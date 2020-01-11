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


struct VSOutput
{
	float4 Position : SV_POSITION;
	float4 UV : TEXCOORD0;
};

Texture2D Source : register(t0);
SamplerState PointSampler : register(s0);

float4 main(VSOutput input) : SV_Target
{    
	float4 total = 0.0f;
	[unroll] for (int x = 0; x < 4; ++x)
	{
		[unroll] for(int y = 0; y < 4; ++y)
			total += Source.Load(uint3(input.Position.xy * 4 + uint2(x, y), 0));
	}

	return total / 16.0f;
}
