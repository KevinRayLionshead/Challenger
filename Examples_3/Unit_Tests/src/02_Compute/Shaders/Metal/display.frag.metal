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

struct PsIn {
	float4 position [[position]];
	float2 texCoord;
};

struct FSData {
    texture2d<float> uTex0      [[texture(0)]];
    sampler uSampler0           [[sampler(0)]];
};

fragment float4 stageMain(
    PsIn In [[stage_in]],
    constant FSData& fsData [[buffer(UPDATE_FREQ_NONE)]]
)
{
	return fsData.uTex0.sample(fsData.uSampler0, In.texCoord);
}
