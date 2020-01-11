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
#include <metal_stdlib>
#include <metal_atomic>
#include <metal_compute>
using namespace metal;

struct CSData {
    texture2d<float, access::read> albedobuffer     [[id(0)]];
    texture2d<float, access::read> lightbuffer      [[id(1)]];
#ifndef TARGET_IOS
    texture2d<float, access::write> outputRT        [[id(2)]];
#endif
};

//[numthreads(16, 16, 1)]
kernel void stageMain(
    uint3 DTid                      [[thread_position_in_grid]],
    constant CSData& csData         [[buffer(UPDATE_FREQ_NONE)]]
#ifdef TARGET_IOS
    ,texture2d<float, access::write> outputRT [[texture(0)]]
#endif
)
{
	float3 albedo =  csData.albedobuffer.read(DTid.xy).xyz;

	//linearise albedo before applying light to it
	albedo = pow(abs(albedo), 2.2);

	float3 diffuse = csData.lightbuffer.read(DTid.xy).xyz;

#ifndef TARGET_IOS
	csData.outputRT.write(float4(diffuse*albedo, 0), DTid.xy);
#else
	outputRT.write(float4(diffuse*albedo, 0), DTid.xy);
#endif
}
