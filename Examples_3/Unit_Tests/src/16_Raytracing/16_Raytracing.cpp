/*
* Copyright (c) 2018 Confetti Interactive Inc.
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

// Unit Test to create Bottom and Top Level Acceleration Structures using Raytracing API.

//tiny stl
#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/vector.h"
#include "../../../../Common_3/ThirdParty/OpenSource/EASTL/string.h"

//Interfaces
#include "../../../../Common_3/OS/Interfaces/ICameraController.h"
#include "../../../../Common_3/OS/Interfaces/IApp.h"
#include "../../../../Common_3/OS/Interfaces/ILog.h"
#include "../../../../Common_3/OS/Interfaces/IFileSystem.h"
#include "../../../../Common_3/OS/Interfaces/ITime.h"
#include "../../../../Common_3/OS/Interfaces/IProfiler.h"
#include "../../../../Common_3/OS/Interfaces/IInput.h"
#include "../../../../Middleware_3/UI/AppUI.h"
#include "../../../../Common_3/Renderer/IRenderer.h"
#include "../../../../Common_3/Renderer/ResourceLoader.h"

#include "../../../../Common_3/Tools/AssimpImporter/AssimpImporter.h"

// Raytracing
#include "../../../../Common_3/Renderer/IRay.h"

//Math
#include "../../../../Common_3/OS/Math/MathTypes.h"
#include "../../../../Common_3/OS/Interfaces/IMemory.h"

// The denoiser is only supported on macOS Catalina and higher. If you want to use the denoiser, set
// USE_DENOISER to 1 in the #if block below.
#if defined(METAL) && !defined(TARGET_IOS) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 101500
#define USE_DENOISER 0
#else
#define USE_DENOISER 0
#endif

ICameraController* pCameraController = NULL;
VirtualJoystickUI gVirtualJoystick;

bool gMicroProfiler = false;
UIApp gAppUI;

struct ShadersConfigBlock
{
	mat4 mCameraToWorld;
	float2 mZ1PlaneSize;
	float mProjNear;
	float mProjFarMinusNear;
	float3 mLightDirection;
	float mRandomSeed;
	float2 mSubpixelJitter;
	uint mFrameIndex;
	uint mFramesSinceCameraMove;
};

struct DenoiserUniforms {
	mat4 mWorldToCamera;
	mat4 mCameraToProjection;
	mat4 mWorldToProjectionPrevious;
	float2 mRTInvSize;
	uint mFrameIndex;
	uint mPadding;
};

//Sponza
const char*           gModel_Sponza_File = "sponza.obj";

const char* pMaterialImageFileNames[] = {
    "SponzaPBR_Textures/ao",
    "SponzaPBR_Textures/ao",
    "SponzaPBR_Textures/ao",
    "SponzaPBR_Textures/ao",
    "SponzaPBR_Textures/ao",

    //common
    "SponzaPBR_Textures/ao",
    "SponzaPBR_Textures/Dielectric_metallic",
    "SponzaPBR_Textures/Metallic_metallic",
    "SponzaPBR_Textures/gi_flag",

    //Background
    "SponzaPBR_Textures/Background/Background_Albedo",
    "SponzaPBR_Textures/Background/Background_Normal",
    "SponzaPBR_Textures/Background/Background_Roughness",

    //ChainTexture
    "SponzaPBR_Textures/ChainTexture/ChainTexture_Albedo",
    "SponzaPBR_Textures/ChainTexture/ChainTexture_Metallic",
    "SponzaPBR_Textures/ChainTexture/ChainTexture_Normal",
    "SponzaPBR_Textures/ChainTexture/ChainTexture_Roughness",

    //Lion
    "SponzaPBR_Textures/Lion/Lion_Albedo",
    "SponzaPBR_Textures/Lion/Lion_Normal",
    "SponzaPBR_Textures/Lion/Lion_Roughness",

    //Sponza_Arch
    "SponzaPBR_Textures/Sponza_Arch/Sponza_Arch_diffuse",
    "SponzaPBR_Textures/Sponza_Arch/Sponza_Arch_normal",
    "SponzaPBR_Textures/Sponza_Arch/Sponza_Arch_roughness",

    //Sponza_Bricks
    "SponzaPBR_Textures/Sponza_Bricks/Sponza_Bricks_a_Albedo",
    "SponzaPBR_Textures/Sponza_Bricks/Sponza_Bricks_a_Normal",
    "SponzaPBR_Textures/Sponza_Bricks/Sponza_Bricks_a_Roughness",

    //Sponza_Ceiling
    "SponzaPBR_Textures/Sponza_Ceiling/Sponza_Ceiling_diffuse",
    "SponzaPBR_Textures/Sponza_Ceiling/Sponza_Ceiling_normal",
    "SponzaPBR_Textures/Sponza_Ceiling/Sponza_Ceiling_roughness",

    //Sponza_Column
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_a_diffuse",
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_a_normal",
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_a_roughness",

    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_b_diffuse",
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_b_normal",
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_b_roughness",

    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_c_diffuse",
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_c_normal",
    "SponzaPBR_Textures/Sponza_Column/Sponza_Column_c_roughness",

    //Sponza_Curtain
    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Blue_diffuse",
    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Blue_normal",

    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Green_diffuse",
    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Green_normal",

    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Red_diffuse",
    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_Red_normal",

    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_metallic",
    "SponzaPBR_Textures/Sponza_Curtain/Sponza_Curtain_roughness",

    //Sponza_Details
    "SponzaPBR_Textures/Sponza_Details/Sponza_Details_diffuse",
    "SponzaPBR_Textures/Sponza_Details/Sponza_Details_metallic",
    "SponzaPBR_Textures/Sponza_Details/Sponza_Details_normal",
    "SponzaPBR_Textures/Sponza_Details/Sponza_Details_roughness",

    //Sponza_Fabric
    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Blue_diffuse",
    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Blue_normal",

    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Green_diffuse",
    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Green_normal",

    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_metallic",
    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_roughness",

    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Red_diffuse",
    "SponzaPBR_Textures/Sponza_Fabric/Sponza_Fabric_Red_normal",

    //Sponza_FlagPole
    "SponzaPBR_Textures/Sponza_FlagPole/Sponza_FlagPole_diffuse",
    "SponzaPBR_Textures/Sponza_FlagPole/Sponza_FlagPole_normal",
    "SponzaPBR_Textures/Sponza_FlagPole/Sponza_FlagPole_roughness",

    //Sponza_Floor
    "SponzaPBR_Textures/Sponza_Floor/Sponza_Floor_diffuse",
    "SponzaPBR_Textures/Sponza_Floor/Sponza_Floor_normal",
    "SponzaPBR_Textures/Sponza_Floor/Sponza_Floor_roughness",

    //Sponza_Roof
    "SponzaPBR_Textures/Sponza_Roof/Sponza_Roof_diffuse",
    "SponzaPBR_Textures/Sponza_Roof/Sponza_Roof_normal",
    "SponzaPBR_Textures/Sponza_Roof/Sponza_Roof_roughness",

    //Sponza_Thorn
    "SponzaPBR_Textures/Sponza_Thorn/Sponza_Thorn_diffuse",
    "SponzaPBR_Textures/Sponza_Thorn/Sponza_Thorn_normal",
    "SponzaPBR_Textures/Sponza_Thorn/Sponza_Thorn_roughness",

    //Vase
    "SponzaPBR_Textures/Vase/Vase_diffuse",
    "SponzaPBR_Textures/Vase/Vase_normal",
    "SponzaPBR_Textures/Vase/Vase_roughness",

    //VaseHanging
    "SponzaPBR_Textures/VaseHanging/VaseHanging_diffuse",
    "SponzaPBR_Textures/VaseHanging/VaseHanging_normal",
    "SponzaPBR_Textures/VaseHanging/VaseHanging_roughness",

    //VasePlant
    "SponzaPBR_Textures/VasePlant/VasePlant_diffuse",
    "SponzaPBR_Textures/VasePlant/VasePlant_normal",
    "SponzaPBR_Textures/VasePlant/VasePlant_roughness",

    //VaseRound
    "SponzaPBR_Textures/VaseRound/VaseRound_diffuse",
    "SponzaPBR_Textures/VaseRound/VaseRound_normal",
    "SponzaPBR_Textures/VaseRound/VaseRound_roughness",

    "lion/lion_albedo",
    "lion/lion_specular",
    "lion/lion_normal",
};

// Have a uniform for object data
struct UniformObjData
{
    mat4  mWorldMat;
    float mRoughness = 0.04f;
    float mMetallic = 0.0f;
    int   pbrMaterials = -1;
    float pad;
};

struct PropData
{
    eastl::vector<float3> PositionsData;
    eastl::vector<uint>   IndicesData;
	
    Buffer* pPositionStream;
    Buffer* pNormalStream;
    Buffer* pUVStream;
    Buffer* pIndicesStream;
    Buffer* pMaterialIdStream; // one per primitive
	Buffer* pMaterialTexturesStream; // 5 per material.
	
    Buffer*                     pConstantBuffer;
};

PropData SponzaProp;

#define TOTAL_IMGS 84
Texture* pMaterialTextures[TOTAL_IMGS];

eastl::vector<int> gSponzaTextureIndexForMaterial;

struct PathTracingData {
	mat4 mHistoryProjView;
	float3 mHistoryLightDirection;
	uint mFrameIndex;
	uint mHaltonIndex;
	uint mLastCameraMoveFrame;
};

void AssignSponzaTextures()
{
    int AO = 5;
    int NoMetallic = 6;
    int Metallic = 7;

    //00 : leaf
    gSponzaTextureIndexForMaterial.push_back(66);
    gSponzaTextureIndexForMaterial.push_back(67);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(68);
    gSponzaTextureIndexForMaterial.push_back(AO);

    //01 : vase_round
    gSponzaTextureIndexForMaterial.push_back(78);
    gSponzaTextureIndexForMaterial.push_back(79);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(80);
    gSponzaTextureIndexForMaterial.push_back(AO);

    //02 : Material__57 (Plant)
    gSponzaTextureIndexForMaterial.push_back(75);
    gSponzaTextureIndexForMaterial.push_back(76);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(77);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 03 : Material__298
    gSponzaTextureIndexForMaterial.push_back(9);
    gSponzaTextureIndexForMaterial.push_back(10);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(11);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 04 : 16___Default (gi_flag)
    gSponzaTextureIndexForMaterial.push_back(8);
    gSponzaTextureIndexForMaterial.push_back(8);    // !!!!!!
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(8);    // !!!!!
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 05 : bricks
    gSponzaTextureIndexForMaterial.push_back(22);
    gSponzaTextureIndexForMaterial.push_back(23);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(24);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 06 :  arch
    gSponzaTextureIndexForMaterial.push_back(19);
    gSponzaTextureIndexForMaterial.push_back(20);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(21);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 07 : ceiling
    gSponzaTextureIndexForMaterial.push_back(25);
    gSponzaTextureIndexForMaterial.push_back(26);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(27);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 08 : column_a
    gSponzaTextureIndexForMaterial.push_back(28);
    gSponzaTextureIndexForMaterial.push_back(29);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(30);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 09 : Floor
    gSponzaTextureIndexForMaterial.push_back(60);
    gSponzaTextureIndexForMaterial.push_back(61);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(62);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 10 : column_c
    gSponzaTextureIndexForMaterial.push_back(34);
    gSponzaTextureIndexForMaterial.push_back(35);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(36);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 11 : details
    gSponzaTextureIndexForMaterial.push_back(45);
    gSponzaTextureIndexForMaterial.push_back(47);
    gSponzaTextureIndexForMaterial.push_back(46);
    gSponzaTextureIndexForMaterial.push_back(48);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 12 : column_b
    gSponzaTextureIndexForMaterial.push_back(31);
    gSponzaTextureIndexForMaterial.push_back(32);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(33);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 13 : Material__47 - it seems missing
    gSponzaTextureIndexForMaterial.push_back(19);
    gSponzaTextureIndexForMaterial.push_back(20);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(21);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 14 : flagpole
    gSponzaTextureIndexForMaterial.push_back(57);
    gSponzaTextureIndexForMaterial.push_back(58);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(59);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 15 : fabric_e (green)
    gSponzaTextureIndexForMaterial.push_back(51);
    gSponzaTextureIndexForMaterial.push_back(52);
    gSponzaTextureIndexForMaterial.push_back(53);
    gSponzaTextureIndexForMaterial.push_back(54);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 16 : fabric_d (blue)
    gSponzaTextureIndexForMaterial.push_back(49);
    gSponzaTextureIndexForMaterial.push_back(50);
    gSponzaTextureIndexForMaterial.push_back(53);
    gSponzaTextureIndexForMaterial.push_back(54);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 17 : fabric_a (red)
    gSponzaTextureIndexForMaterial.push_back(55);
    gSponzaTextureIndexForMaterial.push_back(56);
    gSponzaTextureIndexForMaterial.push_back(53);
    gSponzaTextureIndexForMaterial.push_back(54);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 18 : fabric_g (curtain_blue)
    gSponzaTextureIndexForMaterial.push_back(37);
    gSponzaTextureIndexForMaterial.push_back(38);
    gSponzaTextureIndexForMaterial.push_back(43);
    gSponzaTextureIndexForMaterial.push_back(44);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 19 : fabric_c (curtain_red)
    gSponzaTextureIndexForMaterial.push_back(41);
    gSponzaTextureIndexForMaterial.push_back(42);
    gSponzaTextureIndexForMaterial.push_back(43);
    gSponzaTextureIndexForMaterial.push_back(44);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 20 : fabric_f (curtain_green)
    gSponzaTextureIndexForMaterial.push_back(39);
    gSponzaTextureIndexForMaterial.push_back(40);
    gSponzaTextureIndexForMaterial.push_back(43);
    gSponzaTextureIndexForMaterial.push_back(44);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 21 : chain
    gSponzaTextureIndexForMaterial.push_back(12);
    gSponzaTextureIndexForMaterial.push_back(14);
    gSponzaTextureIndexForMaterial.push_back(13);
    gSponzaTextureIndexForMaterial.push_back(15);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 22 : vase_hanging
    gSponzaTextureIndexForMaterial.push_back(72);
    gSponzaTextureIndexForMaterial.push_back(73);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(74);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 23 : vase
    gSponzaTextureIndexForMaterial.push_back(69);
    gSponzaTextureIndexForMaterial.push_back(70);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(71);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 24 : Material__25 (lion)
    gSponzaTextureIndexForMaterial.push_back(16);
    gSponzaTextureIndexForMaterial.push_back(17);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(18);
    gSponzaTextureIndexForMaterial.push_back(AO);

    // 25 : roof
    gSponzaTextureIndexForMaterial.push_back(63);
    gSponzaTextureIndexForMaterial.push_back(64);
    gSponzaTextureIndexForMaterial.push_back(NoMetallic);
    gSponzaTextureIndexForMaterial.push_back(65);
    gSponzaTextureIndexForMaterial.push_back(AO);
}

bool LoadSponza()
{
    for (int i = 0; i < TOTAL_IMGS; ++i)
    {
        PathHandle texturePath = fsCopyPathInResourceDirectory(RD_TEXTURES, pMaterialImageFileNames[i]);
        TextureLoadDesc textureDesc = {};
        textureDesc.pFilePath = texturePath;
        textureDesc.ppTexture = &pMaterialTextures[i];
        addResource(&textureDesc, true);
    }

    AssimpImporter importer;

    AssimpImporter::Model gModel_Sponza;
    PathHandle sceneFullPath = fsCopyPathInResourceDirectory(RD_MESHES, gModel_Sponza_File);
    if (!importer.ImportModel(sceneFullPath, &gModel_Sponza))
    {
        LOGF(LogLevel::eERROR, "Failed to load %s", fsGetPathFileName(sceneFullPath).buffer);
        finishResourceLoading();
        return false;
    }

    size_t meshCount = gModel_Sponza.mMeshArray.size();
    size_t sponza_matCount = gModel_Sponza.mMaterialList.size();

	size_t totalVertexCount = 0;
	size_t totalPrimitiveCount = 0;
	
	eastl::vector<float3> normals;
	eastl::vector<float2> uvs;
	eastl::vector<uint32_t> materialIds;
	
	for (size_t i = 0; i < gModel_Sponza.mMeshArray.size(); i += 1)
    {
		if (i == 4)
			continue; // Skip the large flag in the middle of the room
		
		AssimpImporter::Mesh& subMesh = gModel_Sponza.mMeshArray[i];
		totalVertexCount += subMesh.mPositions.size();
		totalPrimitiveCount += subMesh.mIndices.size() / 3;
	}

	SponzaProp.PositionsData.reserve(totalVertexCount);
	SponzaProp.IndicesData.reserve(totalPrimitiveCount * 3);
	normals.reserve(totalVertexCount);
	uvs.reserve(totalVertexCount);
	materialIds.reserve(totalPrimitiveCount);
	
	for (size_t i = 0; i < gModel_Sponza.mMeshArray.size(); i += 1)
    {
		if (i == 4)
			continue; // Skip the large flag in the middle of the room
		
		AssimpImporter::Mesh& subMesh = gModel_Sponza.mMeshArray[i];
		
		uint32_t baseVertex = (uint32_t)SponzaProp.PositionsData.size();
		for (uint32_t index : subMesh.mIndices)
			SponzaProp.IndicesData.push_back(index + baseVertex);
		
		for (float3 position : subMesh.mPositions)
			SponzaProp.PositionsData.push_back(position);
	
		for (float3 normal : subMesh.mNormals)
			normals.push_back(normal);
			
		for (float2 uv : subMesh.mUvs)
			uvs.push_back(uv);
		
		size_t meshPrimitiveCount = subMesh.mIndices.size() / 3;
		for (size_t i = 0; i < meshPrimitiveCount; i += 1)
		{
			materialIds.push_back(subMesh.mMaterialId);
		}
	}
		
	BufferLoadDesc desc = {};
	desc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
	desc.mDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER;
	desc.mDesc.mSize = totalPrimitiveCount * 3 * sizeof(uint32_t);
	desc.ppBuffer = &SponzaProp.pIndicesStream;
	desc.pData = SponzaProp.IndicesData.data();
	desc.mDesc.mFormat = TinyImageFormat_R32_UINT;
	desc.mDesc.mElementCount = totalPrimitiveCount * 3;
	desc.mDesc.mStructStride = sizeof(uint);
	addResource(&desc);

	desc.mDesc.mSize = totalVertexCount * sizeof(float3);
	desc.mDesc.mFormat = TinyImageFormat_R32G32B32_SFLOAT;
	desc.mDesc.mElementCount = totalVertexCount;
	desc.mDesc.mVertexStride = sizeof(float3);
	desc.mDesc.mStructStride = sizeof(float3);
	desc.ppBuffer = &SponzaProp.pPositionStream;
	desc.pData = SponzaProp.PositionsData.data();
	addResource(&desc);
	
	desc.mDesc.mSize = totalVertexCount * sizeof(float3);
	desc.ppBuffer = &SponzaProp.pNormalStream;
	desc.pData = normals.data();
	addResource(&desc);
	
	desc.mDesc.mSize = totalVertexCount * sizeof(float2);
	desc.mDesc.mFormat = TinyImageFormat_R32G32_SFLOAT;
	desc.mDesc.mVertexStride = sizeof(float2);
	desc.mDesc.mStructStride = sizeof(float2);
	desc.ppBuffer = &SponzaProp.pUVStream;
	desc.pData = uvs.data();
	addResource(&desc);

	desc.mDesc.mSize = totalPrimitiveCount * sizeof(uint32_t);
	desc.mDesc.mFormat = TinyImageFormat_R32_UINT;
	desc.mDesc.mElementCount = totalPrimitiveCount;
	desc.mDesc.mStructStride = sizeof(uint);
	desc.ppBuffer = &SponzaProp.pMaterialIdStream;
	desc.pData = materialIds.data();
	addResource(&desc);

    //set constant buffer for sponza
    {
        UniformObjData data = {};
        data.mWorldMat = mat4::identity();

        BufferLoadDesc desc = {};
        desc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        desc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
        desc.mDesc.mSize = sizeof(UniformObjData);
        desc.mDesc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
        desc.pData = &data;
        desc.ppBuffer = &SponzaProp.pConstantBuffer;
        addResource(&desc);
    }
	
	AssignSponzaTextures();
	
	desc.mDesc.mSize = gSponzaTextureIndexForMaterial.size() * sizeof(uint32_t);
	desc.mDesc.mElementCount = (uint32_t)gSponzaTextureIndexForMaterial.size();
	desc.ppBuffer = &SponzaProp.pMaterialTexturesStream;
	desc.pData = gSponzaTextureIndexForMaterial.data();
	addResource(&desc);
	
    finishResourceLoading();
    return true;
}


void UnloadSponza()
{
	if (SponzaProp.PositionsData.empty())
		return;
	
    for (int i = 0; i < TOTAL_IMGS; ++i)
		removeResource(pMaterialTextures[i]);

	gSponzaTextureIndexForMaterial.set_capacity(0);
	
	SponzaProp.PositionsData.set_capacity(0);
	SponzaProp.IndicesData.set_capacity(0);
	
	removeResource(SponzaProp.pPositionStream);
	removeResource(SponzaProp.pNormalStream);
	removeResource(SponzaProp.pUVStream);
	removeResource(SponzaProp.pIndicesStream);
	removeResource(SponzaProp.pMaterialIdStream);
	removeResource(SponzaProp.pMaterialTexturesStream);
	removeResource(SponzaProp.pConstantBuffer);
}

static float haltonSequence(uint index, uint base) {
    float f = 1.f;
    float r = 0.f;
    
    while (index > 0) {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
        
    }
    
    return r;
}

class UnitTest_NativeRaytracing : public IApp
{
public:
	UnitTest_NativeRaytracing()
	{
#ifdef TARGET_IOS
		mSettings.mContentScaleFactor = 1.f;
#endif
		for (int i = 0; i < argc; i += 1)
		{
			if (strcmp(argv[i], "-w") == 0 && i + 1 < argc)
				mSettings.mWidth = min(max(atoi(argv[i + 1]), 64), 10000);
			else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc)
				mSettings.mHeight = min(max(atoi(argv[i + 1]), 64), 10000);
			else if (strcmp(argv[i], "-b") == 0)
				mBenchmark = true;
		}
	}
	
	bool Init()
	{
        // FILE PATHS
        PathHandle programDirectory = fsCopyProgramDirectoryPath();
        if (!fsPlatformUsesBundledResources())
        {
            PathHandle resourceDirRoot = fsAppendPathComponent(programDirectory, "../../../src/16_Raytracing");
            fsSetResourceDirectoryRootPath(resourceDirRoot);
            
			fsSetRelativePathForResourceDirectory(RD_TEXTURES,        				 "../../../../Art/Sponza/Textures");
			fsSetRelativePathForResourceDirectory(RD_MESHES,          				 "../../../../Art/Sponza/Meshes");
            fsSetRelativePathForResourceDirectory(RD_BUILTIN_FONTS,   				 "../../UnitTestResources/Fonts");
            fsSetRelativePathForResourceDirectory(RD_ANIMATIONS,      				 "../../UnitTestResources/Animation");
            fsSetRelativePathForResourceDirectory(RD_MIDDLEWARE_TEXT, 				 "../../../../Middleware_3/Text");
            fsSetRelativePathForResourceDirectory(RD_MIDDLEWARE_UI,   				 "../../../../Middleware_3/UI");
            fsSetRelativePathForResourceDirectory(RD_MIDDLEWARE_3, 					 "../../../../Middleware_3/ParallelPrimitives");
        }
        
		if (!initInputSystem(pWindow))
			return false;

		/************************************************************************/
		// 01 Init Raytracing
		/************************************************************************/
		RendererDesc desc = {};
