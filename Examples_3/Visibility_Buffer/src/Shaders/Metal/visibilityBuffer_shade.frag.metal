/*
 * Copyright (c) 2018-2019 Confetti Interactive Inc.
 *
 * This file is part of TheForge
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

// USERMACRO: SAMPLE_COUNT [1,2,4]
// USERMACRO: USE_AMBIENT_OCCLUSION [0,1]

// This shader loads draw / triangle Id per pixel and reconstruct interpolated vertex data.

#include <metal_stdlib>
using namespace metal;

#include "shader_defs.h"
#include "shading.h"

struct SceneVertexPos
{
    packed_float3 position;
};

struct SceneVertexTexcoord {
    packed_float2 texCoord;
};

struct SceneVertexNormal {
    packed_float3 normal;
};

struct SceneVertexTangent {
    packed_float3 tangent;
};

struct VSOutput {
	float4 position [[position]];
    float2 screenPos;
};

struct IndirectDrawArguments
{
    uint vertexCount;
    uint instanceCount;
    uint startVertex;
    uint startInstance;
};

struct DerivativesOutput
{
    float3 db_dx;
    float3 db_dy;
};

// Computes the partial derivatives of a triangle from the projected screen space vertices
DerivativesOutput computePartialDerivatives(float2 v[3])
{
    DerivativesOutput output;
    float d = 1.0 / determinant(float2x2(v[2] - v[1], v[0] - v[1]));
    output.db_dx = float3(v[1].y - v[2].y, v[2].y - v[0].y, v[0].y - v[1].y) * d;
    output.db_dy = float3(v[2].x - v[1].x, v[0].x - v[2].x, v[1].x - v[0].x) * d;
    return output;
}

// Helper functions to interpolate vertex attributes at point 'd' using the partial derivatives
float3 interpolateAttribute(float3x3 attributes, float3 db_dx, float3 db_dy, float2 d)
{
    float3 attribute_x = attributes * db_dx;
    float3 attribute_y = attributes * db_dy;
    float3 attribute_s = attributes[0];
    
    return (attribute_s + d.x * attribute_x + d.y * attribute_y);
}

float interpolateAttribute(float3 attributes, float3 db_dx, float3 db_dy, float2 d)
{
    float attribute_x = dot(attributes, db_dx);
    float attribute_y = dot(attributes, db_dy);
    float attribute_s = attributes[0];
    
    return (attribute_s + d.x * attribute_x + d.y * attribute_y);
}

struct GradientInterpolationResults
{
    float2 interp;
    float2 dx;
    float2 dy;
};

// Interpolate 2D attributes using the partial derivatives and generates dx and dy for texture sampling.
GradientInterpolationResults interpolateAttributeWithGradient(float3x2 attributes, float3 db_dx, float3 db_dy, float2 d, float2 twoOverRes)
{
    float3 attr0 = float3(attributes[0].x, attributes[1].x, attributes[2].x);
    float3 attr1 = float3(attributes[0].y, attributes[1].y, attributes[2].y);
    float2 attribute_x = float2(dot(db_dx,attr0), dot(db_dx,attr1));
    float2 attribute_y = float2(dot(db_dy,attr0), dot(db_dy,attr1));
    float2 attribute_s = attributes[0];
    
    GradientInterpolationResults result;
    result.dx = attribute_x * twoOverRes.x;
    result.dy = attribute_y * twoOverRes.y;
    result.interp = (attribute_s + d.x * attribute_x + d.y * attribute_y);
    return result;
}

float depthLinearization(float depth, float near, float far)
{
	return (2.0 * near) / (far + near - depth * (far - near));
}

struct BindlessDiffuseData
{
    array<texture2d<float>,MATERIAL_BUFFER_SIZE> textures;
};

struct BindlessNormalData
{
    array<texture2d<float>,MATERIAL_BUFFER_SIZE> textures;
};

struct BindlessSpecularData
{
    array<texture2d<float>,MATERIAL_BUFFER_SIZE> textures;
};

struct RootConstantDrawSceneData
{
	packed_float4 lightColor;
	uint lightingMode;
	uint outputMode;
    packed_float4 CameraPlane; //x : near, y : far
};

struct IndirectDrawArgumentsData {
	constant uint* data[NUM_CULLING_VIEWPORTS];
};

struct FSData {
    constant SceneVertexPos* vertexPos                          [[id(0)]];
    constant SceneVertexTexcoord* vertexTexCoord                [[id(1)]];
    constant SceneVertexNormal* vertexNormal                    [[id(2)]];
    constant SceneVertexTangent* vertexTangent                  [[id(3)]];

    constant MeshConstants* meshConstantsBuffer                 [[id(4)]];

    sampler textureSampler                                      [[id(5)]];
    sampler depthSampler                                        [[id(6)]];
//    sampler clampBorderNearSampler                              [[id(7)]];
//    sampler clampMiplessLinearSampler                           [[id(8)]];
//    sampler clampMiplessNearSampler                             [[id(9)]];
    
    device LightData* lights                                    [[id(10)]];

    texture2d<float,access::read> aoTex                         [[id(11)]];
    depth2d<float,access::sample> shadowMap                     [[id(12)]];
    
#if SAMPLE_COUNT > 1
    texture2d_ms<float,access::read> vbTex                      [[id(13)]];
#else
    texture2d<float,access::read> vbTex                         [[id(13)]];
#endif
    
    array<texture2d<float>, MATERIAL_BUFFER_SIZE> diffuseMaps;
    array<texture2d<float>, MATERIAL_BUFFER_SIZE> normalMaps;
    array<texture2d<float>, MATERIAL_BUFFER_SIZE> specularMaps;
};

struct FSDataPerFrame {
    constant uint* filteredIndexBuffer                          [[id(0)]];
    constant uint* indirectMaterialBuffer                       [[id(1)]];

    constant PerFrameConstants& uniforms                        [[id(2)]];
    
    constant uint* lightClustersCount                           [[id(9)]];
    constant uint* lightClusters                                [[id(10)]];
    
    constant uint* indirectDrawArgs[NUM_CULLING_VIEWPORTS];
};

// Pixel shader
fragment float4 stageMain(VSOutput input                                              [[stage_in]],
                          uint32_t sampleID                                           [[sample_id]],
                          constant FSData& fsData                     [[buffer(UPDATE_FREQ_NONE)]],
                          constant FSDataPerFrame& fsDataPerFrame     [[buffer(UPDATE_FREQ_PER_FRAME)]]
)
{
	float4 visRaw = fsData.vbTex.read(uint2(input.position.xy), sampleID);
	
	// Unpack float4 render target data into uint to extract data
	uint alphaBit_drawID_triID = pack_float_to_unorm4x8(visRaw);

    float3 shadedColor = float3(1.0f, 1.0f, 1.0f);
	
	// Early exit if this pixel doesn't contain triangle data
	if (alphaBit_drawID_triID != ~0u)
	{
		// Extract packed data
		uint drawID = (alphaBit_drawID_triID >> 23) & 0x000000FF;
		uint triangleID = (alphaBit_drawID_triID & 0x007FFFFF);
		uint alpha1_opaque0 = (alphaBit_drawID_triID >> 31);
    
//            return float4(drawID / 255.0, 0.0, 0.0, 1.0);
        
		// This is the start vertex of the current draw batch
		uint startIndex = fsDataPerFrame.indirectDrawArgs[alpha1_opaque0][drawID * INDIRECT_DRAW_ARGUMENTS_STRUCT_NUM_ELEMENTS + 2];

		// Calculate vertex indices for this triangle
		uint triIdx0 = (triangleID * 3 + 0) + startIndex;
		uint triIdx1 = (triangleID * 3 + 1) + startIndex;
		uint triIdx2 = (triangleID * 3 + 2) + startIndex;

		uint index0 = fsDataPerFrame.filteredIndexBuffer[triIdx0];
		uint index1 = fsDataPerFrame.filteredIndexBuffer[triIdx1];
		uint index2 = fsDataPerFrame.filteredIndexBuffer[triIdx2];
		
		// Load vertex data of the 3 vertices
		float3 v0pos = fsData.vertexPos[index0].position;
		float3 v1pos = fsData.vertexPos[index1].position;
		float3 v2pos = fsData.vertexPos[index2].position;

		// Transform positions to clip space
		float4 pos0 = fsDataPerFrame.uniforms.transform[VIEW_CAMERA].mvp * float4(v0pos, 1);
		float4 pos1 = fsDataPerFrame.uniforms.transform[VIEW_CAMERA].mvp * float4(v1pos, 1);
		float4 pos2 = fsDataPerFrame.uniforms.transform[VIEW_CAMERA].mvp * float4(v2pos, 1);

		// Calculate the inverse of w, since it's going to be used several times
		float3 one_over_w = 1.0 / float3(pos0.w, pos1.w, pos2.w);

		// Project vertex positions to calcualte 2D post-perspective positions
		pos0 *= one_over_w[0];
		pos1 *= one_over_w[1];
		pos2 *= one_over_w[2];

		float2 pos_scr[3] = {pos0.xy, pos1.xy, pos2.xy};

		// Compute partial derivatives. This is necessary to interpolate triangle attributes per pixel.
		DerivativesOutput derivativesOut = computePartialDerivatives(pos_scr);

		// Calculate delta vector (d) that points from the projected vertex 0 to the current screen point
		float2 d = input.screenPos + -pos_scr[0];

		// Interpolate the 1/w (one_over_w) for all three vertices of the triangle
		// using the barycentric coordinates and the delta vector
		float w = 1.0 / interpolateAttribute(one_over_w, derivativesOut.db_dx, derivativesOut.db_dy, d);

		// Reconstruct the Z value at this screen point performing only the necessary matrix * vector multiplication
		// operations that involve computing Z
		float z = w * fsDataPerFrame.uniforms.transform[VIEW_CAMERA].projection[2][2] + fsDataPerFrame.uniforms.transform[VIEW_CAMERA].projection[3][2];

		// Calculate the world position coordinates:
		// First the projected coordinates at this point are calculated using In.screenPos and the computed Z value at this point.
		// Then, multiplying the perspective projected coordinates by the inverse view-projection matrix (invVP) produces world coordinates
		float3 position = (fsDataPerFrame.uniforms.transform[VIEW_CAMERA].invVP * float4(input.screenPos * w, z, w)).xyz;

		// TEXTURE COORD INTERPOLATION
		// Apply perspective correction to texture coordinates
		float3x2 texCoords = {
			float2(fsData.vertexTexCoord[index0].texCoord) * one_over_w[0],
			float2(fsData.vertexTexCoord[index1].texCoord) * one_over_w[1],
			float2(fsData.vertexTexCoord[index2].texCoord) * one_over_w[2]
		};

		// Interpolate texture coordinates and calculate the gradients for texture sampling with mipmapping support
		GradientInterpolationResults results = interpolateAttributeWithGradient(texCoords, derivativesOut.db_dx, derivativesOut.db_dy, d, fsDataPerFrame.uniforms.twoOverRes);

		float linearZ = depthLinearization(z/w, fsDataPerFrame.uniforms.CameraPlane.x, fsDataPerFrame.uniforms.CameraPlane.y);
		float mip = pow(pow(linearZ, 0.9f) * 5.0f, 1.5f);

		float2 texCoordDX = results.dx * w * mip;
		float2 texCoordDY = results.dy * w * mip;
		float2 texCoord = results.interp * w;
		
		// NORMAL INTERPOLATION
		// Apply perspective division to normals
		float3x3 normals = {
			float3(fsData.vertexNormal[index0].normal) * one_over_w[0],
			float3(fsData.vertexNormal[index1].normal) * one_over_w[1],
			float3(fsData.vertexNormal[index2].normal) * one_over_w[2]
		};

		float3 normal = normalize(interpolateAttribute(normals, derivativesOut.db_dx, derivativesOut.db_dy, d));

		// TANGENT INTERPOLATION
		// Apply perspective division to tangents
		float3x3 tangents = {
			float3(fsData.vertexTangent[index0].tangent) * one_over_w[0],
			float3(fsData.vertexTangent[index1].tangent) * one_over_w[1],
			float3(fsData.vertexTangent[index2].tangent) * one_over_w[2]
		};
		
		float3 tangent = normalize(interpolateAttribute(tangents, derivativesOut.db_dx, derivativesOut.db_dy, d));

		// BaseMaterialBuffer returns constant offset values
		// The following value defines the maximum amount of indirect draw calls that will be
		// drawn at once. This value depends on the number of submeshes or individual objects
		// in the scene. Changing a scene will require to change this value accordingly.
		// #define MAX_DRAWS_INDIRECT 300
		//
		// These values are offsets used to point to the material data depending on the
		// type of geometry and on the culling view
		// #define MATERIAL_BASE_ALPHA0 0
		// #define MATERIAL_BASE_NOALPHA0 MAX_DRAWS_INDIRECT
		// #define MATERIAL_BASE_ALPHA1 (MAX_DRAWS_INDIRECT*2)
		// #define MATERIAL_BASE_NOALPHA1 (MAX_DRAWS_INDIRECT*3)
		uint materialBaseSlot = BaseMaterialBuffer(alpha1_opaque0 == 1, VIEW_CAMERA);
		
		// potential results for materialBaseSlot + drawID are
		// 0 - 299 - shadow alpha
		// 300 - 599 - shadow no alpha
		// 600 - 899 - camera alpha
		uint materialID = fsDataPerFrame.indirectMaterialBuffer[materialBaseSlot + drawID];
		
		// Get textures from arrays.
		texture2d<float> diffuseMap = fsData.diffuseMaps[materialID];
		texture2d<float> normalMap = fsData.normalMaps[materialID];
		texture2d<float> specularMap = fsData.specularMaps[materialID];

		// CALCULATE PIXEL COLOR USING INTERPOLATED ATTRIBUTES
		// Reconstruct normal map Z from X and Y
		float4 normalMapRG = normalMap.sample(fsData.textureSampler,texCoord,gradient2d(texCoordDX,texCoordDY));
        
		float3 reconstructedNormalMap;
		reconstructedNormalMap.xy = normalMapRG.ga * 2 - 1;
		reconstructedNormalMap.z = sqrt(1 - dot(reconstructedNormalMap.xy, reconstructedNormalMap.xy));

		// Calculate vertex binormal from normal and tangent
		float3 binormal = normalize(cross(tangent, normal));
		
		// Calculate pixel normal using the normal map and the tangent space vectors
		normal = reconstructedNormalMap.x * tangent + reconstructedNormalMap.y * binormal + reconstructedNormalMap.z * normal;
		
		// Sample Diffuse color
		float4 posLS = fsDataPerFrame.uniforms.transform[VIEW_SHADOW].vp * float4(position,1);
		float4 diffuseColor = diffuseMap.sample(fsData.textureSampler,texCoord,gradient2d(texCoordDX,texCoordDY));
		float4 specularData = specularMap.sample(fsData.textureSampler,texCoord,gradient2d(texCoordDX,texCoordDY));

//        return float4(diffuseColor.xyz, 1.0);
        
		float Roughness = clamp(specularData.a, 0.05f, 0.99f);
		float Metallic = specularData.b;

#if USE_AMBIENT_OCCLUSION
		float ao = fsData.aoTex.read(uint2(input.position.xy)).x;
#else
		float ao = 1.0f;
#endif

		const bool isTwoSided = (alpha1_opaque0 == 1) && (fsData.meshConstantsBuffer[materialID].twoSided == 1);
		bool isBackFace = false;

		float3 ViewVec = normalize(fsDataPerFrame.uniforms.camPos.xyz - position.xyz);

		//if it is backface
		//this should be < 0 but our mesh's edge normals are smoothed, badly
		if(isTwoSided && dot(normal, ViewVec) < 0.0)
		{
			//flip normal
			normal = -normal;
			isBackFace = true;
		}

		float3 HalfVec = normalize(ViewVec - fsDataPerFrame.uniforms.lightDir.xyz);
		float3 ReflectVec = reflect(-ViewVec, normal);
		float NoV = saturate(dot(normal, ViewVec));
		
		float NoL = dot(normal, -fsDataPerFrame.uniforms.lightDir.xyz);
		
		// Deal with two faced materials
		NoL = (isTwoSided ? abs(NoL) : saturate(NoL));

		float3 shadedColor;
		
		float3 DiffuseColor = diffuseColor.xyz;
		
		float shadowFactor = 1.0f;
		
		float fLightingMode = saturate(float(fsDataPerFrame.uniforms.lightingMode));
		
		shadedColor = calculateIllumination(
											normal,
											ViewVec,
											HalfVec,
											ReflectVec,
											NoL,
											NoV,
											fsDataPerFrame.uniforms.camPos.xyz,
											fsDataPerFrame.uniforms.esmControl,
											fsDataPerFrame.uniforms.lightDir.xyz,
											posLS,
											position,
											fsData.shadowMap,
											DiffuseColor,
											DiffuseColor,
											Roughness,
											Metallic,
											fsData.depthSampler,
											isBackFace,
											fLightingMode,
											shadowFactor);
		
		shadedColor = shadedColor * fsDataPerFrame.uniforms.lightColor.rgb * fsDataPerFrame.uniforms.lightColor.a * NoL * ao;

		// point lights
		// Find the light cluster for the current pixel
		uint2 clusterCoords = uint2(floor((input.screenPos * 0.5 + 0.5) * float2(LIGHT_CLUSTER_WIDTH, LIGHT_CLUSTER_HEIGHT)));
		
		uint numLightsInCluster = fsDataPerFrame.lightClustersCount[LIGHT_CLUSTER_COUNT_POS(clusterCoords.x, clusterCoords.y)];

		// Accumulate light contributions
		for (uint i=0; i<numLightsInCluster; i++)
		{
			uint lightId = fsDataPerFrame.lightClusters[LIGHT_CLUSTER_DATA_POS(i, clusterCoords.x, clusterCoords.y)];
			shadedColor += pointLightShade(
										   normal,
										   ViewVec,
										   HalfVec,
										   ReflectVec,
										   NoL,
										   NoV,
										   fsData.lights[lightId].position,
										   fsData.lights[lightId].color,
										   float4(fsDataPerFrame.uniforms.camPos).xyz,
										   float4(fsDataPerFrame.uniforms.lightDir).xyz,
										   posLS,
										   position,
										   DiffuseColor,
										   DiffuseColor,
										   Roughness,
										   Metallic,
										   isBackFace,
										   fLightingMode);
		}
		
		float ambientIntencity = 0.2f;
		float3 ambient = diffuseColor.xyz * ambientIntencity;
		
		float3 FinalColor = shadedColor + ambient;
		
		return float4(FinalColor, 1.0);
	}
	
	// Output final pixel color
	return float4(shadedColor, 0.0);
}

