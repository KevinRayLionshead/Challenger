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

#ifndef _SHADER_DEFS_H
#define _SHADER_DEFS_H

#define LIGHT_COUNT 128
#define LIGHT_SIZE 150.0f

#define LIGHT_CLUSTER_WIDTH 8
#define LIGHT_CLUSTER_HEIGHT 8

#define LIGHT_CLUSTER_COUNT_POS(ix, iy) ( (iy*LIGHT_CLUSTER_WIDTH)+ix )
#define LIGHT_CLUSTER_DATA_POS(il, ix, iy) ( LIGHT_CLUSTER_COUNT_POS(ix, iy)*LIGHT_COUNT + il )

// This defines the amount of triangles that will be processed in parallel by the
// compute shader in the triangle filtering process.
// Should be a multiple of the wavefront size
#define CLUSTER_SIZE 256

// BATCH_COUNT limits the amount of triangle batches we can process on the GPU.
// It depends on the amoutnt of data we need to store in the constant buffers, since
// the max constant buffer size is 64KB - sizeof(SmallBatchData) * 2048 = 64KB
#define BATCH_COUNT 2048

// This defines the amount of viewports that are going to be culled in parallel.
#define NUM_CULLING_VIEWPORTS 2
#define VIEW_SHADOW 0
#define VIEW_CAMERA 1

// This value defines the amount of threads per group that will be used to clear the
// indirect draw buffers.
#define CLEAR_THREAD_COUNT 256

// The following value defines the maximum amount of indirect draw calls that will be
// drawn at once. This value depends on the number of submeshes or individual objects
// in the scene. Changing a scene will require to change this value accordingly.
#define MAX_DRAWS_INDIRECT 256

// The following values point to the position in the indirect draw buffer that holds the
// number of draw calls to draw after triangle filtering and batch compaction.
// This value number is stored in the last position of the indirect draw buffer.
// So it depends on MAX_DRAWS_INDIRECT
#define INDIRECT_DRAW_ARGUMENTS_STRUCT_NUM_ELEMENTS 8
#define DRAW_COUNTER_SLOT_POS ((MAX_DRAWS_INDIRECT-1)*INDIRECT_DRAW_ARGUMENTS_STRUCT_NUM_ELEMENTS)
#define DRAW_COUNTER_SLOT_OFFSET_IN_BYTES (DRAW_COUNTER_SLOT_POS*sizeof(uint))

// Size for the material buffer assuming each draw call uses one material index.
// The 4 values here stands for the 4 types of rendering passes used in the demo:
// alpha_tested_view0, opaque_view0, alpha_tested_view1, opaque_view1
#define MATERIAL_BUFFER_SIZE (MAX_DRAWS_INDIRECT * 2 * NUM_CULLING_VIEWPORTS)

// This function is used to get the offset of the current material base index depending
// on the type of geometry and on the culling view.
static inline uint BaseMaterialBuffer(bool alpha, uint viewID)
{
	return (viewID * 2 + (alpha ? 0 : 1)) * MAX_DRAWS_INDIRECT;
}

struct RootConstant
{
	uint drawId;
};

struct SmallBatchData
{
	uint meshIndex;	  // Index into meshConstants
	uint indexOffset;	  // Index relative to the meshConstants[meshIndex].indexOffset
	uint faceCount;	  // Number of faces in this small batch
	uint outputIndexOffset; // Offset into the output index buffer
	uint drawBatchStart;	// First slot for the current draw call
	uint accumDrawIndex;
	uint _pad0;
	uint _pad1;
};

struct MeshConstants
{
	uint	faceCount;
	uint	indexOffset;
	uint	materialID;
	uint	twoSided; //0 or 1
};

struct UncompactedDrawArguments
{
	uint numIndices;
	uint startIndex;
	uint materialID;
	uint pad_;
};

struct CullingViewPort
{
	float2 windowSize;
	uint sampleCount;
	uint _pad0;
};

struct Transform
{
#ifdef NO_HLSL_DEFINITIONS
	mat4 mvp;
	mat4 invVP;
	mat4 vp;
	mat4 projection;
#else
	float4x4 mvp;
	float4x4 invVP;
	float4x4 vp;
	float4x4 projection;
#endif
};

struct PerFrameConstants
{
	Transform transform[NUM_CULLING_VIEWPORTS];
	CullingViewPort cullingViewports[NUM_CULLING_VIEWPORTS];
	//========================================
	float4 camPos;
	//========================================
	float4 lightDir;
	//========================================
	float4 lightColor;
	//========================================
	float2 CameraPlane; //x : near, y : far
	uint lightingMode;
	uint outputMode;
	//========================================
	float2 twoOverRes;
	float esmControl;
	//========================================
};

struct LightData
{
	float3 position;
	float3 color;
};

#define UNIT_UNCOMPACTED_ARGS register(t0, UPDATE_FREQ_PER_FRAME)
#define UNIT_MATERIAL_PROPS   register(t11)
#define UNIT_VERTEX_DATA register(t12)
#define UNIT_INDEX_DATA register(t13)
#define UNIT_MESH_CONSTANTS   register(t14)
#define UNIT_BATCH_DATA_CBV register(b15)
#define UNIT_UNIFORMS_CBV register(b16, UPDATE_FREQ_PER_FRAME)
#define UNIT_INDIRECT_MATERIAL_RW register(u19, UPDATE_FREQ_PER_FRAME)
#define UNIT_INDIRECT_DRAW_ARGS_ALPHA_RW register(u20, UPDATE_FREQ_PER_FRAME)
#define UNIT_INDIRECT_DRAW_ARGS_RW register(u30, UPDATE_FREQ_PER_FRAME)
#define UNIT_UNCOMPACTED_ARGS_RW register(u40, UPDATE_FREQ_PER_FRAME)
#define UNIT_INDEX_DATA_RW register(u50, UPDATE_FREQ_PER_FRAME)
#endif