#ifndef DIRECT3D11
		desc.mShaderTarget = shader_target_6_3;
#endif
		initRenderer(GetName(), &desc, &pRenderer);
		initResourceLoaderInterface(pRenderer);

		QueueDesc queueDesc = {};
		queueDesc.mType = CMD_POOL_DIRECT;
		addQueue(pRenderer, &queueDesc, &pQueue);
		addCmdPool(pRenderer, pQueue, false, &pCmdPool);
		addCmd_n(pCmdPool, false, gImageCount, &ppCmds);
		addSemaphore(pRenderer, &pImageAcquiredSemaphore);
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			addFence(pRenderer, &pRenderCompleteFences[i]);
			addSemaphore(pRenderer, &pRenderCompleteSemaphores[i]);
		}

    if (!gAppUI.Init(pRenderer))
      return false;

    gAppUI.LoadFont("TitilliumText/TitilliumText-Bold.otf", RD_BUILTIN_FONTS);

		initProfiler();

		addGpuProfiler(pRenderer, pQueue, &pGpuProfiler, "GpuProfiler");

		/************************************************************************/
		// GUI
		/************************************************************************/
		GuiDesc guiDesc = {};
		guiDesc.mStartSize = vec2(300.0f, 250.0f);
		guiDesc.mStartPosition = vec2(0.0f, guiDesc.mStartSize.getY());
		pGuiWindow = gAppUI.AddGuiComponent(GetName(), &guiDesc);

		pGuiWindow->AddWidget(CheckboxWidget("Toggle Micro Profiler", &gMicroProfiler));
        /************************************************************************/
        // Blit texture
        /************************************************************************/
		ShaderMacro denoiserMacro  = { "DENOISER_ENABLED", USE_DENOISER ? "1" : "0" };
        ShaderLoadDesc displayShader = {};
        displayShader.mStages[0] = { "DisplayTexture.vert", &denoiserMacro, 1, RD_SHADER_SOURCES };
        displayShader.mStages[1] = { "DisplayTexture.frag", &denoiserMacro, 1, RD_SHADER_SOURCES };
        addShader(pRenderer, &displayShader, &pDisplayTextureShader);
        
        SamplerDesc samplerDesc = { FILTER_NEAREST,
                                    FILTER_NEAREST,
                                    MIPMAP_MODE_NEAREST,
                                    ADDRESS_MODE_CLAMP_TO_EDGE,
                                    ADDRESS_MODE_CLAMP_TO_EDGE,
                                    ADDRESS_MODE_CLAMP_TO_EDGE };
        addSampler(pRenderer, &samplerDesc, &pSampler);
        
        const char*       pStaticSamplers[] = { "uSampler0" };
        RootSignatureDesc rootDesc = {};
        rootDesc.mStaticSamplerCount = 1;
        rootDesc.ppStaticSamplerNames = pStaticSamplers;
        rootDesc.ppStaticSamplers = &pSampler;
        rootDesc.mShaderCount = 1;
        rootDesc.ppShaders = &pDisplayTextureShader;
        addRootSignature(pRenderer, &rootDesc, &pDisplayTextureSignature);

        RasterizerStateDesc rasterizerStateDesc = {};
        rasterizerStateDesc.mCullMode = CULL_MODE_NONE;
        addRasterizerState(pRenderer, &rasterizerStateDesc, &pRast);
		
		CameraMotionParameters cmp{ 200.0f, 250.0f, 300.0f };
		vec3                   camPos{ 100.0f, 300.0f, 0.0f };
		vec3                   lookAt{ 0, 340, 0 };

		pCameraController = createFpsCameraController(camPos, lookAt);

		pCameraController->setMotionParameters(cmp);
		
		bool deviceSupported = true;
		
