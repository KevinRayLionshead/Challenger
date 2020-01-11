#version 450 core
#if !defined(WINDOWS) && !defined(ANDROID) && !defined(LINUX)
#define WINDOWS 	// Assume windows if no platform define has been added to the shader
#endif


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


#extension GL_GOOGLE_include_directive : enable

#include "shader_defs.h"
#include "non_uniform_resource_index.h"

// This shader performs the Deferred rendering pass: store per pixel geometry data.

layout(UPDATE_FREQ_PER_FRAME, binding = 1) restrict readonly buffer indirectMaterialBuffer
{
	uint indirectMaterialBufferData[];
};
layout(UPDATE_FREQ_NONE, binding = 2) uniform sampler textureFilter;
layout(UPDATE_FREQ_NONE, binding = 3) uniform texture2D diffuseMaps[MAX_TEXTURE_UNITS];
layout(UPDATE_FREQ_NONE, binding = 3 + MAX_TEXTURE_UNITS) uniform texture2D normalMaps[MAX_TEXTURE_UNITS];
layout(UPDATE_FREQ_NONE, binding = 3 + MAX_TEXTURE_UNITS * 2) uniform texture2D specularMaps[MAX_TEXTURE_UNITS];

layout(location = 0) in vec2 iTexCoord;
layout(location = 1) in vec3 iNormal;
layout(location = 2) in vec3 iTangent;
layout(location = 3) in flat uint iDrawId;

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oNormal;
layout(location = 2) out vec4 oSpecular;
layout(location = 3) out vec4 oSimulation;

// Pixel shader for opaque geometry
void main()
{
	uint matBaseSlot = BaseMaterialBuffer(false, 1); //1 is camera view, 0 is shadow map view
	uint materialID = indirectMaterialBufferData[matBaseSlot + iDrawId];

	// CALCULATE PIXEL COLOR USING INTERPOLATED ATTRIBUTES
	// Reconstruct normal map Z from X and Y
	vec4 normalData = texture(sampler2D(normalMaps[NonUniformResourceIndex(materialID)], textureFilter), iTexCoord);

	vec2 normalMapRG = normalData.ga;

	vec3 reconstructedNormalMap;
	reconstructedNormalMap.xy = normalMapRG * 2 - 1;
	reconstructedNormalMap.z = sqrt(1 - dot(reconstructedNormalMap.xy, reconstructedNormalMap.xy));

	vec3 normal = normalize(iNormal);
	vec3 tangent = normalize(iTangent);
	// Calculate vertex binormal from normal and tangent
	vec3 binormal = normalize(cross(tangent, normal));
	// Calculate pixel normal using the normal map and the tangent space vectors
	oColor = texture(sampler2D(diffuseMaps[NonUniformResourceIndex(materialID)], textureFilter), iTexCoord);
	oColor.a = 0.0;
	oNormal = vec4((reconstructedNormalMap.x * tangent + reconstructedNormalMap.y * binormal + reconstructedNormalMap.z * normal) * 0.5 + 0.5, 0.0);
	oSpecular = texture(sampler2D(specularMaps[NonUniformResourceIndex(materialID)], textureFilter), iTexCoord);
	oSimulation = vec4(0,0,0,0);
}