#ifdef TARGET_IOS
		if (![pRenderer->pDevice supportsFeatureSet:MTLFeatureSet_iOS_GPUFamily4_v1])
			deviceSupported = false;
#endif
		
		if (!isRaytracingSupported(pRenderer) || !deviceSupported)
		{
			pRaytracing = NULL;
			DescriptorSetDesc setDesc = { pDisplayTextureSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
			addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTexture);
			pGuiWindow->AddWidget(LabelWidget("Raytracing is not supported on this GPU"));
			return true;
		}

		pGuiWindow->AddWidget(SliderFloatWidget("Light Direction X", &mLightDirection.x, -2.0f, 2.0f, 0.001f));
		pGuiWindow->AddWidget(SliderFloatWidget("Light Direction Y", &mLightDirection.y, -2.0f, 2.0f, 0.001f));
		pGuiWindow->AddWidget(SliderFloatWidget("Light Direction Z", &mLightDirection.z, -2.0f, 2.0f, 0.001f));
		
		/************************************************************************/
		/************************************************************************/
		
        if (!LoadSponza())
            return false;
		
		// App Actions
		InputActionDesc actionDesc = { InputBindings::BUTTON_FULLSCREEN, [](InputActionContext* ctx) { toggleFullscreen(((IApp*)ctx->pUserData)->pWindow); return true; }, this };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_EXIT, [](InputActionContext* ctx) { requestShutdown(); return true; } };
		addInputAction(&actionDesc);
		actionDesc =
		{
			InputBindings::BUTTON_ANY, [](InputActionContext* ctx)
			{
				bool capture = gAppUI.OnButton(ctx->mBinding, ctx->mBool, ctx->pPosition);
				setEnableCaptureInput(capture && INPUT_ACTION_PHASE_CANCELED != ctx->mPhase);
				return true;
			}, this
		};
		addInputAction(&actionDesc);
		typedef bool (*CameraInputHandler)(InputActionContext* ctx, uint32_t index);
		static CameraInputHandler onCameraInput = [](InputActionContext* ctx, uint32_t index)
		{
			if (!gMicroProfiler && !gAppUI.IsFocused() && *ctx->pCaptured)
			{
				gVirtualJoystick.OnMove(index, ctx->mPhase != INPUT_ACTION_PHASE_CANCELED, ctx->pPosition);
				index ? pCameraController->onRotate(ctx->mFloat2) : pCameraController->onMove(ctx->mFloat2);
			}
			return true;
		};
		actionDesc = { InputBindings::FLOAT_RIGHTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 1); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::FLOAT_LEFTSTICK, [](InputActionContext* ctx) { return onCameraInput(ctx, 0); }, NULL, 20.0f, 200.0f, 1.0f };
		addInputAction(&actionDesc);
		actionDesc = { InputBindings::BUTTON_NORTH, [](InputActionContext* ctx) { pCameraController->resetView(); return true; } };
		addInputAction(&actionDesc);
		
		/************************************************************************/
		// Raytracing setup
		/************************************************************************/
		
		initRaytracing(pRenderer, &pRaytracing);
		/************************************************************************/
		// 02 Creation Acceleration Structure
		/************************************************************************/
        

		AccelerationStructureGeometryDesc geomDesc = {};
		geomDesc.mFlags = ACCELERATION_STRUCTURE_GEOMETRY_FLAG_OPAQUE;
		geomDesc.pVertexArray = SponzaProp.PositionsData.data();
		geomDesc.vertexCount = (uint32_t)SponzaProp.PositionsData.size();
		geomDesc.pIndices32 = SponzaProp.IndicesData.data();
		geomDesc.indicesCount = (uint32_t)SponzaProp.IndicesData.size();
		geomDesc.indexType = INDEX_TYPE_UINT32;
		
		AccelerationStructureDescBottom bottomASDesc = {};
		bottomASDesc.mDescCount = 1;
		bottomASDesc.pGeometryDescs = &geomDesc;
		bottomASDesc.mFlags = ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

        AccelerationStructureDescTop topAS = {};
        topAS.mBottomASDescs = &bottomASDesc;
        topAS.mBottomASDescsCount = 1;
        
        // The transformation matrices for the instances
        mat4 transformation = mat4::identity(); // Identity
        
        //Construct descriptions for Acceleration Structures Instances
        AccelerationStructureInstanceDesc instanceDesc = {};
        
        instanceDesc.mFlags = ACCELERATION_STRUCTURE_INSTANCE_FLAG_NONE;
        instanceDesc.mInstanceContributionToHitGroupIndex = 0;
        instanceDesc.mInstanceID = 0;
        instanceDesc.mInstanceMask = 1;
        memcpy(instanceDesc.mTransform, &transformation, sizeof(float[12]));
        instanceDesc.mAccelerationStructureIndex = 0;
        
        topAS.mInstancesDescCount = 1;
        topAS.pInstanceDescs = &instanceDesc;
        addAccelerationStructure(pRaytracing, &topAS, &pSponzaAS);
        
        // Build Acceleration Structure
		RaytracingBuildASDesc buildASDesc = {};
		unsigned bottomASIndices[] = { 0 };
		buildASDesc.pAccelerationStructure = pSponzaAS;
		buildASDesc.pBottomASIndices = &bottomASIndices[0];
		buildASDesc.mBottomASIndicesCount = 1;
        beginCmd(ppCmds[0]);
		cmdBuildAccelerationStructure(ppCmds[0], pRaytracing, &buildASDesc);
        endCmd(ppCmds[0]);
        queueSubmit(pQueue, 1, &ppCmds[0], pRenderCompleteFences[0], 0, NULL, 0, NULL);
        waitForFences(pRenderer, 1, &pRenderCompleteFences[0]);
        /************************************************************************/
        // 03 - Create Raytracing Shaders
        /************************************************************************/
        {
			
			ShaderMacro denoiserMacro  = { "DENOISER_ENABLED", USE_DENOISER ? "1" : "0" };
            ShaderLoadDesc desc = {};
            desc.mStages[0] = { "RayGen.rgen", &denoiserMacro, 1, RD_SHADER_SOURCES, "rayGen"};
#ifndef DIRECT3D11
            desc.mTarget = shader_target_6_3;
#endif
            addShader(pRenderer, &desc, &pShaderRayGen);
            
            desc.mStages[0] = { "ClosestHit.rchit", &denoiserMacro, 1, RD_SHADER_SOURCES, "chs"};
            addShader(pRenderer, &desc, &pShaderClosestHit);
            
            desc.mStages[0] = { "Miss.rmiss", &denoiserMacro, 1, RD_SHADER_SOURCES, "miss"};
            addShader(pRenderer, &desc, &pShaderMiss);
            
            desc.mStages[0] = { "MissShadow.rmiss", &denoiserMacro, 1, RD_SHADER_SOURCES, "missShadow"};
            addShader(pRenderer, &desc, &pShaderMissShadow);
        }
		
		
		samplerDesc = { FILTER_LINEAR,       FILTER_LINEAR,       MIPMAP_MODE_LINEAR,
									ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT, ADDRESS_MODE_REPEAT };
		addSampler(pRenderer, &samplerDesc, &pLinearSampler);

		pStaticSamplers[0] = "linearSampler";

		Shader* pShaders[] = { pShaderRayGen, pShaderClosestHit, pShaderMiss, pShaderMissShadow };
		RootSignatureDesc signatureDesc = {};
		signatureDesc.ppShaders = pShaders;
		signatureDesc.mShaderCount = 4;
		signatureDesc.ppStaticSamplerNames = pStaticSamplers;
		signatureDesc.ppStaticSamplers = &pLinearSampler;
		signatureDesc.mStaticSamplerCount = 1;
		addRootSignature(pRenderer, &signatureDesc, &pRootSignature);
		/************************************************************************/
		// 03 - Create Raytracing Pipelines
		/************************************************************************/
		RaytracingHitGroup hitGroups[2] = {};
        hitGroups[0].pClosestHitShader    = pShaderClosestHit;
        hitGroups[0].pHitGroupName        = "hitGroup";
		
        hitGroups[1].pHitGroupName        = "missHitGroup";
        
        Shader* pMissShaders[] = { pShaderMiss, pShaderMissShadow };
		PipelineDesc rtPipelineDesc = {};
		rtPipelineDesc.mType = PIPELINE_TYPE_RAYTRACING;
        RaytracingPipelineDesc& pipelineDesc = rtPipelineDesc.mRaytracingDesc;
        pipelineDesc.mAttributeSize			= sizeof(float2);
		pipelineDesc.mMaxTraceRecursionDepth = 5;
#ifdef METAL
        pipelineDesc.mPayloadSize			= sizeof(float4) * (5 + USE_DENOISER); // The denoiser additionally stores the albedo.
#else
		pipelineDesc.mPayloadSize = sizeof(float4);
#endif
        pipelineDesc.pGlobalRootSignature	= pRootSignature;
        pipelineDesc.pRayGenShader			= pShaderRayGen;
		pipelineDesc.pRayGenRootSignature	= nullptr;// pRayGenSignature; //nullptr to bind empty LRS
        pipelineDesc.ppMissShaders			= pMissShaders;
        pipelineDesc.mMissShaderCount		= 2;
        pipelineDesc.pHitGroups				= hitGroups;
        pipelineDesc.mHitGroupCount			= 2;
		pipelineDesc.pRaytracing			= pRaytracing;
        pipelineDesc.mMaxRaysCount = mSettings.mHeight * mSettings.mWidth;
        addPipeline(pRenderer, &rtPipelineDesc, &pPipeline);
        /************************************************************************/
        // 04 - Create Shader Binding Table to connect Pipeline with Acceleration Structure
        /************************************************************************/
		BufferLoadDesc ubDesc = {};
		ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		ubDesc.mDesc.mSize = sizeof(ShadersConfigBlock);
		for (uint32_t i = 0; i < gImageCount; i++)
		{
			ubDesc.ppBuffer = &pRayGenConfigBuffer[i];
			addResource(&ubDesc);
		}

		DescriptorSetDesc setDesc = { pDisplayTextureSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
		addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetTexture);
		setDesc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_NONE, 1 };
		addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetRaytracing);
		setDesc = { pRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
		addDescriptorSet(pRenderer, &setDesc, &pDescriptorSetUniforms);
		
		const char* hitGroupNames[2] = { "hitGroup", "missHitGroup" };
		const char* missShaderNames[2] = { "miss", "missShadow" };
		
        RaytracingShaderTableDesc shaderTableDesc = {};
        shaderTableDesc.pPipeline = pPipeline;
        shaderTableDesc.pRayGenShader = "rayGen";
        shaderTableDesc.mMissShaderCount = 2;
        shaderTableDesc.pMissShaders = missShaderNames;
        shaderTableDesc.mHitGroupCount = 2;
        shaderTableDesc.pHitGroups = hitGroupNames;
        addRaytracingShaderTable(pRaytracing, &shaderTableDesc, &pShaderTable);

		DescriptorData params[1] = {};
		params[0].pName = "gSettings";
		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			params[0].ppBuffers = &pRayGenConfigBuffer[i];
			updateDescriptorSet(pRenderer, i, pDescriptorSetUniforms, 1, params);
		}
		
		return true;
	}

	void PrintBenchmarkStats()
	{
		PathHandle statsPath = fsAppendPathComponent(PathHandle(fsCopyLogFileDirectoryPath()), "Forge-Raytracer-Benchmark.txt");
		FileStream* statsFile = fsOpenFile(statsPath, FM_WRITE);
		
		if (statsFile)
		{
			fsPrintToStream(statsFile, "The Forge Raytracer:\n\n");
			fsPrintToStream(statsFile, "Width: %i\n", mSettings.mWidth);
			fsPrintToStream(statsFile, "Height: %i\n", mSettings.mHeight);
			
			fsPrintToStream(statsFile, "Frames rendered: %llu\n\n", (unsigned long long)mFrameTimes.size());
			
			double averageTime = 0.0;
			double movingAverageTime = 0.0;
			
			double minAverageTime = DBL_MAX;
			double maxAverageTime = -DBL_MAX;
			
			const size_t firstFrame = 2; // Ignore the first two frames while we're warming up.
			
			for (size_t i = firstFrame; i < mFrameTimes.size(); i += 1)
			{
				averageTime += (mFrameTimes[i] - averageTime) / (i + 1 - firstFrame);
				movingAverageTime = i > firstFrame ? (0.8 * movingAverageTime + 0.2 * mFrameTimes[i]) : mFrameTimes[i];
				
				if (movingAverageTime < minAverageTime)
					minAverageTime = movingAverageTime;
				
				if (movingAverageTime > maxAverageTime)
					maxAverageTime = movingAverageTime;
			}
			
			fsPrintToStream(statsFile, "Min/Max/Average Frame Time (ms):\n");
			fsPrintToStream(statsFile, "%.6f/%.6f/%.6f\n\n", minAverageTime, maxAverageTime, averageTime);
			
			fsPrintToStream(statsFile, "Min/Max/Average FPS:\n");
			fsPrintToStream(statsFile, "%.6f/%.6f/%.6f\n", 1.f / maxAverageTime, 1.f / minAverageTime, 1.f / averageTime);
			
			fsPrintToStream(statsFile, "\nFrame Times (ms):\n\n");
			for (size_t i = firstFrame; i < mFrameTimes.size(); i += 1)
			{
				float time = mFrameTimes[i];
				fsPrintToStream(statsFile, "%.6f\n", time * 1000.0);
			}
			mFrameTimes.set_capacity(0);
			
			fsCloseStream(statsFile);
		}
	}
	
	void Exit()
	{
		waitQueueIdle(pQueue);

		exitInputSystem();

		exitProfiler();
		
		destroyCameraController(pCameraController);

		gAppUI.Exit();
		gVirtualJoystick.Exit(); 
		UnloadSponza();

		removeGpuProfiler(pRenderer, pGpuProfiler);

		if (pRaytracing != NULL)
		{
			removeDescriptorSet(pRenderer, pDescriptorSetRaytracing);
			removeDescriptorSet(pRenderer, pDescriptorSetUniforms);

			removeRaytracingShaderTable(pRaytracing, pShaderTable);
			removePipeline(pRenderer, pPipeline);
			removeSampler(pRenderer, pLinearSampler);
			removeRootSignature(pRenderer, pRootSignature);
			for(uint32_t i = 0 ; i < gImageCount; i++)
			{
				removeResource(pRayGenConfigBuffer[i]);
			}
			removeShader(pRenderer, pShaderRayGen);
			removeShader(pRenderer, pShaderClosestHit);
			removeShader(pRenderer, pShaderMiss);
			removeShader(pRenderer, pShaderMissShadow);
			removeAccelerationStructure(pRaytracing, pSponzaAS);
			removeRaytracing(pRenderer, pRaytracing);
		}
		removeDescriptorSet(pRenderer, pDescriptorSetTexture);

		removeSampler(pRenderer, pSampler);
		removeRasterizerState(pRast);
		removeShader(pRenderer, pDisplayTextureShader);
		removeRootSignature(pRenderer, pDisplayTextureSignature);

		for (uint32_t i = 0; i < gImageCount; ++i)
		{
			removeFence(pRenderer, pRenderCompleteFences[i]);
			removeSemaphore(pRenderer, pRenderCompleteSemaphores[i]);
		}
		removeSemaphore(pRenderer, pImageAcquiredSemaphore);
		removeCmd_n(pCmdPool, gImageCount, ppCmds);
		removeCmdPool(pRenderer, pCmdPool);
		removeQueue(pQueue);
		removeResourceLoaderInterface(pRenderer);
		removeRenderer(pRenderer);
		
		if (mBenchmark)
		{
			PrintBenchmarkStats();
		}
	}

	bool Load()
	{
		/************************************************************************/
		// 04 - Create Output Resources
		/************************************************************************/
		TextureDesc uavDesc = {};
		uavDesc.mArraySize = 1;
		uavDesc.mDepth = 1;
#if USE_DENOISER
		uavDesc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
#else
		uavDesc.mFormat = TinyImageFormat_R32G32B32A32_SFLOAT;
#endif
		uavDesc.mHeight = mSettings.mHeight;
		uavDesc.mMipLevels = 1;
		uavDesc.mSampleCount = SAMPLE_COUNT_1;
		uavDesc.mStartState = RESOURCE_STATE_SHADER_RESOURCE;// RESOURCE_STATE_UNORDERED_ACCESS;
		uavDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE | DESCRIPTOR_TYPE_RW_TEXTURE;
		uavDesc.mWidth = mSettings.mWidth;
#ifdef METAL
        uavDesc.mFlags = TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT;
#endif
		TextureLoadDesc loadDesc = {};
		loadDesc.pDesc = &uavDesc;
		loadDesc.ppTexture = &pComputeOutput;
		addResource(&loadDesc);
		
#if USE_DENOISER
		uavDesc.mFormat = TinyImageFormat_B10G10R10A2_UNORM;
		loadDesc.ppTexture = &pAlbedoTexture;
		addResource(&loadDesc);
#endif

		SwapChainDesc swapChainDesc = {};
		swapChainDesc.mColorClearValue = { 1, 1, 1, 1 };
		swapChainDesc.mColorFormat = TinyImageFormat_B8G8R8A8_SRGB; // getRecommendedSwapchainFormat(true);
		swapChainDesc.mEnableVsync = false;
		swapChainDesc.mHeight = mSettings.mHeight;
		swapChainDesc.mImageCount = gImageCount;
		swapChainDesc.mSampleCount = SAMPLE_COUNT_1;
		swapChainDesc.mWidth = mSettings.mWidth;
		swapChainDesc.ppPresentQueues = &pQueue;
		swapChainDesc.mPresentQueueCount = 1;
		swapChainDesc.mWindowHandle = pWindow->handle;
		addSwapChain(pRenderer, &swapChainDesc, &pSwapChain);
		
#if USE_DENOISER
		{
			RenderTargetDesc rtDesc = {};
			rtDesc.mClearValue = { FLT_MAX, 0, 0, 0 };
			rtDesc.mWidth = mSettings.mWidth;
			rtDesc.mHeight = mSettings.mHeight;
			rtDesc.mDepth = 1;
			rtDesc.mSampleCount = SAMPLE_COUNT_1;
			rtDesc.mSampleQuality = 0;
			rtDesc.mArraySize = 1;
			
			rtDesc.mFormat = TinyImageFormat_R16G16B16A16_SFLOAT;
			addRenderTarget(pRenderer, &rtDesc, &pDepthNormalRenderTarget[0]);
			addRenderTarget(pRenderer, &rtDesc, &pDepthNormalRenderTarget[1]);
			
			rtDesc.mFormat = TinyImageFormat_R16G16_SFLOAT;
			rtDesc.mClearValue = { 0, 0 };
			addRenderTarget(pRenderer, &rtDesc, &pMotionVectorRenderTarget);
			
			rtDesc.mFormat = TinyImageFormat_D32_SFLOAT;
			rtDesc.mClearValue = { 1.0f, 0 };
			rtDesc.mFlags = TEXTURE_CREATION_FLAG_ON_TILE;
			addRenderTarget(pRenderer, &rtDesc, &pDepthRenderTarget);
			
			ShaderLoadDesc denoiserShader = {};
			denoiserShader.mStages[0] = { "DenoiserInputsPass.vert", NULL, 0, RD_SHADER_SOURCES };
			denoiserShader.mStages[1] = { "DenoiserInputsPass.frag", NULL, 0, RD_SHADER_SOURCES };
			addShader(pRenderer, &denoiserShader, &pDenoiserInputsShader);
			
			RootSignatureDesc rootSignature = {};
			rootSignature.ppShaders = &pDenoiserInputsShader;
			rootSignature.mShaderCount = 1;
			addRootSignature(pRenderer, &rootSignature, &pDenoiserInputsRootSignature);
			
			RasterizerStateDesc rasterState = {};
			rasterState.mCullMode = CULL_MODE_BACK;
			rasterState.mFrontFace = FRONT_FACE_CW;
			addRasterizerState(pRenderer, &rasterState, &pDenoiserRasterState);
			
			DepthStateDesc depthStateDesc = {};
			depthStateDesc.mDepthTest = true;
			depthStateDesc.mDepthWrite = true;
			depthStateDesc.mDepthFunc = CMP_LEQUAL;
			addDepthState(pRenderer, &depthStateDesc, &pDenoiserDepthState);
			
			PipelineDesc pipelineDesc = {};
			pipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
			
			TinyImageFormat rtFormats[] = { pDepthNormalRenderTarget[0]->pTexture->mDesc.mFormat, pMotionVectorRenderTarget->pTexture->mDesc.mFormat };
			
			VertexLayout vertexLayout = {};
			vertexLayout.mAttribCount = 0;
			GraphicsPipelineDesc& pipelineSettings = pipelineDesc.mGraphicsDesc;
			pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
			pipelineSettings.pRasterizerState = pDenoiserRasterState;
			pipelineSettings.mRenderTargetCount = 2;
			pipelineSettings.pColorFormats = rtFormats;
			pipelineSettings.mDepthStencilFormat = pDepthRenderTarget->mDesc.mFormat;
			pipelineSettings.pDepthState = pDenoiserDepthState;
			pipelineSettings.mSampleCount = SAMPLE_COUNT_1;
			pipelineSettings.mSampleQuality = 0;
			pipelineSettings.pVertexLayout = &vertexLayout;
			pipelineSettings.pRootSignature = pDenoiserInputsRootSignature;
			pipelineSettings.pShaderProgram = pDenoiserInputsShader;

			addPipeline(pRenderer, &pipelineDesc, &pDenoiserInputsPipeline);
			
			BufferLoadDesc ubDesc = {};
			ubDesc.mDesc.mDescriptors = DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			ubDesc.mDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
			ubDesc.mDesc.mSize = sizeof(DenoiserUniforms);
			for (uint32_t i = 0; i < gImageCount; i++)
			{
				ubDesc.ppBuffer = &pDenoiserInputsUniformBuffer[i];
				addResource(&ubDesc);
			}
			
			DescriptorSetDesc setDesc = { pDenoiserInputsRootSignature, DESCRIPTOR_UPDATE_FREQ_PER_FRAME, gImageCount };
			addDescriptorSet(pRenderer, &setDesc, &pDenoiserInputsDescriptorSet);
			
			DescriptorData params[1] = {};
			
			params[0].pName = "uniforms";
			for (uint32_t i = 0; i < gImageCount; ++i)
			{
				params[0].ppBuffers = &pDenoiserInputsUniformBuffer[i];
				updateDescriptorSet(pRenderer, i, pDenoiserInputsDescriptorSet, 1, params);
			}
			
			addSSVGFDenoiser(pRenderer, &pDenoiser);
		}
#endif
        
        VertexLayout vertexLayout = {};
        vertexLayout.mAttribCount = 0;
		PipelineDesc graphicsPipelineDesc = {};
		graphicsPipelineDesc.mType = PIPELINE_TYPE_GRAPHICS;
        GraphicsPipelineDesc& pipelineSettings = graphicsPipelineDesc.mGraphicsDesc;
        pipelineSettings.mPrimitiveTopo = PRIMITIVE_TOPO_TRI_LIST;
        pipelineSettings.pRasterizerState = pRast;
        pipelineSettings.mRenderTargetCount = 1;
        pipelineSettings.pColorFormats = &pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mFormat;
        pipelineSettings.mSampleCount = pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mSampleCount;
        pipelineSettings.mSampleQuality = pSwapChain->ppSwapchainRenderTargets[0]->mDesc.mSampleQuality;
        pipelineSettings.pVertexLayout = &vertexLayout;
        pipelineSettings.pRootSignature = pDisplayTextureSignature;
        pipelineSettings.pShaderProgram = pDisplayTextureShader;
        addPipeline(pRenderer, &graphicsPipelineDesc, &pDisplayTexturePipeline);
		/************************************************************************/
		/************************************************************************/
		
#ifdef TARGET_IOS
		ResourceDirectory circlePadDirectory = RD_ROOT;
#else
		ResourceDirectory circlePadDirectory = RD_TEXTURES;
#endif
		
		if (!gVirtualJoystick.Init(pRenderer, "circlepad", circlePadDirectory))
			return false;
		
		if (!gAppUI.Load(pSwapChain->ppSwapchainRenderTargets))
			return false;

		if (!gVirtualJoystick.Load(pSwapChain->ppSwapchainRenderTargets[0]))
			return false;

		loadProfiler(&gAppUI, mSettings.mWidth, mSettings.mHeight);

		
		DescriptorData params[9] = {};

		if (pRaytracing != NULL)
		{
			params[0].pName = "gOutput";
			params[0].ppTextures = &pComputeOutput;
			
			
			params[1].pName = "indices";
			params[1].ppBuffers = &SponzaProp.pIndicesStream;
			params[2].pName = "positions";
			params[2].ppBuffers = &SponzaProp.pPositionStream;
			params[3].pName = "normals";
			params[3].ppBuffers = &SponzaProp.pNormalStream;
			params[4].pName = "uvs";
			params[4].ppBuffers = &SponzaProp.pUVStream;
			params[5].pName = "materialIndices";
			params[5].ppBuffers = &SponzaProp.pMaterialIdStream;
			params[6].pName = "materialTextureIndices";
			params[6].ppBuffers = &SponzaProp.pMaterialTexturesStream;
			params[7].pName = "materialTextures";
			params[7].ppTextures = pMaterialTextures;
			params[7].mCount = TOTAL_IMGS;

#if USE_DENOISER
			params[8].pName = "gAlbedoTex";
			params[8].ppTextures = &pAlbedoTexture;
#endif
#ifndef METAL
			params[8 + USE_DENOISER].pName = "gRtScene";
			params[8 + USE_DENOISER].ppAccelerationStructures = &pSponzaAS;
			updateDescriptorSet(pRenderer, 0, pDescriptorSetRaytracing, 9 + USE_DENOISER, params);
#else
			updateDescriptorSet(pRenderer, 0, pDescriptorSetRaytracing, 8 + USE_DENOISER, params);
#endif
		}

		params[0].pName = "uTex0";
		params[0].ppTextures = &pComputeOutput;
#if USE_DENOISER
		params[1].pName = "albedoTex";
		params[1].ppTextures = &pAlbedoTexture;
#endif
		for (uint32_t i = 0; i < gImageCount; i += 1)
			updateDescriptorSet(pRenderer, i, pDescriptorSetTexture, 1 + USE_DENOISER, params);

		return true;
	}

	void Unload()
	{
		waitQueueIdle(pQueue);

		unloadProfiler();
		gAppUI.Unload();
		gVirtualJoystick.Unload();
		
		removePipeline(pRenderer, pDisplayTexturePipeline);
		removeSwapChain(pRenderer, pSwapChain);
		removeResource(pComputeOutput);
		
#if USE_DENOISER
		for (uint32_t i = 0; i < gImageCount; i += 1)
		{
			removeResource(pDenoiserInputsUniformBuffer[i]);
		}
		removeResource(pAlbedoTexture);
		
		removeDescriptorSet(pRenderer, pDenoiserInputsDescriptorSet);
		
		removeRenderTarget(pRenderer, pMotionVectorRenderTarget);
		removeRenderTarget(pRenderer, pDepthNormalRenderTarget[0]);
		removeRenderTarget(pRenderer, pDepthNormalRenderTarget[1]);
		removeRenderTarget(pRenderer, pDepthRenderTarget);
		
		removePipeline(pRenderer, pDenoiserInputsPipeline);
		removeRootSignature(pRenderer, pDenoiserInputsRootSignature);
		removeShader(pRenderer, pDenoiserInputsShader);
		removeDepthState(pDenoiserDepthState);
		removeRasterizerState(pDenoiserRasterState);
		
		removeSSVGFDenoiser(pDenoiser);
#endif
	}

	void Update(float deltaTime)
	{
		updateInputSystem(mSettings.mWidth, mSettings.mHeight);

		pCameraController->update(deltaTime);
		
		// ProfileSetDisplayMode()
		// TODO: need to change this better way 
		if (gMicroProfiler != bPrevToggleMicroProfiler)
		{
		  Profile& S = *ProfileGet();
		  int nValue = gMicroProfiler ? 1 : 0;
		  nValue = nValue >= 0 && nValue < P_DRAW_SIZE ? nValue : S.nDisplay;
		  S.nDisplay = nValue;

		  bPrevToggleMicroProfiler = gMicroProfiler;
		}

		gAppUI.Update(deltaTime);
		
		if (mBenchmark && deltaTime > 0)
			mFrameTimes.push_back(deltaTime);
	}

	void Draw()
	{
		acquireNextImage(pRenderer, pSwapChain, pImageAcquiredSemaphore, NULL, &mFrameIdx);

		FenceStatus fenceStatus = {};
		getFenceStatus(pRenderer, pRenderCompleteFences[mFrameIdx], &fenceStatus);
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
			waitForFences(pRenderer, 1, &pRenderCompleteFences[mFrameIdx]);

		if (pRaytracing != NULL)
		{
			mat4 viewMat = pCameraController->getViewMatrix();

			const float aspectInverse = (float)mSettings.mHeight / (float)mSettings.mWidth;
			const float horizontalFOV = PI / 2.0f;
			const float nearPlane = 0.1f;
			const float farPlane = 6000.f;
			mat4 projMat = mat4::perspective(horizontalFOV, aspectInverse, nearPlane, farPlane);
			mat4 projectView = projMat * viewMat;
			
			bool cameraMoved = memcmp(&projectView, &mPathTracingData.mHistoryProjView, sizeof(mat4)) != 0;
			bool lightMoved = memcmp(&mLightDirection, &mPathTracingData.mHistoryLightDirection, sizeof(float3)) != 0;
			
#if USE_DENOISER
			if (lightMoved)
			{
				clearSSVGFDenoiserTemporalHistory(pDenoiser);
			}
#else
			if (cameraMoved || lightMoved)
			{
				mPathTracingData.mFrameIndex = 0;
				mPathTracingData.mHaltonIndex = 0;
			}
#endif
			if (cameraMoved)
			{
				mPathTracingData.mLastCameraMoveFrame = mPathTracingData.mFrameIndex;
			}
			
			ShadersConfigBlock cb;
			cb.mCameraToWorld = inverse(viewMat);
			cb.mProjNear = nearPlane;
			cb.mProjFarMinusNear = farPlane - nearPlane;
			cb.mZ1PlaneSize = float2(1.0f / projMat.getElem(0, 0), 1.0f / projMat.getElem(1, 1));
			cb.mLightDirection = v3ToF3(normalize(f3Tov3(mLightDirection)));
			
			cb.mRandomSeed = (float)sin((double)getUSec());
			
			// Loop through the first 16 items in the Halton sequence.
            // The Halton sequence takes one-based indices.
            cb.mSubpixelJitter = float2(haltonSequence(mPathTracingData.mHaltonIndex + 1, 2),
                                                haltonSequence(mPathTracingData.mHaltonIndex + 1, 3));
			
			cb.mFrameIndex = mPathTracingData.mFrameIndex;
			
			cb.mFramesSinceCameraMove = mPathTracingData.mFrameIndex - mPathTracingData.mLastCameraMoveFrame;
			
			BufferUpdateDesc bufferUpdate;
			bufferUpdate.pBuffer = pRayGenConfigBuffer[mFrameIdx];
			bufferUpdate.pData = &cb;
			bufferUpdate.mSize = sizeof(cb);
			updateResource(&bufferUpdate);
			
#if USE_DENOISER
			DenoiserUniforms denoiserUniforms;
			denoiserUniforms.mWorldToCamera = viewMat;
			denoiserUniforms.mCameraToProjection = projMat; // Unjittered since the depth/normal texture needs to be stable for the denoiser.
			denoiserUniforms.mWorldToProjectionPrevious = mPathTracingData.mHistoryProjView;
			denoiserUniforms.mRTInvSize = float2(1.0f / mSettings.mWidth, 1.0f / mSettings.mHeight);
			denoiserUniforms.mFrameIndex = mPathTracingData.mFrameIndex;
			
			bufferUpdate.pBuffer = pDenoiserInputsUniformBuffer[mFrameIdx];
			bufferUpdate.pData = &denoiserUniforms;
			bufferUpdate.mSize = sizeof(denoiserUniforms);
			updateResource(&bufferUpdate);
#endif
			
			mPathTracingData.mHistoryProjView = projectView;
			mPathTracingData.mHistoryLightDirection = mLightDirection;
			mPathTracingData.mFrameIndex += 1;
			mPathTracingData.mHaltonIndex = (mPathTracingData.mHaltonIndex + 1) % 16;
		}
		
		Cmd* pCmd = ppCmds[mFrameIdx];
		beginCmd(pCmd);
		cmdBeginGpuFrameProfile(pCmd, pGpuProfiler, true);
		
#if USE_DENOISER
		if (pRaytracing != NULL)
		{
			RenderTarget* depthNormalTarget = pDepthNormalRenderTarget[mPathTracingData.mFrameIndex & 0x1];
			
			TextureBarrier barriers[] = {
				{ pDepthRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
				{ depthNormalTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
				{ pMotionVectorRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
			};
			
			cmdResourceBarrier(pCmd, 0, NULL, 3, barriers);
			
			RenderTarget* denoiserRTs[] = { depthNormalTarget, pMotionVectorRenderTarget };
			LoadActionsDesc loadActions = {};
			loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
			loadActions.mClearColorValues[0] = { FLT_MAX, 0, 0, 0 };
			loadActions.mLoadActionsColor[1] = LOAD_ACTION_CLEAR;
			loadActions.mClearColorValues[1] = { 0, 0, 0, 0 };
			loadActions.mLoadActionDepth = LOAD_ACTION_CLEAR;
			loadActions.mClearDepth = { 1.f };
			
			cmdBeginGpuTimestampQuery(pCmd, pGpuProfiler, "Generate Denoiser Inputs");
			cmdBindRenderTargets(pCmd, 2, denoiserRTs, pDepthRenderTarget, &loadActions, NULL, NULL, 0, 0);
			
			cmdBindPipeline(pCmd, pDenoiserInputsPipeline);
			
			cmdBindDescriptorSet(pCmd, mFrameIdx, pDenoiserInputsDescriptorSet);
			
			Buffer* pVertexBuffers[] = { SponzaProp.pPositionStream, SponzaProp.pNormalStream };
			cmdBindVertexBuffer(pCmd, 2, pVertexBuffers, NULL);
			
			cmdBindIndexBuffer(pCmd, SponzaProp.pIndicesStream, 0);
			cmdDrawIndexed(pCmd, (uint32_t)SponzaProp.IndicesData.size(), 0, 0);
			
			cmdBindRenderTargets(pCmd, 0, NULL, NULL, NULL, NULL, NULL, 0, 0);
			cmdEndGpuTimestampQuery(pCmd, pGpuProfiler);
		}
#endif
		
		/************************************************************************/
		// Transition UAV texture so raytracing shader can write to it
		/************************************************************************/
		cmdBeginGpuTimestampQuery(pCmd, pGpuProfiler, "Path Trace Scene", true);
		TextureBarrier uavBarrier = { pComputeOutput, RESOURCE_STATE_UNORDERED_ACCESS };
		cmdResourceBarrier(pCmd, 0, NULL, 1, &uavBarrier);
		
		/************************************************************************/
		// Perform raytracing
		/************************************************************************/
		if (pRaytracing != NULL)
		{
            cmdBindPipeline(pCmd, pPipeline);

            cmdBindDescriptorSet(pCmd, 0, pDescriptorSetRaytracing);
			cmdBindDescriptorSet(pCmd, mFrameIdx, pDescriptorSetUniforms);

			RaytracingDispatchDesc dispatchDesc = {};
			dispatchDesc.mHeight = mSettings.mHeight;
			dispatchDesc.mWidth = mSettings.mWidth;
			dispatchDesc.pShaderTable = pShaderTable;
#ifdef METAL
			dispatchDesc.pTopLevelAccelerationStructure = pSponzaAS;

            //dispatchDesc.pIndexes = { 0 };
            //dispatchDesc.pSets = { 0 };
            
            dispatchDesc.pIndexes[DESCRIPTOR_UPDATE_FREQ_NONE] = 0;
			dispatchDesc.pSets[DESCRIPTOR_UPDATE_FREQ_NONE] = pDescriptorSetRaytracing;
            dispatchDesc.pIndexes[DESCRIPTOR_UPDATE_FREQ_PER_FRAME] = mFrameIdx;
            dispatchDesc.pSets[DESCRIPTOR_UPDATE_FREQ_PER_FRAME] = pDescriptorSetUniforms;
#endif
			cmdDispatchRays(pCmd, pRaytracing, &dispatchDesc);
		}
		/************************************************************************/
		// Transition UAV to be used as source and swapchain as destination in copy operation
		/************************************************************************/
		RenderTarget* pRenderTarget = pSwapChain->ppSwapchainRenderTargets[mFrameIdx];
		TextureBarrier copyBarriers[] = {
			{ pComputeOutput, RESOURCE_STATE_SHADER_RESOURCE },
			{ pRenderTarget->pTexture, RESOURCE_STATE_RENDER_TARGET },
		};
		cmdResourceBarrier(pCmd, 0, NULL, 2, copyBarriers);
		
		Texture* pathTracedTexture = pComputeOutput;
#if USE_DENOISER
		Texture* denoisedTexture = cmdSSVGFDenoise(pCmd, pDenoiser,
						pComputeOutput,
						pMotionVectorRenderTarget->pTexture,
						pDepthNormalRenderTarget[mPathTracingData.mFrameIndex & 0x1]->pTexture,
						pDepthNormalRenderTarget[(mPathTracingData.mFrameIndex + 1) & 0x1]->pTexture);
		
		DescriptorData params[1] = {};
		params[0].pName = "uTex0";
		params[0].ppTextures = &denoisedTexture;
		updateDescriptorSet(pRenderer, mFrameIdx, pDescriptorSetTexture, 1, params);
		
		removeResource(denoisedTexture);
#endif
		
		cmdEndGpuTimestampQuery(pCmd, pGpuProfiler);
		/************************************************************************/
		// Present to screen
		/************************************************************************/
		LoadActionsDesc loadActions = {};
		loadActions.mLoadActionsColor[0] = LOAD_ACTION_CLEAR;
		loadActions.mClearColorValues[0] = pSwapChain->mDesc.mColorClearValue;
		cmdBindRenderTargets(pCmd, 1, &pRenderTarget, NULL, &loadActions, NULL, NULL, -1, -1);
		cmdSetViewport(pCmd, 0.0f, 0.0f, (float)mSettings.mWidth, (float)mSettings.mHeight, 0.0f, 1.0f);
		cmdSetScissor(pCmd, 0, 0, mSettings.mWidth, mSettings.mHeight);
        
		if (pRaytracing != NULL)
		{
			/************************************************************************/
			// Perform copy
			/************************************************************************/
			cmdBeginGpuTimestampQuery(pCmd, pGpuProfiler, "Render result", true);
			// Draw computed results
			cmdBindPipeline(pCmd, pDisplayTexturePipeline);
			cmdBindDescriptorSet(pCmd, mFrameIdx, pDescriptorSetTexture);
			cmdDraw(pCmd, 3, 0);
			cmdEndGpuTimestampQuery(pCmd, pGpuProfiler);
        }

		cmdBeginDebugMarker(pCmd, 0, 1, 0, "Draw UI");
		static HiresTimer gTimer;
		gTimer.GetUSec(true);
		
        TextDrawDesc frameTimeDraw = TextDrawDesc(0, 0xff0080ff, 18);
		
		gVirtualJoystick.Draw(pCmd, { 1.0f, 1.0f, 1.0f, 1.0f });
		
		gAppUI.DrawText(
						pCmd, float2(8, 15), eastl::string().sprintf("CPU %f ms", gTimer.GetUSecAverage() / 1000.0f).c_str(), &frameTimeDraw);
		
#if !defined(__ANDROID__)
		gAppUI.DrawText(
						pCmd, float2(8, 40), eastl::string().sprintf("GPU %f ms", (float)pGpuProfiler->mCumulativeTime * 1000.0f).c_str(),
						&frameTimeDraw);
		gAppUI.DrawDebugGpuProfile(pCmd, float2(8, 65), pGpuProfiler, NULL);
#endif
		
		cmdDrawProfiler();
		
		gAppUI.Gui(pGuiWindow);
		gAppUI.Draw(pCmd);
		
		cmdBindRenderTargets(pCmd, 0, NULL, NULL, NULL, NULL, NULL, -1, -1);

		TextureBarrier presentBarrier = { pRenderTarget->pTexture, RESOURCE_STATE_PRESENT };
		cmdResourceBarrier(pCmd, 0, NULL, 1, &presentBarrier);

		cmdEndGpuFrameProfile(pCmd, pGpuProfiler);
		
		endCmd(pCmd);
		queueSubmit(pQueue, 1, &pCmd, pRenderCompleteFences[mFrameIdx], 1, &pImageAcquiredSemaphore, 1, &pRenderCompleteSemaphores[mFrameIdx]);
		queuePresent(pQueue, pSwapChain, mFrameIdx, 1, &pRenderCompleteSemaphores[mFrameIdx]);
		flipProfiler();
		/************************************************************************/
		/************************************************************************/
	}

	const char* GetName()
	{
		return "16_Raytracing";
	}

	/************************************************************************/
	// Data
	/************************************************************************/
private:
	static const uint32_t   gImageCount = 3;
  bool           bPrevToggleMicroProfiler = false;

	eastl::vector<float> 	mFrameTimes;
	bool					mBenchmark;
	
	Renderer*			   pRenderer;
	Raytracing*			 pRaytracing;
	Queue*				  pQueue;
	CmdPool*				pCmdPool;
	Cmd**				   ppCmds;
	Fence*				  pRenderCompleteFences[gImageCount];
	Buffer*				 pRayGenConfigBuffer[gImageCount];
	AccelerationStructure*  pSponzaAS;
	Shader*	   pShaderRayGen;
	Shader*	   pShaderClosestHit;
	Shader*	   pShaderMiss;
	Shader*	   pShaderMissShadow;
    RasterizerState*        pRast;
    Shader*                 pDisplayTextureShader;
    Sampler*                pSampler;
	Sampler*				pLinearSampler;
	RootSignature*			pRootSignature;
    RootSignature*          pDisplayTextureSignature;
	DescriptorSet*          pDescriptorSetRaytracing;
	DescriptorSet*          pDescriptorSetUniforms;
	DescriptorSet*          pDescriptorSetTexture;
	Pipeline*				pPipeline;
    Pipeline*               pDisplayTexturePipeline;
	RaytracingShaderTable*  pShaderTable;
	SwapChain*				pSwapChain;
	Texture*				pComputeOutput;
	Semaphore*				pRenderCompleteSemaphores[gImageCount];
	Semaphore*				pImageAcquiredSemaphore;
	GpuProfiler*			pGpuProfiler;
	uint32_t				mFrameIdx = 0;
	PathTracingData			mPathTracingData = {};
	GuiComponent*			pGuiWindow;
	float3					mLightDirection = float3(0.2f, 0.8f, 0.1f);

#if USE_DENOISER
	Buffer*				 	pDenoiserInputsUniformBuffer[gImageCount];
	Texture*				pAlbedoTexture;
	DescriptorSet*			pDenoiserInputsDescriptorSet;
	RenderTarget*			pDepthNormalRenderTarget[2];
	RenderTarget*			pMotionVectorRenderTarget;
	RenderTarget*			pDepthRenderTarget;
	RootSignature*		 	pDenoiserInputsRootSignature;
	Shader*		 			pDenoiserInputsShader;
	DepthState*				pDenoiserDepthState;
	RasterizerState* 		pDenoiserRasterState;
	Pipeline*				pDenoiserInputsPipeline;
	SSVGFDenoiser*			pDenoiser;
#endif
};

DEFINE_APPLICATION_MAIN(UnitTest_NativeRaytracing)
