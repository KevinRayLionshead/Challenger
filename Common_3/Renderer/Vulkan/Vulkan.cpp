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

#ifdef VULKAN

#define RENDERER_IMPLEMENTATION
#define VMA_IMPLEMENTATION
/************************************************************************/
// Debugging Macros
/************************************************************************/
// Uncomment this to enable render doc capture support
//#define USE_RENDER_DOC

// Debug Utils Extension is still WIP and does not work with all Validation Layers
// Disable this to use the old debug report and debug marker extensions
// Debug Utils requires the Nightly Build of RenderDoc
//#define USE_DEBUG_UTILS_EXTENSION
/************************************************************************/
/************************************************************************/
#if defined(_WIN32)
// Pull in minimal Windows headers
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif


#if defined(__linux__)
#define stricmp(a, b) strcasecmp(a, b)
#define vsprintf_s vsnprintf
#define strncpy_s strncpy
#endif

#include "../IRenderer.h"

#include "../../ThirdParty/OpenSource/EASTL/functional.h"
#include "../../ThirdParty/OpenSource/EASTL/sort.h"
#include "../../ThirdParty/OpenSource/EASTL/string_hash_map.h"

#include "../../OS/Interfaces/ILog.h"
#include "../../ThirdParty/OpenSource/VulkanMemoryAllocator/VulkanMemoryAllocator.h"
#include "../../OS/Core/Atomics.h"
#include "../../OS/Core/GPUConfig.h"
#include "../../ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"
#include "../../ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"
#include "VulkanCapsBuilder.h"

#if defined(VK_USE_DISPATCH_TABLES)
#include "../../../Common_3/ThirdParty/OpenSource/volk/volkForgeExt.h"
#endif

#include "../../OS/Interfaces/IMemory.h"

extern void vk_createShaderReflection(const uint8_t* shaderCode, uint32_t shaderSize, ShaderStage shaderStage, ShaderReflection* pOutReflection);
extern long vk_createBuffer(
	MemoryAllocator* pAllocator, const BufferCreateInfo* pCreateInfo, const AllocatorMemoryRequirements* pMemoryRequirements,
	Buffer* pBuffer);
extern void vk_destroyBuffer(MemoryAllocator* pAllocator, struct Buffer* pBuffer);
extern long vk_createTexture(
	MemoryAllocator* pAllocator, const TextureCreateInfo* pCreateInfo, const AllocatorMemoryRequirements* pMemoryRequirements,
	Texture* pTexture);
extern void vk_destroyTexture(MemoryAllocator* pAllocator, struct Texture* pTexture);

#ifdef ENABLE_RAYTRACING
extern void vk_addRaytracingPipeline(const RaytracingPipelineDesc*, Pipeline**);
extern void vk_FillRaytracingDescriptorData(const AccelerationStructure* pAccelerationStructure, void* pWriteNV);
#endif

// clang-format off
VkBlendOp gVkBlendOpTranslator[BlendMode::MAX_BLEND_MODES] =
{
	VK_BLEND_OP_ADD,
	VK_BLEND_OP_SUBTRACT,
	VK_BLEND_OP_REVERSE_SUBTRACT,
	VK_BLEND_OP_MIN,
	VK_BLEND_OP_MAX,
};

VkBlendFactor gVkBlendConstantTranslator[BlendConstant::MAX_BLEND_CONSTANTS] =
{
	VK_BLEND_FACTOR_ZERO,
	VK_BLEND_FACTOR_ONE,
	VK_BLEND_FACTOR_SRC_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
	VK_BLEND_FACTOR_DST_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
	VK_BLEND_FACTOR_SRC_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
	VK_BLEND_FACTOR_DST_ALPHA,
	VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
	VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
	VK_BLEND_FACTOR_CONSTANT_COLOR,
	VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
};

VkCompareOp gVkComparisonFuncTranslator[CompareMode::MAX_COMPARE_MODES] =
{
	VK_COMPARE_OP_NEVER,
	VK_COMPARE_OP_LESS,
	VK_COMPARE_OP_EQUAL,
	VK_COMPARE_OP_LESS_OR_EQUAL,
	VK_COMPARE_OP_GREATER,
	VK_COMPARE_OP_NOT_EQUAL,
	VK_COMPARE_OP_GREATER_OR_EQUAL,
	VK_COMPARE_OP_ALWAYS,
};

VkStencilOp gVkStencilOpTranslator[StencilOp::MAX_STENCIL_OPS] =
{
	VK_STENCIL_OP_KEEP,
	VK_STENCIL_OP_ZERO,
	VK_STENCIL_OP_REPLACE,
	VK_STENCIL_OP_INVERT,
	VK_STENCIL_OP_INCREMENT_AND_WRAP,
	VK_STENCIL_OP_DECREMENT_AND_WRAP,
	VK_STENCIL_OP_INCREMENT_AND_CLAMP,
	VK_STENCIL_OP_DECREMENT_AND_CLAMP,
};

VkCullModeFlagBits gVkCullModeTranslator[CullMode::MAX_CULL_MODES] =
{
	VK_CULL_MODE_NONE,
	VK_CULL_MODE_BACK_BIT,
	VK_CULL_MODE_FRONT_BIT
};

VkPolygonMode gVkFillModeTranslator[FillMode::MAX_FILL_MODES] =
{
	VK_POLYGON_MODE_FILL,
	VK_POLYGON_MODE_LINE
};

VkFrontFace gVkFrontFaceTranslator[] =
{
	VK_FRONT_FACE_COUNTER_CLOCKWISE,
	VK_FRONT_FACE_CLOCKWISE
};

static const VkFormat gVkFormatTranslator[] =
{
	VK_FORMAT_UNDEFINED,

	VK_FORMAT_R8_UNORM,
	VK_FORMAT_R8G8_UNORM,
	VK_FORMAT_UNDEFINED,  // RGB8 not directly supported
	VK_FORMAT_R8G8B8A8_UNORM,

	VK_FORMAT_R16_UNORM,
	VK_FORMAT_R16G16_UNORM,
	VK_FORMAT_UNDEFINED,  // RGB16 not directly supported
	VK_FORMAT_R16G16B16A16_UNORM,

	VK_FORMAT_R8_SNORM,
	VK_FORMAT_R8G8_SNORM,
	VK_FORMAT_UNDEFINED,  // RGB8S not directly supported
	VK_FORMAT_R8G8B8A8_SNORM,

	VK_FORMAT_R16_SNORM,
	VK_FORMAT_R16G16_SNORM,
	VK_FORMAT_UNDEFINED,  // RGB16S not directly supported
	VK_FORMAT_R16G16B16A16_SNORM,

	VK_FORMAT_R16_SFLOAT,
	VK_FORMAT_R16G16_SFLOAT,
	VK_FORMAT_UNDEFINED,  // RGB16F not directly supported
	VK_FORMAT_R16G16B16A16_SFLOAT,

	VK_FORMAT_R32_SFLOAT,
	VK_FORMAT_R32G32_SFLOAT,
	VK_FORMAT_R32G32B32_SFLOAT,
	VK_FORMAT_R32G32B32A32_SFLOAT,

	VK_FORMAT_R16_SINT,
	VK_FORMAT_R16G16_SINT,
	VK_FORMAT_UNDEFINED,  // RGB16I not directly supported
	VK_FORMAT_R16G16B16A16_SINT,

	VK_FORMAT_R32_SINT,
	VK_FORMAT_R32G32_SINT,
	VK_FORMAT_R32G32B32_SINT,
	VK_FORMAT_R32G32B32A32_SINT,

	VK_FORMAT_R16_UINT,
	VK_FORMAT_R16G16_UINT,
	VK_FORMAT_UNDEFINED,  // RGB16UI not directly supported
	VK_FORMAT_R16G16B16A16_UINT,

	VK_FORMAT_R32_UINT,
	VK_FORMAT_R32G32_UINT,
	VK_FORMAT_R32G32B32_UINT,
	VK_FORMAT_R32G32B32A32_UINT,

	VK_FORMAT_UNDEFINED,  // RGBE8 not directly supported
	VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
	VK_FORMAT_B10G11R11_UFLOAT_PACK32, // order switched in vulkan compared to DX11/12
	VK_FORMAT_B5G6R5_UNORM_PACK16,
	VK_FORMAT_UNDEFINED,  // RGBA4 not directly supported
	VK_FORMAT_A2R10G10B10_UNORM_PACK32,// order switched in vulkan compared to DX11/12
	// Depth formats
	VK_FORMAT_D16_UNORM,
	VK_FORMAT_D24_UNORM_S8_UINT,
	VK_FORMAT_D24_UNORM_S8_UINT,
	VK_FORMAT_D32_SFLOAT,
	// DXT formats
	VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
	VK_FORMAT_BC2_UNORM_BLOCK,
	VK_FORMAT_BC3_UNORM_BLOCK,
	VK_FORMAT_BC4_UNORM_BLOCK,
	VK_FORMAT_BC5_UNORM_BLOCK,
	// PVR formats
	VK_FORMAT_UNDEFINED, // PVR_2BPP = 56,
	VK_FORMAT_UNDEFINED, // PVR_2BPPA = 57,
	VK_FORMAT_UNDEFINED, // PVR_4BPP = 58,
	VK_FORMAT_UNDEFINED, // PVR_4BPPA = 59,
	VK_FORMAT_UNDEFINED, // INTZ = 60,  //  NVidia hack. Supported on all DX10+ HW
	//  XBox 360 specific fron buffer formats. NOt listed in other renderers. Please, add them when extend this structure.
	VK_FORMAT_UNDEFINED, // LE_XRGB8 = 61,
	VK_FORMAT_UNDEFINED, // LE_ARGB8 = 62,
	VK_FORMAT_UNDEFINED, // LE_X2RGB10 = 63,
	VK_FORMAT_UNDEFINED, // LE_A2RGB10 = 64,
	// compressed mobile forms
	VK_FORMAT_UNDEFINED, // ETC1 = 65,  //  RGB
	VK_FORMAT_UNDEFINED, // ATC = 66,   //  RGB
	VK_FORMAT_UNDEFINED, // ATCA = 67,  //  RGBA, explicit alpha
	VK_FORMAT_UNDEFINED, // ATCI = 68,  //  RGBA, interpolated alpha
	VK_FORMAT_UNDEFINED, // RAWZ = 69, //depth only, Nvidia (requires recombination of data) //FIX IT: PS3 as well?
	VK_FORMAT_UNDEFINED, // DF16 = 70, //depth only, Intel/AMD
	VK_FORMAT_UNDEFINED, // STENCILONLY = 71, // stencil ony usage
	// BC formats
	VK_FORMAT_BC1_RGBA_UNORM_BLOCK, // GNF_BC1    = 72,
	VK_FORMAT_BC2_UNORM_BLOCK,      // GNF_BC2    = 73,
	VK_FORMAT_BC3_UNORM_BLOCK,      // GNF_BC3    = 74,
	VK_FORMAT_BC4_UNORM_BLOCK,      // GNF_BC4    = 75,
	VK_FORMAT_BC5_UNORM_BLOCK,      // GNF_BC5    = 76,
	VK_FORMAT_BC6H_UFLOAT_BLOCK,    // GNF_BC6HUF = 77,
	VK_FORMAT_BC6H_SFLOAT_BLOCK,    // GNF_BC6HSF = 78,
	VK_FORMAT_BC7_UNORM_BLOCK,      // GNF_BC7    = 79,
	// Reveser Form
	VK_FORMAT_B8G8R8A8_UNORM, // BGRA8 = 80,
	// Extend for DXGI
	VK_FORMAT_X8_D24_UNORM_PACK32, // X8D24PAX32 = 81,
	VK_FORMAT_S8_UINT, // S8 = 82,
	VK_FORMAT_D16_UNORM_S8_UINT, // D16S8 = 83,
	VK_FORMAT_D32_SFLOAT_S8_UINT, // D32S8 = 84,
	// ASTC formats
	VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
	VK_FORMAT_ASTC_5x4_UNORM_BLOCK,
	VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
	VK_FORMAT_ASTC_6x5_UNORM_BLOCK,
	VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
	VK_FORMAT_ASTC_8x5_UNORM_BLOCK,
	VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
	VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
	VK_FORMAT_ASTC_10x5_UNORM_BLOCK,
	VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
	VK_FORMAT_ASTC_10x8_UNORM_BLOCK,
	VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
	VK_FORMAT_ASTC_12x10_UNORM_BLOCK,
	VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
};

VkAttachmentLoadOp gVkAttachmentLoadOpTranslator[LoadActionType::MAX_LOAD_ACTION] = 
{
	VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	VK_ATTACHMENT_LOAD_OP_LOAD,
	VK_ATTACHMENT_LOAD_OP_CLEAR,
};

const char* gVkWantedInstanceExtensions[] =
{
	VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
	VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_GGP)
	VK_GGP_STREAM_DESCRIPTOR_SURFACE_EXTENSION_NAME,
#endif
	// Debug utils not supported on all devices yet
#ifdef USE_DEBUG_UTILS_EXTENSION
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#else
	VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
#endif
	VK_NV_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
	// To legally use HDR formats
	VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
	/************************************************************************/
	// VR Extensions
	/************************************************************************/
	VK_KHR_DISPLAY_EXTENSION_NAME,
	VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
	/************************************************************************/
	// Multi GPU Extensions
	/************************************************************************/
#if VK_KHR_device_group_creation
	  VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,
#endif
	/************************************************************************/
	// Property querying extensions
	/************************************************************************/
	VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
	/************************************************************************/
	/************************************************************************/
};

const char* gVkWantedDeviceExtensions[] =
{
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_MAINTENANCE1_EXTENSION_NAME,
	VK_KHR_SHADER_DRAW_PARAMETERS_EXTENSION_NAME,
	VK_EXT_SHADER_SUBGROUP_BALLOT_EXTENSION_NAME,
	VK_EXT_SHADER_SUBGROUP_VOTE_EXTENSION_NAME,
	VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
	VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
#ifdef USE_EXTERNAL_MEMORY_EXTENSIONS
	VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_WIN32_EXTENSION_NAME,
#endif
#endif
	// Debug marker extension in case debug utils is not supported
#ifndef USE_DEBUG_UTILS_EXTENSION
	VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
#endif
#if defined(VK_USE_PLATFORM_GGP)
	VK_GGP_FRAME_TOKEN_EXTENSION_NAME,
#endif

#if VK_KHR_draw_indirect_count
	VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
#endif
	// Fragment shader interlock extension to be used for ROV type functionality in Vulkan
#if VK_EXT_fragment_shader_interlock
	VK_EXT_FRAGMENT_SHADER_INTERLOCK_EXTENSION_NAME,
#endif
	/************************************************************************/
	// NVIDIA Specific Extensions
	/************************************************************************/
#ifdef USE_NV_EXTENSIONS
	VK_NVX_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME,
#endif
	/************************************************************************/
	// AMD Specific Extensions
	/************************************************************************/
	VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME,
	VK_AMD_SHADER_BALLOT_EXTENSION_NAME,
	VK_AMD_GCN_SHADER_EXTENSION_NAME,
	/************************************************************************/
	// Multi GPU Extensions
	/************************************************************************/
#if VK_KHR_device_group
	VK_KHR_DEVICE_GROUP_EXTENSION_NAME,
#endif
	/************************************************************************/
	// Bindless & None Uniform access Extensions
	/************************************************************************/
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
	/************************************************************************/
	// Descriptor Update Template Extension for efficient descriptor set updates
	/************************************************************************/
	VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
	/************************************************************************/
	// Raytracing
	/************************************************************************/
#ifdef ENABLE_RAYTRACING
	VK_NV_RAY_TRACING_EXTENSION_NAME,
#endif
	/************************************************************************/
	/************************************************************************/
};
// clang-format on

#ifdef USE_DEBUG_UTILS_EXTENSION
static bool gDebugUtilsExtension = false;
#endif
static bool gRenderDocLayerEnabled = false;
static bool gDedicatedAllocationExtension = false;
static bool gExternalMemoryExtension = false;
static bool gDrawIndirectCountExtension = false;
static bool gDeviceGroupCreationExtension = false;
static bool gDescriptorIndexingExtension = false;
static bool gAMDDrawIndirectCountExtension = false;
static bool gAMDGCNShaderExtension = false;
static bool gNVRayTracingExtension = false;

static bool gDebugMarkerSupport = false;

PFN_vkCmdDrawIndirectCountKHR        pfnVkCmdDrawIndirectCountKHR = NULL;
PFN_vkCmdDrawIndexedIndirectCountKHR pfnVkCmdDrawIndexedIndirectCountKHR = NULL;
/************************************************************************/
// IMPLEMENTATION
/************************************************************************/
#if defined(RENDERER_IMPLEMENTATION)

#if !defined(VK_USE_DISPATCH_TABLES)
#ifdef _MSC_VER
#pragma comment(lib, "vulkan-1.lib")
#endif
#endif

#define SAFE_FREE(p_var)         \
	if (p_var)                   \
	{                            \
		conf_free((void*)p_var); \
	}

#if defined(__cplusplus)
#define DECLARE_ZERO(type, var) type var = {};
#else
#define DECLARE_ZERO(type, var) type var = { 0 };
#endif

// Internal utility functions (may become external one day)
VkSampleCountFlagBits util_to_vk_sample_count(SampleCount sampleCount);

// clang-format off
API_INTERFACE void FORGE_CALLCONV addBuffer(Renderer* pRenderer, const BufferDesc* pDesc, Buffer** pp_buffer);
API_INTERFACE void FORGE_CALLCONV removeBuffer(Renderer* pRenderer, Buffer* pBuffer);
API_INTERFACE void FORGE_CALLCONV addTexture(Renderer* pRenderer, const TextureDesc* pDesc, Texture** ppTexture);
API_INTERFACE void FORGE_CALLCONV removeTexture(Renderer* pRenderer, Texture* pTexture);
API_INTERFACE void FORGE_CALLCONV mapBuffer(Renderer* pRenderer, Buffer* pBuffer, ReadRange* pRange);
API_INTERFACE void FORGE_CALLCONV unmapBuffer(Renderer* pRenderer, Buffer* pBuffer);
API_INTERFACE void FORGE_CALLCONV cmdUpdateBuffer(Cmd* pCmd, Buffer* pBuffer, uint64_t dstOffset, Buffer* pSrcBuffer, uint64_t srcOffset, uint64_t size);
API_INTERFACE void FORGE_CALLCONV cmdUpdateSubresource(Cmd* pCmd, Texture* pTexture, Buffer* pSrcBuffer, SubresourceDataDesc* pSubresourceDesc);
// clang-format on

//+1 for Acceleration Structure because it is not counted by VK_DESCRIPTOR_TYPE_RANGE_SIZE
#define CONF_DESCRIPTOR_TYPE_RANGE_SIZE (VK_DESCRIPTOR_TYPE_RANGE_SIZE + 1)	
static uint32_t gDescriptorTypeRangeSize = VK_DESCRIPTOR_TYPE_RANGE_SIZE;

/************************************************************************/
// DescriptorInfo Heap Structures
/************************************************************************/
/// CPU Visible Heap to store all the resources needing CPU read / write operations - Textures/Buffers/RTV
typedef struct DescriptorPool
{
	VkDevice                        pDevice;
	VkDescriptorPool                pCurrentPool;
	VkDescriptorPoolSize*           pPoolSizes;
	eastl::vector<VkDescriptorPool> mDescriptorPools;
	uint32_t                        mPoolSizeCount;
	uint32_t                        mNumDescriptorSets;
	uint32_t                        mUsedDescriptorSetCount;
	VkDescriptorPoolCreateFlags     mFlags;
	Mutex*                          pMutex;
} DescriptorPool;

/************************************************************************/
// Static DescriptorInfo Heap Implementation
/************************************************************************/
static void add_descriptor_pool(
	Renderer* pRenderer, uint32_t numDescriptorSets, VkDescriptorPoolCreateFlags flags, VkDescriptorPoolSize* pPoolSizes,
	uint32_t numPoolSizes, DescriptorPool** ppPool)
{
	DescriptorPool* pPool = (DescriptorPool*)conf_calloc(1, sizeof(*pPool));
	pPool->mFlags = flags;
	pPool->mNumDescriptorSets = numDescriptorSets;
	pPool->mUsedDescriptorSetCount = 0;
	pPool->pDevice = pRenderer->pVkDevice;
	pPool->pMutex = (Mutex*)conf_calloc(1, sizeof(Mutex));
	pPool->pMutex->Init();

	pPool->mPoolSizeCount = numPoolSizes;
	pPool->pPoolSizes = (VkDescriptorPoolSize*)conf_calloc(numPoolSizes, sizeof(VkDescriptorPoolSize));
	for (uint32_t i = 0; i < numPoolSizes; ++i)
		pPool->pPoolSizes[i] = pPoolSizes[i];

	VkDescriptorPoolCreateInfo poolCreateInfo = {};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.pNext = NULL;
	poolCreateInfo.poolSizeCount = numPoolSizes;
	poolCreateInfo.pPoolSizes = pPoolSizes;
	poolCreateInfo.flags = flags;
	poolCreateInfo.maxSets = numDescriptorSets;

	VkResult res = vkCreateDescriptorPool(pPool->pDevice, &poolCreateInfo, NULL, &pPool->pCurrentPool);
	ASSERT(VK_SUCCESS == res);

	pPool->mDescriptorPools.emplace_back(pPool->pCurrentPool);

	*ppPool = pPool;
}

static void reset_descriptor_pool(DescriptorPool* pPool)
{
	VkResult res = vkResetDescriptorPool(pPool->pDevice, pPool->pCurrentPool, pPool->mFlags);
	pPool->mUsedDescriptorSetCount = 0;
	ASSERT(VK_SUCCESS == res);
}

static void remove_descriptor_pool(Renderer* pRenderer, DescriptorPool* pPool)
{
	for (uint32_t i = 0; i < (uint32_t)pPool->mDescriptorPools.size(); ++i)
		vkDestroyDescriptorPool(pRenderer->pVkDevice, pPool->mDescriptorPools[i], NULL);

	pPool->mDescriptorPools.~vector();

	pPool->pMutex->Destroy();
	conf_free(pPool->pMutex);
	SAFE_FREE(pPool->pPoolSizes);
	SAFE_FREE(pPool);
}

static void consume_descriptor_sets(DescriptorPool* pPool,
	const VkDescriptorSetLayout* pLayouts, VkDescriptorSet** pSets, uint32_t numDescriptorSets)
{
	// Need a lock since vkAllocateDescriptorSets needs to be externally synchronized
	// This is fine since this will only happen during Init time
	MutexLock lock(*pPool->pMutex);

	DECLARE_ZERO(VkDescriptorSetAllocateInfo, alloc_info);
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.descriptorPool = pPool->pCurrentPool;
	alloc_info.descriptorSetCount = numDescriptorSets;
	alloc_info.pSetLayouts = pLayouts;

	VkResult vk_res = vkAllocateDescriptorSets(pPool->pDevice, &alloc_info, *pSets);
	if (VK_SUCCESS != vk_res)
	{
		VkDescriptorPool pDescriptorPool = VK_NULL_HANDLE;

		VkDescriptorPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolCreateInfo.pNext = NULL;
		poolCreateInfo.poolSizeCount = pPool->mPoolSizeCount;
		poolCreateInfo.pPoolSizes = pPool->pPoolSizes;
		poolCreateInfo.flags = pPool->mFlags;
		poolCreateInfo.maxSets = pPool->mNumDescriptorSets;

		VkResult res = vkCreateDescriptorPool(pPool->pDevice, &poolCreateInfo, NULL, &pDescriptorPool);
		ASSERT(VK_SUCCESS == res);

		pPool->mDescriptorPools.emplace_back(pDescriptorPool);

		pPool->pCurrentPool = pDescriptorPool;
		pPool->mUsedDescriptorSetCount = 0;

		alloc_info.descriptorPool = pPool->pCurrentPool;
		vk_res = vkAllocateDescriptorSets(pPool->pDevice, &alloc_info, *pSets);
	}

	ASSERT(VK_SUCCESS == vk_res);

	pPool->mUsedDescriptorSetCount += numDescriptorSets;
}

/************************************************************************/
/************************************************************************/
VkPipelineBindPoint gPipelineBindPoint[PIPELINE_TYPE_COUNT] =
{
	VK_PIPELINE_BIND_POINT_MAX_ENUM,
	VK_PIPELINE_BIND_POINT_COMPUTE,
	VK_PIPELINE_BIND_POINT_GRAPHICS,
#ifdef ENABLE_RAYTRACING
	VK_PIPELINE_BIND_POINT_RAY_TRACING_NV
#endif
};

using DescriptorNameToIndexMap = eastl::string_hash_map<uint32_t>;

union DescriptorUpdateData
{
	VkDescriptorImageInfo  mImageInfo;
	VkDescriptorBufferInfo mBufferInfo;
	VkBufferView           mBuferView;
};

/************************************************************************/
// Descriptor Set Structure
/************************************************************************/
typedef struct DescriptorIndexMap
{
	eastl::string_hash_map<uint32_t> mMap;
} DescriptorIndexMap;

typedef struct DescriptorSet
{
	VkDescriptorSet*       pHandles;
	const RootSignature*   pRootSignature;
	/// Values passed to vkUpdateDescriptorSetWithTemplate. Initialized to default descriptor values.
	DescriptorUpdateData** ppUpdateData;
	uint32_t**             pDynamicOffsets;
	uint32_t**             pDynamicSizes;
	uint32_t               mMaxSets;
	uint8_t                mDynamicOffsetCount;
	uint8_t                mUpdateFrequency;
	uint8_t                mNodeIndex;
	uint8_t                mPadA;
} DescriptorSet;

static const DescriptorInfo* get_descriptor(const RootSignature* pRootSignature, const char* pResName)
{
	DescriptorNameToIndexMap::const_iterator it = pRootSignature->pDescriptorNameToIndexMap->mMap.find(pResName);
	if (it != pRootSignature->pDescriptorNameToIndexMap->mMap.end())
	{
		return &pRootSignature->pDescriptors[it->second];
	}
	else
	{
		LOGF(LogLevel::eERROR, "Invalid descriptor param (%s)", pResName);
		return NULL;
	}
}
/************************************************************************/
// Render Pass Implementation
/************************************************************************/
typedef struct RenderPassDesc
{
	TinyImageFormat*    	pColorFormats;
	const LoadActionType* pLoadActionsColor;
	bool*                 pSrgbValues;
	uint32_t              mRenderTargetCount;
	SampleCount           mSampleCount;
	TinyImageFormat     	mDepthStencilFormat;
	LoadActionType        mLoadActionDepth;
	LoadActionType        mLoadActionStencil;
} RenderPassDesc;

typedef struct RenderPass
{
	VkRenderPass   pRenderPass;
	RenderPassDesc mDesc;
} RenderPass;

typedef struct FrameBufferDesc
{
	RenderPass*    pRenderPass;
	RenderTarget** ppRenderTargets;
	RenderTarget*  pDepthStencil;
	uint32_t*      pColorArraySlices;
	uint32_t*      pColorMipSlices;
	uint32_t       mDepthArraySlice;
	uint32_t       mDepthMipSlice;
	uint32_t       mRenderTargetCount;
} FrameBufferDesc;

typedef struct FrameBuffer
{
	VkFramebuffer pFramebuffer;
	uint32_t      mWidth;
	uint32_t      mHeight;
	uint32_t      mArraySize;
} FrameBuffer;

static void add_render_pass(Renderer* pRenderer, const RenderPassDesc* pDesc, RenderPass** ppRenderPass)
{
	RenderPass* pRenderPass = (RenderPass*)conf_calloc(1, sizeof(*pRenderPass));
	pRenderPass->mDesc = *pDesc;
	/************************************************************************/
	// Add render pass
	/************************************************************************/
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	uint32_t colorAttachmentCount = pDesc->mRenderTargetCount;
	uint32_t depthAttachmentCount = (pDesc->mDepthStencilFormat != TinyImageFormat_UNDEFINED) ? 1 : 0;

	VkAttachmentDescription* attachments = NULL;
	VkAttachmentReference*   color_attachment_refs = NULL;
	VkAttachmentReference*   depth_stencil_attachment_ref = NULL;

	VkSampleCountFlagBits sample_count = util_to_vk_sample_count(pDesc->mSampleCount);

	// Fill out attachment descriptions and references
	{
		attachments = (VkAttachmentDescription*)conf_calloc(colorAttachmentCount + depthAttachmentCount, sizeof(*attachments));
		ASSERT(attachments);

		if (colorAttachmentCount > 0)
		{
			color_attachment_refs = (VkAttachmentReference*)conf_calloc(colorAttachmentCount, sizeof(*color_attachment_refs));
			ASSERT(color_attachment_refs);
		}
		if (depthAttachmentCount > 0)
		{
			depth_stencil_attachment_ref = (VkAttachmentReference*)conf_calloc(1, sizeof(*depth_stencil_attachment_ref));
			ASSERT(depth_stencil_attachment_ref);
		}

		// Color
		for (uint32_t i = 0; i < colorAttachmentCount; ++i)
		{
			const uint32_t ssidx = i;

			// descriptions
			attachments[ssidx].flags = 0;
			attachments[ssidx].format = (VkFormat)TinyImageFormat_ToVkFormat(pDesc->pColorFormats[i]);
			attachments[ssidx].samples = sample_count;
			attachments[ssidx].loadOp =
				pDesc->pLoadActionsColor ? gVkAttachmentLoadOpTranslator[pDesc->pLoadActionsColor[i]] : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[ssidx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[ssidx].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachments[ssidx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachments[ssidx].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			attachments[ssidx].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			// references
			color_attachment_refs[i].attachment = ssidx;
			color_attachment_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
	}

	// Depth stencil
	if (depthAttachmentCount > 0)
	{
		uint32_t idx = colorAttachmentCount;
		attachments[idx].flags = 0;
		attachments[idx].format = (VkFormat) TinyImageFormat_ToVkFormat(pDesc->mDepthStencilFormat);
		attachments[idx].samples = sample_count;
		attachments[idx].loadOp = gVkAttachmentLoadOpTranslator[pDesc->mLoadActionDepth];
		attachments[idx].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[idx].stencilLoadOp = gVkAttachmentLoadOpTranslator[pDesc->mLoadActionStencil];
		attachments[idx].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[idx].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		attachments[idx].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depth_stencil_attachment_ref[0].attachment = idx;
		depth_stencil_attachment_ref[0].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	DECLARE_ZERO(VkSubpassDescription, subpass);
	subpass.flags = 0;
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = colorAttachmentCount;
	subpass.pColorAttachments = color_attachment_refs;
	subpass.pResolveAttachments = NULL;
	subpass.pDepthStencilAttachment = depth_stencil_attachment_ref;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	uint32_t attachment_count = colorAttachmentCount;
	attachment_count += depthAttachmentCount;

	DECLARE_ZERO(VkRenderPassCreateInfo, create_info);
	create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	create_info.pNext = NULL;
	create_info.flags = 0;
	create_info.attachmentCount = attachment_count;
	create_info.pAttachments = attachments;
	create_info.subpassCount = 1;
	create_info.pSubpasses = &subpass;
	create_info.dependencyCount = 0;
	create_info.pDependencies = NULL;

	VkResult vk_res = vkCreateRenderPass(pRenderer->pVkDevice, &create_info, NULL, &(pRenderPass->pRenderPass));
	ASSERT(VK_SUCCESS == vk_res);

	SAFE_FREE(attachments);
	SAFE_FREE(color_attachment_refs);
	SAFE_FREE(depth_stencil_attachment_ref);

	*ppRenderPass = pRenderPass;
}

static void remove_render_pass(Renderer* pRenderer, RenderPass* pRenderPass)
{
	vkDestroyRenderPass(pRenderer->pVkDevice, pRenderPass->pRenderPass, NULL);
	SAFE_FREE(pRenderPass);
}

static void add_framebuffer(Renderer* pRenderer, const FrameBufferDesc* pDesc, FrameBuffer** ppFrameBuffer)
{
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	FrameBuffer* pFrameBuffer = (FrameBuffer*)conf_calloc(1, sizeof(*pFrameBuffer));
	ASSERT(pFrameBuffer);

	uint32_t colorAttachmentCount = pDesc->mRenderTargetCount;
	uint32_t depthAttachmentCount = (pDesc->pDepthStencil) ? 1 : 0;

	if (colorAttachmentCount)
	{
		pFrameBuffer->mWidth = pDesc->ppRenderTargets[0]->mDesc.mWidth;
		pFrameBuffer->mHeight = pDesc->ppRenderTargets[0]->mDesc.mHeight;
		if (pDesc->pColorArraySlices)
			pFrameBuffer->mArraySize = 1;
		else
			pFrameBuffer->mArraySize = pDesc->ppRenderTargets[0]->mDesc.mArraySize;
	}
	else
	{
		pFrameBuffer->mWidth = pDesc->pDepthStencil->mDesc.mWidth;
		pFrameBuffer->mHeight = pDesc->pDepthStencil->mDesc.mHeight;
		if (pDesc->mDepthArraySlice != -1)
			pFrameBuffer->mArraySize = 1;
		else
			pFrameBuffer->mArraySize = pDesc->pDepthStencil->mDesc.mArraySize;
	}

	/************************************************************************/
	// Add frame buffer
	/************************************************************************/
	uint32_t attachment_count = colorAttachmentCount;
	attachment_count += depthAttachmentCount;

	VkImageView* pImageViews = (VkImageView*)conf_calloc(attachment_count, sizeof(*pImageViews));
	ASSERT(pImageViews);

	VkImageView* iter_attachments = pImageViews;
	// Color
	for (uint32_t i = 0; i < pDesc->mRenderTargetCount; ++i)
	{
		uint32_t handle = 0;
		if (pDesc->pColorMipSlices)
		{
			if (pDesc->pColorArraySlices)
				handle = 1 + pDesc->pColorMipSlices[i] * pDesc->ppRenderTargets[i]->mDesc.mArraySize + pDesc->pColorArraySlices[i];
			else
				handle = 1 + pDesc->pColorMipSlices[i];
		}
		else if (pDesc->pColorArraySlices)
		{
			handle = 1 + pDesc->pColorArraySlices[i];
		}
		*iter_attachments = pDesc->ppRenderTargets[i]->pVkDescriptors[handle];
		++iter_attachments;
	}
	// Depth/stencil
	if (pDesc->pDepthStencil)
	{
		uint32_t handle = 0;
		if (pDesc->mDepthMipSlice != -1)
		{
			if (pDesc->mDepthArraySlice != -1)
				handle = 1 + pDesc->mDepthMipSlice * pDesc->pDepthStencil->mDesc.mArraySize + pDesc->mDepthArraySlice;
			else
				handle = 1 + pDesc->mDepthMipSlice;
		}
		else if (pDesc->mDepthArraySlice != -1)
		{
			handle = 1 + pDesc->mDepthArraySlice;
		}
		*iter_attachments = pDesc->pDepthStencil->pVkDescriptors[handle];
		++iter_attachments;
	}

	DECLARE_ZERO(VkFramebufferCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.renderPass = pDesc->pRenderPass->pRenderPass;
	add_info.attachmentCount = attachment_count;
	add_info.pAttachments = pImageViews;
	add_info.width = pFrameBuffer->mWidth;
	add_info.height = pFrameBuffer->mHeight;
	add_info.layers = pFrameBuffer->mArraySize;
	VkResult vk_res = vkCreateFramebuffer(pRenderer->pVkDevice, &add_info, NULL, &(pFrameBuffer->pFramebuffer));
	ASSERT(VK_SUCCESS == vk_res);
	SAFE_FREE(pImageViews);
	/************************************************************************/
	/************************************************************************/

	*ppFrameBuffer = pFrameBuffer;
}

static void remove_framebuffer(Renderer* pRenderer, FrameBuffer* pFrameBuffer)
{
	ASSERT(pRenderer);
	ASSERT(pFrameBuffer);

	vkDestroyFramebuffer(pRenderer->pVkDevice, pFrameBuffer->pFramebuffer, NULL);
	SAFE_FREE(pFrameBuffer);
}
/************************************************************************/
// Per Thread Render Pass synchronization logic
/************************************************************************/
/// Render-passes are not exposed to the app code since they are not available on all apis
/// This map takes care of hashing a render pass based on the render targets passed to cmdBeginRender
using RenderPassMap = eastl::hash_map<uint64_t, struct RenderPass*>;
using RenderPassMapNode = RenderPassMap::value_type;
using RenderPassMapIt = RenderPassMap::iterator;
using FrameBufferMap = eastl::hash_map<uint64_t, struct FrameBuffer*>;
using FrameBufferMapNode = FrameBufferMap::value_type;
using FrameBufferMapIt = FrameBufferMap::iterator;

// RenderPass map per thread (this will make lookups lock free and we only need a lock when inserting a RenderPass Map for the first time)
eastl::hash_map<ThreadID, RenderPassMap>* gRenderPassMap;
// FrameBuffer map per thread (this will make lookups lock free and we only need a lock when inserting a FrameBuffer map for the first time)
eastl::hash_map<ThreadID, FrameBufferMap>* gFrameBufferMap;
Mutex*                                    pRenderPassMutex;

static RenderPassMap& get_render_pass_map()
{
	eastl::hash_map<ThreadID, RenderPassMap>::iterator it = gRenderPassMap->find(Thread::GetCurrentThreadID());
	if (it == gRenderPassMap->end())
	{
		// Only need a lock when creating a new renderpass map for this thread
		MutexLock lock(*pRenderPassMutex);
		return gRenderPassMap->insert(Thread::GetCurrentThreadID()).first->second;
	}
	else
	{
		return it->second;
	}
}

static FrameBufferMap& get_frame_buffer_map()
{
	eastl::hash_map<ThreadID, FrameBufferMap>::iterator it = gFrameBufferMap->find(Thread::GetCurrentThreadID());
	if (it == gFrameBufferMap->end())
	{
		// Only need a lock when creating a new framebuffer map for this thread
		MutexLock lock(*pRenderPassMutex);
		return gFrameBufferMap->insert(Thread::GetCurrentThreadID()).first->second;
	}
	else
	{
		return it->second;
	}
}
/************************************************************************/
// Logging, Validation layer implementation
/************************************************************************/
// Proxy log callback
static void internal_log(LogType type, const char* msg, const char* component)
{
	switch (type)
	{
		case LOG_TYPE_INFO: LOGF(LogLevel::eINFO, "%s ( %s )", component, msg); break;
		case LOG_TYPE_WARN: LOGF(LogLevel::eWARNING, "%s ( %s )", component, msg); break;
		case LOG_TYPE_DEBUG: LOGF(LogLevel::eDEBUG, "%s ( %s )", component, msg); break;
		case LOG_TYPE_ERROR: LOGF(LogLevel::eERROR, "%s ( %s )", component, msg); break;
		default: break;
	}
}

#ifdef USE_DEBUG_UTILS_EXTENSION
// Debug callback for Vulkan layers
static VkBool32 VKAPI_PTR internal_debug_report_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	const char* pLayerPrefix = pCallbackData->pMessageIdName;
	const char* pMessage = pCallbackData->pMessage;
	int32_t     messageCode = pCallbackData->messageIdNumber;
	if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
	{
		LOGF(LogLevel::eINFO, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		// Code 64 is vkCmdClearAttachments issued before any draws
		// We ignore this since we dont use load store actions
		// Instead we clear the attachments in the DirectX way
		if (messageCode == 64)
			return VK_FALSE;

		LOGF(LogLevel::eWARNING, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
	{
		LOGF(LogLevel::eERROR, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}

	return VK_FALSE;
}
#else
static VKAPI_ATTR VkBool32 VKAPI_CALL internal_debug_report_callback(
																	 VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode,
																	 const char* pLayerPrefix, const char* pMessage, void* pUserData)
{
	if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT)
	{
		LOGF(LogLevel::eINFO, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}
	else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT)
	{
		LOGF(LogLevel::eWARNING, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}
	else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT)
	{
		LOGF(LogLevel::eWARNING, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}
	else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT)
	{
		LOGF(LogLevel::eERROR, "[%s] : %s (%i)", pLayerPrefix, pMessage, messageCode);
	}
	
	return VK_FALSE;
}
#endif
/************************************************************************/
// Create default resources to be used a null descriptors in case user does not specify some descriptors
/************************************************************************/
static void create_default_resources(Renderer* pRenderer)
{
	for (uint32_t i = 0; i < pRenderer->mLinkedNodeCount; ++i)
	{
		// 1D texture
		TextureDesc textureDesc = {};
		textureDesc.mNodeIndex = i;
		textureDesc.mArraySize = 1;
		textureDesc.mDepth = 1;
		textureDesc.mFormat = TinyImageFormat_R8G8B8A8_UNORM;
		textureDesc.mHeight = 1;
		textureDesc.mMipLevels = 1;
		textureDesc.mSampleCount = SAMPLE_COUNT_1;
		textureDesc.mStartState = RESOURCE_STATE_COMMON;
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		textureDesc.mWidth = 1;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_1D]);
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureUAV[i][TEXTURE_DIM_1D]);

		// 1D texture array
		textureDesc.mArraySize = 2;
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_1D_ARRAY]);
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureUAV[i][TEXTURE_DIM_1D_ARRAY]);

		// 2D texture
		textureDesc.mWidth = 2;
		textureDesc.mHeight = 2;
		textureDesc.mArraySize = 1;
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_2D]);
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureUAV[i][TEXTURE_DIM_2D]);

		// 2D MS texture
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		textureDesc.mSampleCount = SAMPLE_COUNT_2;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_2DMS]);
		textureDesc.mSampleCount = SAMPLE_COUNT_1;

		// 2D texture array
		textureDesc.mArraySize = 2;
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_2D_ARRAY]);
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureUAV[i][TEXTURE_DIM_2D_ARRAY]);

		// 2D MS texture array
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		textureDesc.mSampleCount = SAMPLE_COUNT_2;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_2DMS_ARRAY]);
		textureDesc.mSampleCount = SAMPLE_COUNT_1;

		// 3D texture
		textureDesc.mDepth = 2;
		textureDesc.mArraySize = 1;
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_3D]);
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_RW_TEXTURE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureUAV[i][TEXTURE_DIM_3D]);

		// Cube texture
		textureDesc.mWidth = 2;
		textureDesc.mHeight = 2;
		textureDesc.mDepth = 1;
		textureDesc.mArraySize = 6;
		textureDesc.mDescriptors = DESCRIPTOR_TYPE_TEXTURE_CUBE;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_CUBE]);
		textureDesc.mArraySize = 6 * 2;
		addTexture(pRenderer, &textureDesc, &pRenderer->pDefaultTextureSRV[i][TEXTURE_DIM_CUBE_ARRAY]);

		BufferDesc bufferDesc = {};
		bufferDesc.mNodeIndex = i;
		bufferDesc.mDescriptors = DESCRIPTOR_TYPE_BUFFER | DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bufferDesc.mMemoryUsage = RESOURCE_MEMORY_USAGE_GPU_ONLY;
		bufferDesc.mStartState = RESOURCE_STATE_COMMON;
		bufferDesc.mSize = sizeof(uint32_t);
		bufferDesc.mFirstElement = 0;
		bufferDesc.mElementCount = 1;
		bufferDesc.mStructStride = sizeof(uint32_t);
		bufferDesc.mFormat = TinyImageFormat_R32_UINT;
		addBuffer(pRenderer, &bufferDesc, &pRenderer->pDefaultBufferSRV[i]);
		bufferDesc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
		addBuffer(pRenderer, &bufferDesc, &pRenderer->pDefaultBufferUAV[i]);
	}

	SamplerDesc samplerDesc = {};
	samplerDesc.mAddressU = ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerDesc.mAddressV = ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerDesc.mAddressW = ADDRESS_MODE_CLAMP_TO_BORDER;
	addSampler(pRenderer, &samplerDesc, &pRenderer->pDefaultSampler);

	BlendStateDesc blendStateDesc = {};
	blendStateDesc.mDstAlphaFactors[0] = BC_ZERO;
	blendStateDesc.mDstFactors[0] = BC_ZERO;
	blendStateDesc.mSrcAlphaFactors[0] = BC_ONE;
	blendStateDesc.mSrcFactors[0] = BC_ONE;
	blendStateDesc.mMasks[0] = ALL;
	blendStateDesc.mRenderTargetMask = BLEND_STATE_TARGET_ALL;
	blendStateDesc.mIndependentBlend = false;
	addBlendState(pRenderer, &blendStateDesc, &pRenderer->pDefaultBlendState);

	DepthStateDesc depthStateDesc = {};
	depthStateDesc.mDepthFunc = CMP_LEQUAL;
	depthStateDesc.mDepthTest = false;
	depthStateDesc.mDepthWrite = false;
	depthStateDesc.mStencilBackFunc = CMP_ALWAYS;
	depthStateDesc.mStencilFrontFunc = CMP_ALWAYS;
	depthStateDesc.mStencilReadMask = 0xFF;
	depthStateDesc.mStencilWriteMask = 0xFF;
	addDepthState(pRenderer, &depthStateDesc, &pRenderer->pDefaultDepthState);

	RasterizerStateDesc rasterizerStateDesc = {};
	rasterizerStateDesc.mCullMode = CULL_MODE_BACK;
	addRasterizerState(pRenderer, &rasterizerStateDesc, &pRenderer->pDefaultRasterizerState);

	// Create command buffer to transition resources to the correct state
	Queue*   graphicsQueue = NULL;
	CmdPool* cmdPool = NULL;
	Cmd*     cmd = NULL;

	QueueDesc queueDesc = {};
	queueDesc.mType = CMD_POOL_DIRECT;
	addQueue(pRenderer, &queueDesc, &graphicsQueue);

	addCmdPool(pRenderer, graphicsQueue, false, &cmdPool);
	addCmd(cmdPool, false, &cmd);

	// Transition resources
	beginCmd(cmd);

	eastl::vector<BufferBarrier> bufferBarriers;
	eastl::vector<TextureBarrier> textureBarriers;

	for (uint32_t i = 0; i < pRenderer->mLinkedNodeCount; ++i)
	{
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim)
		{
			if (pRenderer->pDefaultTextureSRV[i][dim])
				textureBarriers.push_back(TextureBarrier{ pRenderer->pDefaultTextureSRV[i][dim], RESOURCE_STATE_SHADER_RESOURCE });

			if (pRenderer->pDefaultTextureUAV[i][dim])
				textureBarriers.push_back(TextureBarrier{ pRenderer->pDefaultTextureUAV[i][dim], RESOURCE_STATE_UNORDERED_ACCESS });
		}

		bufferBarriers.push_back(BufferBarrier{ pRenderer->pDefaultBufferSRV[i], RESOURCE_STATE_SHADER_RESOURCE, false });
		bufferBarriers.push_back(BufferBarrier{ pRenderer->pDefaultBufferUAV[i], RESOURCE_STATE_UNORDERED_ACCESS, false });
	}

	uint32_t bufferBarrierCount = (uint32_t)bufferBarriers.size();
	uint32_t textureBarrierCount = (uint32_t)textureBarriers.size();
	cmdResourceBarrier(cmd, bufferBarrierCount, bufferBarriers.data(), textureBarrierCount, textureBarriers.data());
	endCmd(cmd);

	queueSubmit(graphicsQueue, 1, &cmd, NULL, 0, NULL, 0, NULL);
	waitQueueIdle(graphicsQueue);

	// Delete command buffer
	removeCmd(cmdPool, cmd);
	removeCmdPool(pRenderer, cmdPool);
	removeQueue(graphicsQueue);
}

static void destroy_default_resources(Renderer* pRenderer)
{
	for (uint32_t i = 0; i < pRenderer->mLinkedNodeCount; ++i)
	{
		for (uint32_t dim = 0; dim < TEXTURE_DIM_COUNT; ++dim)
		{
			if (pRenderer->pDefaultTextureSRV[i][dim])
				removeTexture(pRenderer, pRenderer->pDefaultTextureSRV[i][dim]);

			if (pRenderer->pDefaultTextureUAV[i][dim])
				removeTexture(pRenderer, pRenderer->pDefaultTextureUAV[i][dim]);
		}

		removeBuffer(pRenderer, pRenderer->pDefaultBufferSRV[i]);
		removeBuffer(pRenderer, pRenderer->pDefaultBufferUAV[i]);
	}

	removeSampler(pRenderer, pRenderer->pDefaultSampler);

	removeBlendState(pRenderer->pDefaultBlendState);
	removeDepthState(pRenderer->pDefaultDepthState);
	removeRasterizerState(pRenderer->pDefaultRasterizerState);
}
/************************************************************************/
// Globals
/************************************************************************/
static tfrg_atomic64_t gRenderTargetIds = 0;
/************************************************************************/
// Internal utility functions
/************************************************************************/
VkFilter util_to_vk_filter(FilterType filter)
{
	switch (filter)
	{
		case FILTER_NEAREST: return VK_FILTER_NEAREST;
		case FILTER_LINEAR: return VK_FILTER_LINEAR;
		default: return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode util_to_vk_mip_map_mode(MipMapMode mipMapMode)
{
	switch (mipMapMode)
	{
		case MIPMAP_MODE_NEAREST: return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		case MIPMAP_MODE_LINEAR: return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		default: ASSERT(false && "Invalid Mip Map Mode"); return VK_SAMPLER_MIPMAP_MODE_MAX_ENUM;
	}
}

VkSamplerAddressMode util_to_vk_address_mode(AddressMode addressMode)
{
	switch (addressMode)
	{
		case ADDRESS_MODE_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case ADDRESS_MODE_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case ADDRESS_MODE_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case ADDRESS_MODE_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

VkShaderStageFlags util_to_vk_shader_stages(ShaderStage shader_stages)
{
	VkShaderStageFlags result = 0;
	if (SHADER_STAGE_ALL_GRAPHICS == (shader_stages & SHADER_STAGE_ALL_GRAPHICS))
	{
		result = VK_SHADER_STAGE_ALL_GRAPHICS;
	}
	else
	{
		if (SHADER_STAGE_VERT == (shader_stages & SHADER_STAGE_VERT))
		{
			result |= VK_SHADER_STAGE_VERTEX_BIT;
		}
		if (SHADER_STAGE_TESC == (shader_stages & SHADER_STAGE_TESC))
		{
			result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		}
		if (SHADER_STAGE_TESE == (shader_stages & SHADER_STAGE_TESE))
		{
			result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		}
		if (SHADER_STAGE_GEOM == (shader_stages & SHADER_STAGE_GEOM))
		{
			result |= VK_SHADER_STAGE_GEOMETRY_BIT;
		}
		if (SHADER_STAGE_FRAG == (shader_stages & SHADER_STAGE_FRAG))
		{
			result |= VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		if (SHADER_STAGE_COMP == (shader_stages & SHADER_STAGE_COMP))
		{
			result |= VK_SHADER_STAGE_COMPUTE_BIT;
		}
	}
	return result;
}

VkSampleCountFlagBits util_to_vk_sample_count(SampleCount sampleCount)
{
	VkSampleCountFlagBits result = VK_SAMPLE_COUNT_1_BIT;
	switch (sampleCount)
	{
		case SAMPLE_COUNT_1: result = VK_SAMPLE_COUNT_1_BIT; break;
		case SAMPLE_COUNT_2: result = VK_SAMPLE_COUNT_2_BIT; break;
		case SAMPLE_COUNT_4: result = VK_SAMPLE_COUNT_4_BIT; break;
		case SAMPLE_COUNT_8: result = VK_SAMPLE_COUNT_8_BIT; break;
		case SAMPLE_COUNT_16: result = VK_SAMPLE_COUNT_16_BIT; break;
	}
	return result;
}

VkBufferUsageFlags util_to_vk_buffer_usage(DescriptorType usage, bool typed)
{
	VkBufferUsageFlags result = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	if (usage & DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	{
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_RW_BUFFER)
	{
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (typed)
			result |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_BUFFER)
	{
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (typed)
			result |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_INDEX_BUFFER)
	{
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_VERTEX_BUFFER)
	{
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (usage & DESCRIPTOR_TYPE_INDIRECT_BUFFER)
	{
		result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	}
#ifdef ENABLE_RAYTRACING
	if (usage & DESCRIPTOR_TYPE_RAY_TRACING)
	{
		result |= VK_BUFFER_USAGE_RAY_TRACING_BIT_NV;
	}
#endif
	return result;
}

VkImageUsageFlags util_to_vk_image_usage(DescriptorType usage)
{
	VkImageUsageFlags result = 0;
	if (DESCRIPTOR_TYPE_TEXTURE == (usage & DESCRIPTOR_TYPE_TEXTURE))
		result |= VK_IMAGE_USAGE_SAMPLED_BIT;
	if (DESCRIPTOR_TYPE_RW_TEXTURE == (usage & DESCRIPTOR_TYPE_RW_TEXTURE))
		result |= VK_IMAGE_USAGE_STORAGE_BIT;
	return result;
}

VkAccessFlags util_to_vk_access_flags(ResourceState state)
{
	VkAccessFlags ret = 0;
	if (state & RESOURCE_STATE_COPY_SOURCE)
	{
		ret |= VK_ACCESS_TRANSFER_READ_BIT;
	}
	if (state & RESOURCE_STATE_COPY_DEST)
	{
		ret |= VK_ACCESS_TRANSFER_WRITE_BIT;
	}
	if (state & RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
	{
		ret |= VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	}
	if (state & RESOURCE_STATE_INDEX_BUFFER)
	{
		ret |= VK_ACCESS_INDEX_READ_BIT;
	}
	if (state & RESOURCE_STATE_UNORDERED_ACCESS)
	{
		ret |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	}
	if (state & RESOURCE_STATE_INDIRECT_ARGUMENT)
	{
		ret |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	}
	if (state & RESOURCE_STATE_RENDER_TARGET)
	{
		ret |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}
	if (state & RESOURCE_STATE_DEPTH_WRITE)
	{
		ret |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}
	if ((state & RESOURCE_STATE_SHADER_RESOURCE) || (state & RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
	{
		ret |= VK_ACCESS_SHADER_READ_BIT;
	}
	if (state & RESOURCE_STATE_PRESENT)
	{
		ret |= VK_ACCESS_MEMORY_READ_BIT;
	}

	return ret;
}

VkImageLayout util_to_vk_image_layout(ResourceState usage)
{
	if (usage & RESOURCE_STATE_COPY_SOURCE)
		return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	if (usage & RESOURCE_STATE_COPY_DEST)
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	if (usage & RESOURCE_STATE_RENDER_TARGET)
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	if (usage & RESOURCE_STATE_DEPTH_WRITE)
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	if (usage & RESOURCE_STATE_UNORDERED_ACCESS)
		return VK_IMAGE_LAYOUT_GENERAL;

	if ((usage & RESOURCE_STATE_SHADER_RESOURCE) || (usage & RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
		return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	if (usage & RESOURCE_STATE_PRESENT)
		return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	if (usage == RESOURCE_STATE_COMMON)
		return VK_IMAGE_LAYOUT_GENERAL;

	return VK_IMAGE_LAYOUT_UNDEFINED;
}

// Determines pipeline stages involved for given accesses
VkPipelineStageFlags util_determine_pipeline_stage_flags(VkAccessFlags accessFlags, CmdPoolType cmdPoolType)
{
	VkPipelineStageFlags flags = 0;

	switch (cmdPoolType)
	{
	case CMD_POOL_DIRECT:
	case CMD_POOL_BUNDLE:
	{
		if ((accessFlags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0)
			flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;

		if ((accessFlags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0)
		{
			flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
			flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
			flags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
			flags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
#ifdef ENABLE_RAYTRACING
			flags |= VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV;
#endif
		}

		if ((accessFlags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0)
			flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

		if ((accessFlags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0)
			flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		if ((accessFlags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
			flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	}
	case CMD_POOL_COMPUTE:
	{
		if ((accessFlags & (VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT)) != 0 ||
			(accessFlags & VK_ACCESS_INPUT_ATTACHMENT_READ_BIT) != 0 ||
			(accessFlags & (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)) != 0 ||
			(accessFlags & (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)) != 0)
			return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

		if ((accessFlags & (VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)) != 0)
			flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

		break;
	}
	case CMD_POOL_COPY:
		return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	default:
		break;
	}

	// Compatible with both compute and graphics queues
	if ((accessFlags & VK_ACCESS_INDIRECT_COMMAND_READ_BIT) != 0)
		flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

	if ((accessFlags & (VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT)) != 0)
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;

	if ((accessFlags & (VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT)) != 0)
		flags |= VK_PIPELINE_STAGE_HOST_BIT;

	if (flags == 0)
		flags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	return flags;
}

VkImageAspectFlags util_vk_determine_aspect_mask(VkFormat format, bool includeStencilBit)
{
	VkImageAspectFlags result = 0;
	switch (format)
	{
			// Depth
		case VK_FORMAT_D16_UNORM:
		case VK_FORMAT_X8_D24_UNORM_PACK32:
		case VK_FORMAT_D32_SFLOAT:
			result = VK_IMAGE_ASPECT_DEPTH_BIT;
			break;
			// Stencil
		case VK_FORMAT_S8_UINT:
			result = VK_IMAGE_ASPECT_STENCIL_BIT;
			break;
			// Depth/stencil
		case VK_FORMAT_D16_UNORM_S8_UINT:
		case VK_FORMAT_D24_UNORM_S8_UINT:
		case VK_FORMAT_D32_SFLOAT_S8_UINT:
			result = VK_IMAGE_ASPECT_DEPTH_BIT;
			if (includeStencilBit) result |= VK_IMAGE_ASPECT_STENCIL_BIT;
			break;
			// Assume everything else is Color
		default: result = VK_IMAGE_ASPECT_COLOR_BIT; break;
	}
	return result;
}

VkFormatFeatureFlags util_vk_image_usage_to_format_features(VkImageUsageFlags usage)
{
	VkFormatFeatureFlags result = (VkFormatFeatureFlags)0;
	if (VK_IMAGE_USAGE_SAMPLED_BIT == (usage & VK_IMAGE_USAGE_SAMPLED_BIT))
	{
		result |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	}
	if (VK_IMAGE_USAGE_STORAGE_BIT == (usage & VK_IMAGE_USAGE_STORAGE_BIT))
	{
		result |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
	}
	if (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT == (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
	{
		result |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	}
	if (VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT == (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
	{
		result |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	return result;
}

VkQueueFlags util_to_vk_queue_flags(CmdPoolType cmdPoolType)
{
	switch (cmdPoolType)
	{
		case CMD_POOL_DIRECT: return VK_QUEUE_GRAPHICS_BIT;
		case CMD_POOL_COPY: return VK_QUEUE_TRANSFER_BIT;
		case CMD_POOL_COMPUTE: return VK_QUEUE_COMPUTE_BIT;
		default: ASSERT(false && "Invalid Queue Type"); return VK_QUEUE_FLAG_BITS_MAX_ENUM;
	}
}

VkDescriptorType util_to_vk_descriptor_type(DescriptorType type)
{
	switch (type)
	{
		case DESCRIPTOR_TYPE_UNDEFINED: ASSERT("Invalid DescriptorInfo Type"); return VK_DESCRIPTOR_TYPE_MAX_ENUM;
		case DESCRIPTOR_TYPE_SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
		case DESCRIPTOR_TYPE_TEXTURE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case DESCRIPTOR_TYPE_UNIFORM_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case DESCRIPTOR_TYPE_RW_TEXTURE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		case DESCRIPTOR_TYPE_BUFFER:
		case DESCRIPTOR_TYPE_RW_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		case DESCRIPTOR_TYPE_TEXEL_BUFFER: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
#ifdef ENABLE_RAYTRACING
		case DESCRIPTOR_TYPE_RAY_TRACING: return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV;
#endif
		default:
			ASSERT("Invalid DescriptorInfo Type");
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
			break;
	}
}

VkShaderStageFlags util_to_vk_shader_stage_flags(ShaderStage stages)
{
	VkShaderStageFlags res = 0;
	if (stages & SHADER_STAGE_ALL_GRAPHICS)
		return VK_SHADER_STAGE_ALL_GRAPHICS;

	if (stages & SHADER_STAGE_VERT)
		res |= VK_SHADER_STAGE_VERTEX_BIT;
	if (stages & SHADER_STAGE_GEOM)
		res |= VK_SHADER_STAGE_GEOMETRY_BIT;
	if (stages & SHADER_STAGE_TESE)
		res |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	if (stages & SHADER_STAGE_TESC)
		res |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	if (stages & SHADER_STAGE_COMP)
		res |= VK_SHADER_STAGE_COMPUTE_BIT;
#ifdef ENABLE_RAYTRACING
	if (stages & SHADER_STAGE_RAYTRACING)
		res |= (
			VK_SHADER_STAGE_RAYGEN_BIT_NV |
			VK_SHADER_STAGE_ANY_HIT_BIT_NV |
			VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV |
			VK_SHADER_STAGE_MISS_BIT_NV |
			VK_SHADER_STAGE_INTERSECTION_BIT_NV |
			VK_SHADER_STAGE_CALLABLE_BIT_NV);
#endif

	ASSERT(res != 0);
	return res;
}
/************************************************************************/
// Multi GPU Helper Functions
/************************************************************************/
uint32_t util_calculate_shared_device_mask(uint32_t gpuCount) { return (1 << gpuCount) - 1; }

void util_calculate_device_indices(
	Renderer* pRenderer, uint32_t nodeIndex, uint32_t* pSharedNodeIndices, uint32_t sharedNodeIndexCount, uint32_t* pIndices)
{
	for (uint32_t i = 0; i < pRenderer->mLinkedNodeCount; ++i)
		pIndices[i] = i;

	pIndices[nodeIndex] = nodeIndex;
	/************************************************************************/
	// Set the node indices which need sharing access to the creation node
	// Example: Texture created on GPU0 but GPU1 will need to access it, GPU2 does not care
	//		  pIndices = { 0, 0, 2 }
	/************************************************************************/
	for (uint32_t i = 0; i < sharedNodeIndexCount; ++i)
		pIndices[pSharedNodeIndices[i]] = nodeIndex;
}
/************************************************************************/
// Internal init functions
/************************************************************************/
void CreateInstance(const char* app_name, Renderer* pRenderer)
{
	uint32_t              layerCount = 0;
	uint32_t              extCount = 0;
	vkEnumerateInstanceLayerProperties(&layerCount, NULL);
	vkEnumerateInstanceExtensionProperties(NULL, &extCount, NULL);

	VkLayerProperties*    layers = (VkLayerProperties*)alloca(sizeof(VkLayerProperties) * layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, layers);

	VkExtensionProperties* exts = (VkExtensionProperties*)alloca(sizeof(VkExtensionProperties) * extCount);
	vkEnumerateInstanceExtensionProperties(NULL, &extCount, exts);

	for (uint32_t i = 0; i < layerCount; ++i)
	{
		internal_log(LOG_TYPE_INFO, layers[i].layerName, "vkinstance-layer");
	}

	for (uint32_t i = 0; i < extCount; ++i)
	{
		internal_log(LOG_TYPE_INFO, exts[i].extensionName, "vkinstance-ext");
	}

	DECLARE_ZERO(VkApplicationInfo, app_info);
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pNext = NULL;
	app_info.pApplicationName = app_name;
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "TheForge";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_1;

	VkResult vk_res = VK_RESULT_MAX_ENUM;

	eastl::vector<const char*> layerTemp = eastl::vector<const char*>(pRenderer->mInstanceLayerCount);
	memcpy(layerTemp.data(), pRenderer->ppInstanceLayers, layerTemp.size() * sizeof(char*));

	// Instance
	{
		// check to see if the layers are present
		for (uint32_t i = 0; i < (uint32_t)layerTemp.size(); ++i)
		{
			bool layerFound = false;
			for (uint32_t j = 0; j < layerCount; ++j)
			{
				if (strcmp(pRenderer->ppInstanceLayers[i], layers[j].layerName) == 0)
				{
					layerFound = true;
					break;
				}
			}
			if (layerFound == false)
			{
				internal_log(LOG_TYPE_WARN, pRenderer->ppInstanceLayers[i], "vkinstance-layer-missing");
				// deleate layer and get new index
				i = (uint32_t)(
					layerTemp.erase(layerTemp.begin() + i) - layerTemp.begin());
			}
		}

		uint32_t                     extension_count = 0;
		const uint32_t               initialCount = sizeof(gVkWantedInstanceExtensions) / sizeof(gVkWantedInstanceExtensions[0]);
		const uint32_t               userRequestedCount = (uint32_t)pRenderer->mSettings.mInstanceExtensionCount;
		eastl::vector<const char*> wantedInstanceExtensions(initialCount + userRequestedCount);
		for (uint32_t i = 0; i < initialCount; ++i)
		{
			wantedInstanceExtensions[i] = gVkWantedInstanceExtensions[i];
		}
		for (uint32_t i = 0; i < userRequestedCount; ++i)
		{
			wantedInstanceExtensions[initialCount + i] = pRenderer->mSettings.ppInstanceExtensions[i];
		}
		const uint32_t wanted_extension_count = (uint32_t)wantedInstanceExtensions.size();
		// Layer extensions
		for (uint32_t i = 0; i < pRenderer->mInstanceLayerCount; ++i)
		{
			const char* layer_name = layerTemp[i];
			uint32_t    count = 0;
			vkEnumerateInstanceExtensionProperties(layer_name, &count, NULL);
			VkExtensionProperties* properties = (VkExtensionProperties*)conf_calloc(count, sizeof(*properties));
			ASSERT(properties != NULL);
			vkEnumerateInstanceExtensionProperties(layer_name, &count, properties);
			for (uint32_t j = 0; j < count; ++j)
			{
				for (uint32_t k = 0; k < wanted_extension_count; ++k)
				{
					if (strcmp(wantedInstanceExtensions[k], properties[j].extensionName) == 0)
					{
						pRenderer->gVkInstanceExtensions[extension_count++] = wantedInstanceExtensions[k];
						// clear wanted extenstion so we dont load it more then once
						wantedInstanceExtensions[k] = "";
						break;
					}
				}
			}
			SAFE_FREE((void*)properties);
		}
		// Standalone extensions
		{
			const char* layer_name = NULL;
			uint32_t    count = 0;
			vkEnumerateInstanceExtensionProperties(layer_name, &count, NULL);
			if (count > 0)
			{
				VkExtensionProperties* properties = (VkExtensionProperties*)conf_calloc(count, sizeof(*properties));
				ASSERT(properties != NULL);
				vkEnumerateInstanceExtensionProperties(layer_name, &count, properties);
				for (uint32_t j = 0; j < count; ++j)
				{
					for (uint32_t k = 0; k < wanted_extension_count; ++k)
					{
						if (strcmp(wantedInstanceExtensions[k], properties[j].extensionName) == 0)
						{
							pRenderer->gVkInstanceExtensions[extension_count++] = wantedInstanceExtensions[k];
							// clear wanted extenstion so we dont load it more then once
							//gVkWantedInstanceExtensions[k] = "";
							if (strcmp(wantedInstanceExtensions[k], VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME) == 0)
								gDeviceGroupCreationExtension = true;
#ifdef USE_DEBUG_UTILS_EXTENSION
							if (strcmp(wantedInstanceExtensions[k], VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
								gDebugUtilsExtension = true;
#endif
							break;
						}
					}
				}
				SAFE_FREE((void*)properties);
			}
		}

#if VK_HEADER_VERSION >= 108
		VkValidationFeaturesEXT validationFeaturesExt = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
		VkValidationFeatureEnableEXT enabledValidationFeatures[] =
		{
			VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		};

		if (pRenderer->mSettings.mEnableGPUBasedValidation)
		{
			validationFeaturesExt.enabledValidationFeatureCount = 1;
			validationFeaturesExt.pEnabledValidationFeatures = enabledValidationFeatures;
		}
#endif

		pRenderer->mInstanceLayerCount = (uint32_t)layerTemp.size();
		memcpy(pRenderer->ppInstanceLayers, layerTemp.data(), layerTemp.size() * sizeof(char*));

		// Add more extensions here
		DECLARE_ZERO(VkInstanceCreateInfo, create_info);
		create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#if VK_HEADER_VERSION >= 108
		create_info.pNext = &validationFeaturesExt;
#endif
		create_info.flags = 0;
		create_info.pApplicationInfo = &app_info;
		create_info.enabledLayerCount = (uint32_t)pRenderer->mInstanceLayerCount;
		create_info.ppEnabledLayerNames = pRenderer->ppInstanceLayers;
		create_info.enabledExtensionCount = extension_count;
		create_info.ppEnabledExtensionNames = pRenderer->gVkInstanceExtensions;
		vk_res = vkCreateInstance(&create_info, NULL, &(pRenderer->pVkInstance));
		ASSERT(VK_SUCCESS == vk_res);
	}

	// Load Vulkan instance functions
	volkLoadInstance(pRenderer->pVkInstance);

	// Debug
	{
#ifdef USE_DEBUG_UTILS_EXTENSION
		if (gDebugUtilsExtension)
		{
			VkDebugUtilsMessengerCreateInfoEXT create_info = {};
			create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			create_info.pfnUserCallback = internal_debug_report_callback;
			create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			create_info.flags = 0;
			create_info.pUserData = NULL;
			VkResult res = vkCreateDebugUtilsMessengerEXT(pRenderer->pVkInstance, &create_info, NULL, &(pRenderer->pVkDebugUtilsMessenger));
			if (VK_SUCCESS != res)
			{
				internal_log(
							 LOG_TYPE_ERROR, "vkCreateDebugUtilsMessengerEXT failed - disabling Vulkan debug callbacks",
							 "internal_vk_init_instance");
			}
		}
#else
		DECLARE_ZERO(VkDebugReportCallbackCreateInfoEXT, create_info);
		create_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
		create_info.pNext = NULL;
		create_info.pfnCallback = internal_debug_report_callback;
		create_info.flags = VK_DEBUG_REPORT_WARNING_BIT_EXT |
		// VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | Performance warnings are not very vaild on desktop
		VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
		VkResult res = vkCreateDebugReportCallbackEXT(pRenderer->pVkInstance, &create_info, NULL, &(pRenderer->pVkDebugReport));
		if (VK_SUCCESS != res)
		{
			internal_log(
				LOG_TYPE_ERROR, "vkCreateDebugReportCallbackEXT failed - disabling Vulkan debug callbacks", "internal_vk_init_instance");
		}
#endif
	}
}

static void RemoveInstance(Renderer* pRenderer)
{
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkInstance);

#ifdef USE_DEBUG_UTILS_EXTENSION
	if (pRenderer->pVkDebugUtilsMessenger)
	{
		vkDestroyDebugUtilsMessengerEXT(pRenderer->pVkInstance, pRenderer->pVkDebugUtilsMessenger, NULL);
		pRenderer->pVkDebugUtilsMessenger = NULL;
	}
#else
	if (pRenderer->pVkDebugReport)
	{
		vkDestroyDebugReportCallbackEXT(pRenderer->pVkInstance, pRenderer->pVkDebugReport, NULL);
		pRenderer->pVkDebugReport = NULL;
	}
#endif


	vkDestroyInstance(pRenderer->pVkInstance, NULL);
}

static void AddDevice(Renderer* pRenderer)
{
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkInstance);

	VkResult                                            vk_res = VK_RESULT_MAX_ENUM;

#if VK_KHR_device_group_creation
	VkDeviceGroupDeviceCreateInfoKHR                    deviceGroupInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO_KHR };
	eastl::vector<VkPhysicalDeviceGroupPropertiesKHR>   props;

	pRenderer->mLinkedNodeCount = 1;
	if (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED && gDeviceGroupCreationExtension)
	{
		// (not shown) fill out devCreateInfo as usual.
		uint32_t deviceGroupCount = 0;

		// Query the number of device groups
		vkEnumeratePhysicalDeviceGroupsKHR(pRenderer->pVkInstance, &deviceGroupCount, NULL);

		// Allocate and initialize structures to query the device groups
		props.resize(deviceGroupCount);
		for (uint32_t i = 0; i < deviceGroupCount; ++i)
		{
			props[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES_KHR;
			props[i].pNext = NULL;
		}
		vk_res = vkEnumeratePhysicalDeviceGroupsKHR(pRenderer->pVkInstance, &deviceGroupCount, props.data());
		ASSERT(VK_SUCCESS == vk_res);

		// If the first device group has more than one physical device. create
		// a logical device using all of the physical devices.
		for (uint32_t i = 0; i < deviceGroupCount; ++i)
		{
			if (props[i].physicalDeviceCount > 1)
			{
				deviceGroupInfo.physicalDeviceCount = props[i].physicalDeviceCount;
				deviceGroupInfo.pPhysicalDevices = props[i].physicalDevices;
				pRenderer->mLinkedNodeCount = deviceGroupInfo.physicalDeviceCount;
				break;
			}
		}
	}
#endif

	if (pRenderer->mLinkedNodeCount < 2)
	{
		pRenderer->mSettings.mGpuMode = GPU_MODE_SINGLE;
	}

	vk_res = vkEnumeratePhysicalDevices(pRenderer->pVkInstance, &(pRenderer->mNumOfGPUs), NULL);
	ASSERT(VK_SUCCESS == vk_res);
	ASSERT(pRenderer->mNumOfGPUs > 0);

	pRenderer->mNumOfGPUs = min<uint32_t>(MAX_GPUS, pRenderer->mNumOfGPUs);

	vk_res = vkEnumeratePhysicalDevices(pRenderer->pVkInstance, &(pRenderer->mNumOfGPUs), pRenderer->pVkGPUs);
	ASSERT(VK_SUCCESS == vk_res);

	/************************************************************************/
	// Select discrete gpus first
	// If we have multiple discrete gpus prefer with bigger VRAM size
	// To find VRAM in Vulkan, loop through all the heaps and find if the
	// heap has the DEVICE_LOCAL_BIT flag set
	/************************************************************************/
	auto isDeviceBetter = [pRenderer](uint32_t testIndex, uint32_t refIndex)->bool {
		VkPhysicalDeviceProperties& testProps = pRenderer->mVkGpuProperties[testIndex].properties;
		VkPhysicalDeviceProperties& refProps = pRenderer->mVkGpuProperties[refIndex].properties;

		if (testProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && refProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
		{
			return true;
		}

		if (testProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && refProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			return false;
		}

		//compare by preset if both gpu's are of same type (integrated vs discrete)
		if (testProps.vendorID == refProps.vendorID && testProps.deviceID == refProps.deviceID)
		{
			VkPhysicalDeviceMemoryProperties& testMemoryProps = pRenderer->mVkGpuMemoryProperties[testIndex];
			VkPhysicalDeviceMemoryProperties& refMemoryProps = pRenderer->mVkGpuMemoryProperties[refIndex];
			//if presets are the same then sort by vram size
			VkDeviceSize totalTestVram = 0;
			VkDeviceSize totalRefVram = 0;
			for (uint32_t i = 0; i < testMemoryProps.memoryHeapCount; ++i)
			{
				if (VK_MEMORY_HEAP_DEVICE_LOCAL_BIT & testMemoryProps.memoryHeaps[i].flags)
					totalTestVram += testMemoryProps.memoryHeaps[i].size;
			}
			for (uint32_t i = 0; i < refMemoryProps.memoryHeapCount; ++i)
			{
				if (VK_MEMORY_HEAP_DEVICE_LOCAL_BIT & refMemoryProps.memoryHeaps[i].flags)
					totalRefVram += refMemoryProps.memoryHeaps[i].size;
			}

			return totalTestVram >= totalRefVram;
		}

		return false;
	};

	uint32_t gpuIndex = UINT32_MAX;
	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		// Get memory properties
		vkGetPhysicalDeviceMemoryProperties(pRenderer->pVkGPUs[i], &(pRenderer->mVkGpuMemoryProperties[i]));

		// Get features
		pRenderer->mVkGpuFeatures[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR;

#if VK_EXT_fragment_shader_interlock
		VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fragmentShaderInterlockFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT };
		pRenderer->mVkGpuFeatures[i].pNext = &fragmentShaderInterlockFeatures;
#endif
		vkGetPhysicalDeviceFeatures2KHR(pRenderer->pVkGPUs[i], &pRenderer->mVkGpuFeatures[i]);

		// Get device properties
		VkPhysicalDeviceSubgroupProperties subgroupProperties;
		subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
		subgroupProperties.pNext = NULL;

#ifdef ENABLE_RAYTRACING
		pRenderer->mVkRaytracingProperties[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;
		pRenderer->mVkRaytracingProperties[i].pNext = &subgroupProperties;

		pRenderer->mVkGpuProperties[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
		pRenderer->mVkGpuProperties[i].pNext = &pRenderer->mVkRaytracingProperties[i];
#else
		pRenderer->mVkGpuProperties[i].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
		pRenderer->mVkGpuProperties[i].pNext = &subgroupProperties;
#endif
		vkGetPhysicalDeviceProperties2(pRenderer->pVkGPUs[i], &(pRenderer->mVkGpuProperties[i]));

#ifdef ENABLE_RAYTRACING
		pRenderer->mVkRaytracingProperties[i].pNext = nullptr;
#endif

		// Get queue family properties
		vkGetPhysicalDeviceQueueFamilyProperties(pRenderer->pVkGPUs[i], &(pRenderer->mVkQueueFamilyPropertyCount[i]), NULL);
		pRenderer->mVkQueueFamilyProperties[i] =
			(VkQueueFamilyProperties*)conf_calloc(pRenderer->mVkQueueFamilyPropertyCount[i], sizeof(VkQueueFamilyProperties));
		vkGetPhysicalDeviceQueueFamilyProperties(
			pRenderer->pVkGPUs[i], &(pRenderer->mVkQueueFamilyPropertyCount[i]), pRenderer->mVkQueueFamilyProperties[i]);

		pRenderer->mGpuSettings[i].mUniformBufferAlignment =
			(uint32_t)pRenderer->mVkGpuProperties[i].properties.limits.minUniformBufferOffsetAlignment;
		pRenderer->mGpuSettings[i].mUploadBufferTextureAlignment =
			16;    // TODO: (uint32_t)pRenderer->mVkGpuProperties[i].properties.limits.optimalBufferCopyOffsetAlignment;
		pRenderer->mGpuSettings[i].mUploadBufferTextureRowAlignment =
			1;    // TODO: (uint32_t)pRenderer->mVkGpuProperties[i].properties.limits.optimalBufferCopyRowPitchAlignment;
		pRenderer->mGpuSettings[i].mMaxVertexInputBindings = pRenderer->mVkGpuProperties[i].properties.limits.maxVertexInputBindings;
		pRenderer->mGpuSettings[i].mMultiDrawIndirect = pRenderer->mVkGpuProperties[i].properties.limits.maxDrawIndirectCount > 1;
		pRenderer->mGpuSettings[i].mWaveLaneCount = subgroupProperties.subgroupSize;
#if VK_EXT_fragment_shader_interlock
		pRenderer->mGpuSettings[i].mROVsSupported = (bool)fragmentShaderInterlockFeatures.fragmentShaderPixelInterlock;
#endif

		//save vendor and model Id as string
		sprintf(pRenderer->mGpuSettings[i].mGpuVendorPreset.mModelId, "%#x", pRenderer->mVkGpuProperties[i].properties.deviceID);
		sprintf(pRenderer->mGpuSettings[i].mGpuVendorPreset.mVendorId, "%#x", pRenderer->mVkGpuProperties[i].properties.vendorID);
		strncpy(
			pRenderer->mGpuSettings[i].mGpuVendorPreset.mGpuName, pRenderer->mVkGpuProperties[i].properties.deviceName,
			MAX_GPU_VENDOR_STRING_LENGTH);

		//TODO: Fix once vulkan adds support for revision ID
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mRevisionId, "0x00", MAX_GPU_VENDOR_STRING_LENGTH);
		pRenderer->mGpuSettings[i].mGpuVendorPreset.mPresetLevel = getGPUPresetLevel(
			pRenderer->mGpuSettings[i].mGpuVendorPreset.mVendorId, pRenderer->mGpuSettings[i].mGpuVendorPreset.mModelId,
			pRenderer->mGpuSettings[i].mGpuVendorPreset.mRevisionId);

		// Check that gpu supports at least graphics
		if (gpuIndex == UINT32_MAX || isDeviceBetter(i, gpuIndex))
		{
			uint32_t                 count = pRenderer->mVkQueueFamilyPropertyCount[i];
			VkQueueFamilyProperties* properties = pRenderer->mVkQueueFamilyProperties[i];

			//select if graphics queue is available
			for (uint32_t j = 0; j < count; j++)
			{
				//get graphics queue family
				if (properties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					gpuIndex = i;
					break;
				}
			}
		}
	}

#if defined(AUTOMATED_TESTING) && defined(ACTIVE_TESTING_GPU)
	// Overwrite gpuIndex for automatic testing
	GPUVendorPreset activeTestingPreset;
	bool            activeTestingGpu = getActiveGpuConfig(activeTestingPreset);
	if (activeTestingGpu)
	{
		for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; i++)
		{
			VkPhysicalDeviceProperties& props = pRenderer->mVkGpuProperties[i].properties;

			char deviceId[MAX_GPU_VENDOR_STRING_LENGTH];
			sprintf(deviceId, "%#x", props.deviceID);

			char vendorId[MAX_GPU_VENDOR_STRING_LENGTH];
			sprintf(vendorId, "%#x", props.vendorID);

			if (strcmp(vendorId, activeTestingPreset.mVendorId) == 0 && strcmp(deviceId, activeTestingPreset.mModelId)==0)
			{
				gpuIndex = i;
				break;
			}
		}
	}
#endif

	// If we don't own the instance or device, then we need to set the gpuIndex to the correct physical device
#if defined(VK_USE_DISPATCH_TABLES)
	gpuIndex = UINT32_MAX;
	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; i++)
	{
		if (pRenderer->pVkGPUs[i] == pRenderer->pVkActiveGPU)
		{
			gpuIndex = i;
		}
	}
#endif

	ASSERT(gpuIndex != UINT32_MAX);
	pRenderer->mActiveGPUIndex = gpuIndex;
	pRenderer->pVkActiveGPU = pRenderer->pVkGPUs[gpuIndex];
	pRenderer->pVkActiveGPUProperties = &pRenderer->mVkGpuProperties[gpuIndex];
	pRenderer->pVkActiveGpuMemoryProperties = &pRenderer->mVkGpuMemoryProperties[gpuIndex];
	pRenderer->pVkActiveGpuFeatures = &pRenderer->mVkGpuFeatures[gpuIndex];
	pRenderer->pActiveGpuSettings = &pRenderer->mGpuSettings[gpuIndex];
#ifdef ENABLE_RAYTRACING
	pRenderer->pVkActiveCPURaytracingProperties = &pRenderer->mVkRaytracingProperties[gpuIndex];
#endif
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkActiveGPU);

	LOGF(LogLevel::eINFO, "Vendor id of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mVendorId);
	LOGF(LogLevel::eINFO, "Model id of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mModelId);
	LOGF(LogLevel::eINFO, "Name of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mGpuName);

	uint32_t layerCount = 0;
	uint32_t extCount = 0;
	vkEnumerateDeviceLayerProperties(pRenderer->pVkActiveGPU, &layerCount, NULL);
	vkEnumerateDeviceExtensionProperties(pRenderer->pVkActiveGPU, NULL, &extCount, NULL);

	VkLayerProperties* layers = (VkLayerProperties*)alloca(sizeof(VkLayerProperties) * layerCount);
	vkEnumerateDeviceLayerProperties(pRenderer->pVkActiveGPU, &layerCount, layers);

	VkExtensionProperties* exts = (VkExtensionProperties*)alloca(sizeof(VkExtensionProperties) * extCount);
	vkEnumerateDeviceExtensionProperties(pRenderer->pVkActiveGPU, NULL, &extCount, exts);

	for (uint32_t i = 0; i < layerCount; ++i)
	{
		internal_log(LOG_TYPE_INFO, layers[i].layerName, "vkdevice-layer");
		if (strcmp(layers[i].layerName, "VK_LAYER_RENDERDOC_Capture") == 0)
			gRenderDocLayerEnabled = true;
	}

	for (uint32_t i = 0; i < extCount; ++i)
	{
		internal_log(LOG_TYPE_INFO, exts[i].extensionName, "vkdevice-ext");
	}

	uint32_t extension_count = 0;
	bool     dedicatedAllocationExtension = false;
	bool     memoryReq2Extension = false;
	bool     externalMemoryExtension = false;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	bool externalMemoryWin32Extension = false;
#endif
	// Standalone extensions
	{
		const char*                  layer_name = NULL;
		uint32_t                     initialCount = sizeof(gVkWantedDeviceExtensions) / sizeof(gVkWantedDeviceExtensions[0]);
		const uint32_t               userRequestedCount = (uint32_t)pRenderer->mSettings.mDeviceExtensionCount;
		eastl::vector<const char*> wantedDeviceExtensions(initialCount + userRequestedCount);
		for (uint32_t i = 0; i < initialCount; ++i)
		{
			wantedDeviceExtensions[i] = gVkWantedDeviceExtensions[i];
		}
		for (uint32_t i = 0; i < userRequestedCount; ++i)
		{
			wantedDeviceExtensions[initialCount + i] = pRenderer->mSettings.ppDeviceExtensions[i];
		}
		const uint32_t wanted_extension_count = (uint32_t)wantedDeviceExtensions.size();
		uint32_t       count = 0;
		vkEnumerateDeviceExtensionProperties(pRenderer->pVkActiveGPU, layer_name, &count, NULL);
		if (count > 0)
		{
			VkExtensionProperties* properties = (VkExtensionProperties*)conf_calloc(count, sizeof(*properties));
			ASSERT(properties != NULL);
			vkEnumerateDeviceExtensionProperties(pRenderer->pVkActiveGPU, layer_name, &count, properties);
			for (uint32_t j = 0; j < count; ++j)
			{
				for (uint32_t k = 0; k < wanted_extension_count; ++k)
				{
					if (strcmp(wantedDeviceExtensions[k], properties[j].extensionName) == 0)
					{
						pRenderer->gVkDeviceExtensions[extension_count++] = wantedDeviceExtensions[k];

#ifndef USE_DEBUG_UTILS_EXTENSION
						if (strcmp(wantedDeviceExtensions[k], VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0)
							gDebugMarkerSupport = true;
#endif
						if (strcmp(wantedDeviceExtensions[k], VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
							dedicatedAllocationExtension = true;
						if (strcmp(wantedDeviceExtensions[k], VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
							memoryReq2Extension = true;
						if (strcmp(wantedDeviceExtensions[k], VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME) == 0)
							externalMemoryExtension = true;
#if defined(VK_USE_PLATFORM_WIN32_KHR)
						if (strcmp(wantedDeviceExtensions[k], VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME) == 0)
							externalMemoryWin32Extension = true;
#endif
#ifdef VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME
						if (strcmp(wantedDeviceExtensions[k], VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME) == 0)
							gDrawIndirectCountExtension = true;
#endif
						if (strcmp(wantedDeviceExtensions[k], VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME) == 0)
							gAMDDrawIndirectCountExtension = true;
						if (strcmp(wantedDeviceExtensions[k], VK_AMD_GCN_SHADER_EXTENSION_NAME) == 0)
							gAMDGCNShaderExtension = true;
						if (strcmp(wantedDeviceExtensions[k], VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0)
							gDescriptorIndexingExtension = true;
#ifdef VK_NV_RAY_TRACING_SPEC_VERSION
						if (strcmp(wantedDeviceExtensions[k], VK_NV_RAY_TRACING_EXTENSION_NAME) == 0)
						{
							gNVRayTracingExtension = true;
							gDescriptorTypeRangeSize = CONF_DESCRIPTOR_TYPE_RANGE_SIZE;
						}
#endif
						break;
					}
				}
			}
			SAFE_FREE((void*)properties);
		}
	}

#if !defined(VK_USE_DISPATCH_TABLES)
	// Add more extensions here
#if VK_EXT_fragment_shader_interlock
	VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT fragmentShaderInterlockFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT };
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT, &fragmentShaderInterlockFeatures };
#else
	VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptorIndexingFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT };
#endif // VK_EXT_fragment_shader_interlock

	VkPhysicalDeviceFeatures2KHR gpuFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR };
	gpuFeatures2.pNext = &descriptorIndexingFeatures;

	vkGetPhysicalDeviceFeatures2KHR(pRenderer->pVkActiveGPU, &gpuFeatures2);

	// need a queue_priorite for each queue in the queue family we create
	uint32_t queueFamiliesCount = pRenderer->mVkQueueFamilyPropertyCount[pRenderer->mActiveGPUIndex];
	VkQueueFamilyProperties* queueFamiliesProperties = pRenderer->mVkQueueFamilyProperties[pRenderer->mActiveGPUIndex];
	eastl::vector<eastl::vector<float> > queue_priorities(queueFamiliesCount);
		uint32_t queue_create_infos_count = 0;
	DECLARE_ZERO(VkDeviceQueueCreateInfo, queue_create_infos[4]);

	//create all queue families with maximum amount of queues
	for (uint32_t i = 0; i < queueFamiliesCount; i++)
	{
		uint32_t queueCount = queueFamiliesProperties[i].queueCount;
		if (queueCount > 0)
		{
			queue_create_infos[queue_create_infos_count].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queue_create_infos[queue_create_infos_count].pNext = NULL;
			queue_create_infos[queue_create_infos_count].flags = 0;
			queue_create_infos[queue_create_infos_count].queueFamilyIndex = i;
			queue_create_infos[queue_create_infos_count].queueCount = queueCount;
			queue_priorities[i].resize(queueCount);
			memset(queue_priorities[i].data(), 1, queue_priorities[i].size() * sizeof(float));
			queue_create_infos[queue_create_infos_count].pQueuePriorities = queue_priorities[i].data();
			queue_create_infos_count++;
		}
	}

	DECLARE_ZERO(VkDeviceCreateInfo, create_info);
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create_info.pNext = &gpuFeatures2;
	create_info.flags = 0;
	create_info.queueCreateInfoCount = queue_create_infos_count;
	create_info.pQueueCreateInfos = queue_create_infos;
	create_info.enabledLayerCount = 0;
	create_info.ppEnabledLayerNames = NULL;
	create_info.enabledExtensionCount = extension_count;
	create_info.ppEnabledExtensionNames = pRenderer->gVkDeviceExtensions;
	create_info.pEnabledFeatures = NULL;
	/************************************************************************/
	// Add Device Group Extension if requested and available
	/************************************************************************/
#if VK_KHR_device_group_creation
	if (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED)
	{
		create_info.pNext = &deviceGroupInfo;
	}
#endif
	vk_res = vkCreateDevice(pRenderer->pVkActiveGPU, &create_info, NULL, &(pRenderer->pVkDevice));
	ASSERT(VK_SUCCESS == vk_res);

	// Load Vulkan device functions to bypass loader
	volkLoadDevice(pRenderer->pVkDevice);

	queue_priorities.clear();
#endif

	gDedicatedAllocationExtension = dedicatedAllocationExtension && memoryReq2Extension;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
	gExternalMemoryExtension = externalMemoryExtension && externalMemoryWin32Extension;
#endif

	if (gDedicatedAllocationExtension)
	{
		LOGF(LogLevel::eINFO, "Successfully loaded Dedicated Allocation extension");
	}

	if (gExternalMemoryExtension)
	{
		LOGF(LogLevel::eINFO, "Successfully loaded External Memory extension");
	}

#ifdef VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME
	if (gDrawIndirectCountExtension)
	{
		pfnVkCmdDrawIndirectCountKHR = vkCmdDrawIndirectCountKHR;
		pfnVkCmdDrawIndexedIndirectCountKHR = vkCmdDrawIndexedIndirectCountKHR;
		LOGF(LogLevel::eINFO, "Successfully loaded Draw Indirect extension");
	}
	else if (gAMDDrawIndirectCountExtension)
#endif
	{
		pfnVkCmdDrawIndirectCountKHR = vkCmdDrawIndirectCountAMD;
		pfnVkCmdDrawIndexedIndirectCountKHR = vkCmdDrawIndexedIndirectCountAMD;
		LOGF(LogLevel::eINFO, "Successfully loaded AMD Draw Indirect extension");
	}

	if (gAMDGCNShaderExtension)
	{
		LOGF(LogLevel::eINFO, "Successfully loaded AMD GCN Shader extension");
	}

	if (gDescriptorIndexingExtension)
	{
		LOGF(LogLevel::eINFO, "Successfully loaded Descriptor Indexing extension");
	}

	if (gNVRayTracingExtension)
	{
		LOGF(LogLevel::eINFO, "Successfully loaded Nvidia Ray Tracing extension");
	}

#ifdef USE_DEBUG_UTILS_EXTENSION
	gDebugMarkerSupport =
		vkCmdBeginDebugUtilsLabelEXT && vkCmdEndDebugUtilsLabelEXT && vkCmdInsertDebugUtilsLabelEXT && vkSetDebugUtilsObjectNameEXT;
#endif

	utils_caps_builder(pRenderer);
}

static void RemoveDevice(Renderer* pRenderer)
{
	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		SAFE_FREE(pRenderer->mVkQueueFamilyProperties[i]);
	}

	vkDestroyDevice(pRenderer->pVkDevice, NULL);
}

VkDeviceMemory get_vk_device_memory(Renderer* pRenderer, Buffer* pBuffer)
{
	VmaAllocationInfo allocInfo = {};
	vmaGetAllocationInfo(pRenderer->pVmaAllocator, pBuffer->pVkAllocation, &allocInfo);
	return allocInfo.deviceMemory;
}

uint64_t get_vk_device_memory_offset(Renderer* pRenderer, Buffer* pBuffer)
{
	VmaAllocationInfo allocInfo = {};
	vmaGetAllocationInfo(pRenderer->pVmaAllocator, pBuffer->pVkAllocation, &allocInfo);
	return (uint64_t)allocInfo.offset;
}
/************************************************************************/
// Renderer Init Remove
/************************************************************************/
void initRenderer(const char* app_name, const RendererDesc* settings, Renderer** ppRenderer)
{
	Renderer* pRenderer = (Renderer*)conf_calloc(1, sizeof(*pRenderer));
	ASSERT(pRenderer);

	pRenderer->pName = (char*)conf_calloc(strlen(app_name) + 1, sizeof(char));
	memcpy(pRenderer->pName, app_name, strlen(app_name));

	// Copy settings
	memcpy(&(pRenderer->mSettings), settings, sizeof(*settings));
	pRenderer->mSettings.mApi = RENDERER_API_VULKAN;

	// Initialize the Vulkan internal bits
	{
#if defined(VK_USE_DISPATCH_TABLES)
		VkResult vkRes = volkInitializeWithDispatchTables(pRenderer);
		if (vkRes != VK_SUCCESS)
		{
			LOGF(LogLevel::eERROR, "Failed to initialize Vulkan");
			return;
		}
#else
#if defined(_DEBUG)
		// this turns on all validation layers
		pRenderer->ppInstanceLayers = (const char**)conf_calloc(1, sizeof(char*));
		pRenderer->ppInstanceLayers[pRenderer->mInstanceLayerCount++] = "VK_LAYER_LUNARG_standard_validation";
#endif

		// this turns on render doc layer for gpu capture
#ifdef USE_RENDER_DOC
		pRenderer->ppInstanceLayers = (const char**)conf_realloc(pRenderer->ppInstanceLayers, (pRenderer->mInstanceLayerCount + 1) * sizeof(char*));
		pRenderer->ppInstanceLayers[pRenderer->mInstanceLayerCount++] = "VK_LAYER_RENDERDOC_Capture";
#endif

		if (settings->mInstanceLayerCount)
			pRenderer->ppInstanceLayers = (const char**)conf_realloc(pRenderer->ppInstanceLayers, (pRenderer->mInstanceLayerCount + settings->mInstanceLayerCount) * sizeof(char*));
		// Add user specified instance layers for instance creation
		for (uint32_t i = 0; i < (uint32_t)settings->mInstanceLayerCount; ++i)
			pRenderer->ppInstanceLayers[pRenderer->mInstanceLayerCount++] = settings->ppInstanceLayers[i];

		VkResult vkRes = volkInitialize();
		if (vkRes != VK_SUCCESS)
		{
			LOGF(LogLevel::eERROR, "Failed to initialize Vulkan");
			return;
		}

		CreateInstance(app_name, pRenderer);
#endif
		AddDevice(pRenderer);
		//anything below LOW preset is not supported and we will exit
		if (pRenderer->pActiveGpuSettings->mGpuVendorPreset.mPresetLevel < GPU_PRESET_LOW)
		{
			//have the condition in the assert as well so its cleared when the assert message box appears

			ASSERT(pRenderer->pActiveGpuSettings->mGpuVendorPreset.mPresetLevel >= GPU_PRESET_LOW);

			SAFE_FREE(pRenderer->pName);

			//remove device and any memory we allocated in just above as this is the first function called
			//when initializing the forge
#if !defined(VK_USE_DISPATCH_TABLES)
			RemoveDevice(pRenderer);
			RemoveInstance(pRenderer);
			SAFE_FREE(pRenderer);
			LOGF(LogLevel::eERROR, "Selected GPU has an Office Preset in gpu.cfg.");
			LOGF(LogLevel::eERROR, "Office preset is not supported by The Forge.");
#endif

			//return NULL pRenderer so that client can gracefully handle exit
			//This is better than exiting from here in case client has allocated memory or has fallbacks
			ppRenderer = NULL;
			return;
		}
		/************************************************************************/
		/************************************************************************/
		VmaAllocatorCreateInfo createInfo = { 0 };
		createInfo.device = pRenderer->pVkDevice;
		createInfo.physicalDevice = pRenderer->pVkActiveGPU;

		// Render Doc Capture currently does not support use of this extension
		if (gDedicatedAllocationExtension && !gRenderDocLayerEnabled)
		{
			createInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
		}

		VmaVulkanFunctions vulkanFunctions = {};
		vulkanFunctions.vkAllocateMemory = vkAllocateMemory;
		vulkanFunctions.vkBindBufferMemory = vkBindBufferMemory;
		vulkanFunctions.vkBindImageMemory = vkBindImageMemory;
		vulkanFunctions.vkCreateBuffer = vkCreateBuffer;
		vulkanFunctions.vkCreateImage = vkCreateImage;
		vulkanFunctions.vkDestroyBuffer = vkDestroyBuffer;
		vulkanFunctions.vkDestroyImage = vkDestroyImage;
		vulkanFunctions.vkFreeMemory = vkFreeMemory;
		vulkanFunctions.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements;
		vulkanFunctions.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
		vulkanFunctions.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements;
		vulkanFunctions.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR;
		vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
		vulkanFunctions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
		vulkanFunctions.vkMapMemory = vkMapMemory;
		vulkanFunctions.vkUnmapMemory = vkUnmapMemory;
		vulkanFunctions.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges;
		vulkanFunctions.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges;
		vulkanFunctions.vkCmdCopyBuffer = vkCmdCopyBuffer;

		createInfo.pVulkanFunctions = &vulkanFunctions;

		vmaCreateAllocator(&createInfo, &pRenderer->pVmaAllocator);
	}

	create_default_resources(pRenderer);

	VkDescriptorPoolSize descriptorPoolSizes[CONF_DESCRIPTOR_TYPE_RANGE_SIZE] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1024 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1024 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1024 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1 },
	};
#ifdef VK_NV_RAY_TRACING_SPEC_VERSION
	if (gNVRayTracingExtension)
	{
		descriptorPoolSizes[CONF_DESCRIPTOR_TYPE_RANGE_SIZE - 1] = { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1024 };
	}
#endif
	add_descriptor_pool(pRenderer, 8192, (VkDescriptorPoolCreateFlags)0, descriptorPoolSizes, gDescriptorTypeRangeSize, &pRenderer->pDescriptorPool);
	pRenderPassMutex = (Mutex*)conf_calloc(1, sizeof(Mutex));
	pRenderPassMutex->Init();
	gRenderPassMap = conf_placement_new<eastl::hash_map<ThreadID, RenderPassMap> >(conf_malloc(sizeof(*gRenderPassMap)));
	gFrameBufferMap = conf_placement_new<eastl::hash_map<ThreadID, FrameBufferMap> >(conf_malloc(sizeof(*gFrameBufferMap)));

	// Set shader macro based on runtime information
	static char descriptorIndexingMacroBuffer[2] = {};
	static char textureArrayDynamicIndexingMacroBuffer[2] = {};
	sprintf(descriptorIndexingMacroBuffer, "%u", (uint32_t)(gDescriptorIndexingExtension));
	sprintf(textureArrayDynamicIndexingMacroBuffer, "%u", (uint32_t)(pRenderer->pVkActiveGpuFeatures->features.shaderSampledImageArrayDynamicIndexing));
	static ShaderMacro rendererShaderDefines[] =
	{
		{ "VK_EXT_DESCRIPTOR_INDEXING_ENABLED", descriptorIndexingMacroBuffer },
		{ "VK_FEATURE_TEXTURE_ARRAY_DYNAMIC_INDEXING_ENABLED", textureArrayDynamicIndexingMacroBuffer },
		// Descriptor set indices
		{ "UPDATE_FREQ_NONE",      "set = 0" },
		{ "UPDATE_FREQ_PER_FRAME", "set = 1" },
		{ "UPDATE_FREQ_PER_BATCH", "set = 2" },
		{ "UPDATE_FREQ_PER_DRAW",  "set = 3" },
	};
	pRenderer->mBuiltinShaderDefinesCount = sizeof(rendererShaderDefines) / sizeof(rendererShaderDefines[0]);
	pRenderer->pBuiltinShaderDefines = rendererShaderDefines;

	// Renderer is good! Assign it to result!
	*(ppRenderer) = pRenderer;
}

void removeRenderer(Renderer* pRenderer)
{
	ASSERT(pRenderer);

	SAFE_FREE(pRenderer->pName);

	remove_descriptor_pool(pRenderer, pRenderer->pDescriptorPool);
	pRenderPassMutex->Destroy();
	conf_free(pRenderPassMutex);

	destroy_default_resources(pRenderer);

	// Remove the renderpasses
	for (eastl::hash_map<ThreadID, RenderPassMap>::value_type& t : *gRenderPassMap)
		for (RenderPassMapNode& it : t.second)
			remove_render_pass(pRenderer, it.second);

	for (eastl::hash_map<ThreadID, FrameBufferMap>::value_type& t : *gFrameBufferMap)
		for (FrameBufferMapNode& it : t.second)
			remove_framebuffer(pRenderer, it.second);

	gRenderPassMap->clear();
	gFrameBufferMap->clear();
	conf_delete(gRenderPassMap);
	gRenderPassMap = NULL;
	conf_delete(gFrameBufferMap);
	gFrameBufferMap = NULL;

	// Destroy the Vulkan bits
	vmaDestroyAllocator(pRenderer->pVmaAllocator);

#if defined(VK_USE_DISPATCH_TABLES)
	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		SAFE_FREE(pRenderer->mVkQueueFamilyProperties[i]);
	}
#else
	RemoveDevice(pRenderer);
	RemoveInstance(pRenderer);
#endif

	SAFE_FREE(pRenderer->ppInstanceLayers);
	// Free all the renderer components!
	SAFE_FREE(pRenderer);
}
/************************************************************************/
// Resource Creation Functions
/************************************************************************/
void addFence(Renderer* pRenderer, Fence** ppFence)
{
	ASSERT(pRenderer);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	Fence* pFence = (Fence*)conf_calloc(1, sizeof(*pFence));
	ASSERT(pFence);

	DECLARE_ZERO(VkFenceCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = 0;
	VkResult vk_res = vkCreateFence(pRenderer->pVkDevice, &add_info, NULL, &(pFence->pVkFence));
	ASSERT(VK_SUCCESS == vk_res);

	pFence->mSubmitted = false;

	*ppFence = pFence;
}

void removeFence(Renderer* pRenderer, Fence* pFence)
{
	ASSERT(pRenderer);
	ASSERT(pFence);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pFence->pVkFence);

	vkDestroyFence(pRenderer->pVkDevice, pFence->pVkFence, NULL);

	SAFE_FREE(pFence);
}

void addSemaphore(Renderer* pRenderer, Semaphore** ppSemaphore)
{
	ASSERT(pRenderer);

	Semaphore* pSemaphore = (Semaphore*)conf_calloc(1, sizeof(*pSemaphore));
	ASSERT(pSemaphore);

	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	DECLARE_ZERO(VkSemaphoreCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = 0;
	VkResult vk_res = vkCreateSemaphore(pRenderer->pVkDevice, &add_info, NULL, &(pSemaphore->pVkSemaphore));
	ASSERT(VK_SUCCESS == vk_res);
	// Set signal inital state.
	pSemaphore->mSignaled = false;

	*ppSemaphore = pSemaphore;
}

void removeSemaphore(Renderer* pRenderer, Semaphore* pSemaphore)
{
	ASSERT(pRenderer);
	ASSERT(pSemaphore);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pSemaphore->pVkSemaphore);

	vkDestroySemaphore(pRenderer->pVkDevice, pSemaphore->pVkSemaphore, NULL);

	SAFE_FREE(pSemaphore);
}

void addQueue(Renderer* pRenderer, QueueDesc* pDesc, Queue** ppQueue)
{
	ASSERT(pDesc != NULL);

	uint32_t       queueFamilyIndex = -1;
	VkQueueFlags   requiredFlags = util_to_vk_queue_flags(pDesc->mType);
	uint32_t       queueIndex = -1;
	bool           found = false;
	const uint32_t nodeIndex = pDesc->mNodeIndex;

	// Try to find a dedicated queue of this type
	for (uint32_t index = 0; index < pRenderer->mVkQueueFamilyPropertyCount[nodeIndex]; ++index)
	{
		VkQueueFlags queueFlags = pRenderer->mVkQueueFamilyProperties[nodeIndex][index].queueFlags;
		if ((queueFlags & requiredFlags) && ((queueFlags & ~requiredFlags) == 0) &&
			pRenderer->mVkUsedQueueCount[nodeIndex][queueFlags] < pRenderer->mVkQueueFamilyProperties[nodeIndex][index].queueCount)
		{
			found = true;
			queueFamilyIndex = index;
			queueIndex = pRenderer->mVkUsedQueueCount[nodeIndex][queueFlags];
			break;
		}
	}

	// If hardware doesn't provide a dedicated queue try to find a non-dedicated one
	if (!found)
	{
		for (uint32_t index = 0; index < pRenderer->mVkQueueFamilyPropertyCount[nodeIndex]; ++index)
		{
			VkQueueFlags queueFlags = pRenderer->mVkQueueFamilyProperties[nodeIndex][index].queueFlags;
			if ((queueFlags & requiredFlags) &&
				pRenderer->mVkUsedQueueCount[nodeIndex][queueFlags] < pRenderer->mVkQueueFamilyProperties[nodeIndex][index].queueCount)
			{
				found = true;
				queueFamilyIndex = index;
				queueIndex = pRenderer->mVkUsedQueueCount[nodeIndex][queueFlags];
				break;
			}
		}
	}

	if (!found)
	{
		found = true;
		queueFamilyIndex = 0;
		queueIndex = 0;

		LOGF(LogLevel::eWARNING, "Could not find queue of type %u. Using default queue", (uint32_t)pDesc->mType);
	}

	if (found)
	{
		VkQueueFamilyProperties& queueProps = pRenderer->mVkQueueFamilyProperties[nodeIndex][queueFamilyIndex];
		Queue*       pQueueToCreate = (Queue*)conf_calloc(1, sizeof(*pQueueToCreate));
		pQueueToCreate->mVkQueueFamilyIndex = queueFamilyIndex;
		pQueueToCreate->pRenderer = pRenderer;
		pQueueToCreate->mQueueDesc = *pDesc;
		pQueueToCreate->mVkQueueIndex = queueIndex;
		pQueueToCreate->mUploadGranularity = { queueProps.minImageTransferGranularity.width, queueProps.minImageTransferGranularity.height,
											   queueProps.minImageTransferGranularity.depth };
		//get queue handle
		vkGetDeviceQueue(
			pRenderer->pVkDevice, pQueueToCreate->mVkQueueFamilyIndex, pQueueToCreate->mVkQueueIndex, &(pQueueToCreate->pVkQueue));
		ASSERT(VK_NULL_HANDLE != pQueueToCreate->pVkQueue);
		*ppQueue = pQueueToCreate;

		++pRenderer->mVkUsedQueueCount[nodeIndex][queueProps.queueFlags];
	}
	else
	{
		LOGF(LogLevel::eERROR, "Cannot create queue of type (%u)", pDesc->mType);
	}
}

void removeQueue(Queue* pQueue)
{
	ASSERT(pQueue != NULL);
	const uint32_t nodeIndex = pQueue->mQueueDesc.mNodeIndex;
	VkQueueFlags   queueFlags = pQueue->pRenderer->mVkQueueFamilyProperties[nodeIndex][pQueue->mVkQueueFamilyIndex].queueFlags;
	--pQueue->pRenderer->mVkUsedQueueCount[nodeIndex][queueFlags];
	SAFE_FREE(pQueue);
}

void addCmdPool(Renderer* pRenderer, Queue* pQueue, bool transient, CmdPool** ppCmdPool)
{
	ASSERT(pRenderer);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	CmdPool* pCmdPool = (CmdPool*)conf_calloc(1, sizeof(*pCmdPool));
	ASSERT(pCmdPool);

	pCmdPool->mCmdPoolDesc = { pQueue->mQueueDesc.mType };
	pCmdPool->pQueue = pQueue;

	DECLARE_ZERO(VkCommandPoolCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	add_info.queueFamilyIndex = pQueue->mVkQueueFamilyIndex;
	if (transient)
	{
		add_info.flags |= VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	}
	VkResult vk_res = vkCreateCommandPool(pRenderer->pVkDevice, &add_info, NULL, &(pCmdPool->pVkCmdPool));
	ASSERT(VK_SUCCESS == vk_res);

	*ppCmdPool = pCmdPool;
}

void removeCmdPool(Renderer* pRenderer, CmdPool* pCmdPool)
{
	ASSERT(pRenderer);
	ASSERT(pCmdPool);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pCmdPool->pVkCmdPool);

	vkDestroyCommandPool(pRenderer->pVkDevice, pCmdPool->pVkCmdPool, NULL);

	SAFE_FREE(pCmdPool);
}

void addCmd(CmdPool* pCmdPool, bool secondary, Cmd** ppCmd)
{
	ASSERT(pCmdPool);
	ASSERT(VK_NULL_HANDLE != pCmdPool->pQueue->pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pCmdPool->pVkCmdPool);

	Cmd* pCmd = (Cmd*)conf_calloc(1, sizeof(*pCmd));
	ASSERT(pCmd);

	pCmd->pRenderer = pCmdPool->pQueue->pRenderer;
	pCmd->pCmdPool = pCmdPool;
	pCmd->mNodeIndex = pCmdPool->pQueue->mQueueDesc.mNodeIndex;

	DECLARE_ZERO(VkCommandBufferAllocateInfo, alloc_info);
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.pNext = NULL;
	alloc_info.commandPool = pCmdPool->pVkCmdPool;
	alloc_info.level = secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;
	VkResult vk_res = vkAllocateCommandBuffers(pCmd->pRenderer->pVkDevice, &alloc_info, &(pCmd->pVkCmdBuf));
	ASSERT(VK_SUCCESS == vk_res);

	*ppCmd = pCmd;
}

void removeCmd(CmdPool* pCmdPool, Cmd* pCmd)
{
	ASSERT(pCmdPool);
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pCmdPool->pVkCmdPool);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	vkFreeCommandBuffers(pCmd->pRenderer->pVkDevice, pCmdPool->pVkCmdPool, 1, &(pCmd->pVkCmdBuf));

	SAFE_FREE(pCmd);
}

void addCmd_n(CmdPool* pCmdPool, bool secondary, uint32_t cmdCount, Cmd*** pppCmd)
{
	ASSERT(pppCmd);

	Cmd** ppCmd = (Cmd**)conf_calloc(cmdCount, sizeof(*ppCmd));
	ASSERT(ppCmd);

	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		::addCmd(pCmdPool, secondary, &(ppCmd[i]));
	}

	*pppCmd = ppCmd;
}

void removeCmd_n(CmdPool* pCmdPool, uint32_t cmdCount, Cmd** ppCmd)
{
	ASSERT(ppCmd);

	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		::removeCmd(pCmdPool, ppCmd[i]);
	}

	SAFE_FREE(ppCmd);
}

void toggleVSync(Renderer* pRenderer, SwapChain** ppSwapChain)
{
	//toggle vsync on or off
	//for Vulkan we need to remove the SwapChain and recreate it with correct vsync option
	(*ppSwapChain)->mDesc.mEnableVsync = !(*ppSwapChain)->mDesc.mEnableVsync;
	SwapChainDesc desc = (*ppSwapChain)->mDesc;
	removeSwapChain(pRenderer, *ppSwapChain);
	addSwapChain(pRenderer, &desc, ppSwapChain);
}

void addSwapChain(Renderer* pRenderer, const SwapChainDesc* pDesc, SwapChain** ppSwapChain)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(ppSwapChain);

	SwapChain* pSwapChain = (SwapChain*)conf_calloc(1, sizeof(*pSwapChain));
	pSwapChain->mDesc = *pDesc;

	/************************************************************************/
	// Create surface
	/************************************************************************/
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkInstance);
	VkResult vk_res;
	// Create a WSI surface for the window:
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	DECLARE_ZERO(VkWin32SurfaceCreateInfoKHR, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.hinstance = ::GetModuleHandle(NULL);
	add_info.hwnd = (HWND)pDesc->mWindowHandle.window;
	vk_res = vkCreateWin32SurfaceKHR(pRenderer->pVkInstance, &add_info, NULL, &pSwapChain->pVkSurface);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
	DECLARE_ZERO(VkXlibSurfaceCreateInfoKHR, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.dpy = pDesc->mWindowHandle.display;      //TODO
	add_info.window = pDesc->mWindowHandle.window;    //TODO

	vk_res = vkCreateXlibSurfaceKHR(pRenderer->pVkInstance, &add_info, NULL, &pSwapChain->pVkSurface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	DECLARE_ZERO(VkXcbSurfaceCreateInfoKHR, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.connection = pDesc->mWindowHandle.connection;    //TODO
	add_info.window = pDesc->mWindowHandle.window;        //TODO

	vk_res = vkCreateXcbSurfaceKHR(pRenderer->pVkInstance, &add_info, NULL, &pSwapChain->pVkSurface);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
	// Add IOS support here
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	// Add MacOS support here
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	DECLARE_ZERO(VkAndroidSurfaceCreateInfoKHR, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.window = (ANativeWindow*)pDesc->mWindowHandle.window;
	vk_res = vkCreateAndroidSurfaceKHR(pRenderer->pVkInstance, &add_info, NULL, &pSwapChain->pVkSurface);
#elif defined(VK_USE_PLATFORM_GGP)
	extern VkResult ggpCreateSurface(VkInstance, VkSurfaceKHR* surface);
	vk_res = ggpCreateSurface(pRenderer->pVkInstance, &pSwapChain->pVkSurface);
#else
#error PLATFORM NOT SUPPORTED
#endif
	ASSERT(VK_SUCCESS == vk_res);
	/************************************************************************/
	// Create swap chain
	/************************************************************************/
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkActiveGPU);

	// Most GPUs will not go beyond VK_SAMPLE_COUNT_8_BIT
	ASSERT(0 != (pRenderer->pVkActiveGPUProperties->properties.limits.framebufferColorSampleCounts & pSwapChain->mDesc.mSampleCount));

	// Image count
	if (0 == pSwapChain->mDesc.mImageCount)
	{
		pSwapChain->mDesc.mImageCount = 2;
	}

	DECLARE_ZERO(VkSurfaceCapabilitiesKHR, caps);
	vk_res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pRenderer->pVkActiveGPU, pSwapChain->pVkSurface, &caps);
	ASSERT(VK_SUCCESS == vk_res);

	if ((caps.maxImageCount > 0) && (pSwapChain->mDesc.mImageCount > caps.maxImageCount))
	{
		pSwapChain->mDesc.mImageCount = caps.maxImageCount;
	}

	// Surface format
	// Select a surface format, depending on whether HDR is available.
	VkSurfaceFormatKHR hdrSurfaceFormat = { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT };

	DECLARE_ZERO(VkSurfaceFormatKHR, surface_format);
	surface_format.format = VK_FORMAT_UNDEFINED;
	uint32_t            surfaceFormatCount = 0;
	VkSurfaceFormatKHR* formats = NULL;

	// Get surface formats count
	vk_res = vkGetPhysicalDeviceSurfaceFormatsKHR(pRenderer->pVkActiveGPU, pSwapChain->pVkSurface, &surfaceFormatCount, NULL);
	ASSERT(VK_SUCCESS == vk_res);

	// Allocate and get surface formats
	formats = (VkSurfaceFormatKHR*)conf_calloc(surfaceFormatCount, sizeof(*formats));
	vk_res = vkGetPhysicalDeviceSurfaceFormatsKHR(pRenderer->pVkActiveGPU, pSwapChain->pVkSurface, &surfaceFormatCount, formats);
	ASSERT(VK_SUCCESS == vk_res);

	if ((1 == surfaceFormatCount) && (VK_FORMAT_UNDEFINED == formats[0].format))
	{
		surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
		surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	}
	else
	{
		VkFormat requested_format = (VkFormat)TinyImageFormat_ToVkFormat(pSwapChain->mDesc.mColorFormat);
		VkColorSpaceKHR requested_color_space = requested_format == hdrSurfaceFormat.format ? hdrSurfaceFormat.colorSpace : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		for (uint32_t i = 0; i < surfaceFormatCount; ++i)
		{
			if ((requested_format == formats[i].format) && (requested_color_space == formats[i].colorSpace))
			{
				surface_format.format = requested_format;
				surface_format.colorSpace = requested_color_space;
				break;
			}
		}

		// Default to VK_FORMAT_B8G8R8A8_UNORM if requested format isn't found
		if (VK_FORMAT_UNDEFINED == surface_format.format)
		{
			surface_format.format = VK_FORMAT_B8G8R8A8_UNORM;
			surface_format.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
		}
	}

	ASSERT(VK_FORMAT_UNDEFINED != surface_format.format);

	// Free formats
	SAFE_FREE(formats);

	// The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
	// This mode waits for the vertical blank ("v-sync")
	VkPresentModeKHR  present_mode = VK_PRESENT_MODE_FIFO_KHR;
	uint32_t          swapChainImageCount = 0;
	VkPresentModeKHR* modes = NULL;
	// Get present mode count
	vk_res = vkGetPhysicalDeviceSurfacePresentModesKHR(pRenderer->pVkActiveGPU, pSwapChain->pVkSurface, &swapChainImageCount, NULL);
	ASSERT(VK_SUCCESS == vk_res);

	// Allocate and get present modes
	modes = (VkPresentModeKHR*)conf_calloc(swapChainImageCount, sizeof(*modes));
	vk_res = vkGetPhysicalDeviceSurfacePresentModesKHR(pRenderer->pVkActiveGPU, pSwapChain->pVkSurface, &swapChainImageCount, modes);
	ASSERT(VK_SUCCESS == vk_res);

	const uint32_t preferredModeCount = 4;
	VkPresentModeKHR preferredModeList[preferredModeCount] = { VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_RELAXED_KHR, VK_PRESENT_MODE_FIFO_KHR };
	uint32_t preferredModeStartIndex = pSwapChain->mDesc.mEnableVsync ? 2 : 0;

	for (uint32_t j = preferredModeStartIndex; j < preferredModeCount; ++j)
	{
		VkPresentModeKHR mode = preferredModeList[j];
		uint32_t         i = 0;
		for (; i < swapChainImageCount; ++i)
		{
			if (modes[i] == mode)
			{
				break;
			}
		}
		if (i < swapChainImageCount)
		{
            present_mode = mode;
			break;
		}
	}

	// Free modes
	SAFE_FREE(modes);

	// Swapchain
	VkExtent2D extent = { 0 };
	extent.width = pSwapChain->mDesc.mWidth;
	extent.height = pSwapChain->mDesc.mHeight;

	VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
	uint32_t      queue_family_index_count = 0;
	uint32_t      queue_family_indices[2] = { pDesc->ppPresentQueues[0]->mVkQueueFamilyIndex, 0 };
	uint32_t      presentQueueFamilyIndex = -1;
	uint32_t      nodeIndex = 0;

	// Check if hardware provides dedicated present queue
	if (0 != pRenderer->mVkQueueFamilyPropertyCount[nodeIndex])
	{
		for (uint32_t index = 0; index < pRenderer->mVkQueueFamilyPropertyCount[nodeIndex]; ++index)
		{
			VkBool32 supports_present = VK_FALSE;
			VkResult res = vkGetPhysicalDeviceSurfaceSupportKHR(pRenderer->pVkActiveGPU, index, pSwapChain->pVkSurface, &supports_present);
			if ((VK_SUCCESS == res) && (VK_TRUE == supports_present) && pSwapChain->mDesc.ppPresentQueues[0]->mVkQueueFamilyIndex != index)
			{
				presentQueueFamilyIndex = index;
				break;
			}
		}

		// If there is no dedicated present queue, just find the first available queue which supports present
		if (presentQueueFamilyIndex == -1)
		{
			for (uint32_t index = 0; index < pRenderer->mVkQueueFamilyPropertyCount[nodeIndex]; ++index)
			{
				VkBool32 supports_present = VK_FALSE;
				VkResult res =
					vkGetPhysicalDeviceSurfaceSupportKHR(pRenderer->pVkActiveGPU, index, pSwapChain->pVkSurface, &supports_present);
				if ((VK_SUCCESS == res) && (VK_TRUE == supports_present))
				{
					presentQueueFamilyIndex = index;
					break;
				}
				else
				{
					// No present queue family available. Something goes wrong.
					ASSERT(0);
				}
			}
		}
	}

	// Find if gpu has a dedicated present queue
	if (presentQueueFamilyIndex != -1 && queue_family_indices[0] != presentQueueFamilyIndex)
	{
		queue_family_indices[0] = presentQueueFamilyIndex;
		vkGetDeviceQueue(pRenderer->pVkDevice, queue_family_indices[0], 0, &pSwapChain->pPresentQueue);
		queue_family_index_count = 1;
		pSwapChain->mPresentQueueFamilyIndex = presentQueueFamilyIndex;
	}
	else
	{
		pSwapChain->pPresentQueue = VK_NULL_HANDLE;
	}

	DECLARE_ZERO(VkSwapchainCreateInfoKHR, swapChainCreateInfo);
	swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapChainCreateInfo.pNext = NULL;
	swapChainCreateInfo.flags = 0;
	swapChainCreateInfo.surface = pSwapChain->pVkSurface;
#ifndef VK_USE_PLATFORM_ANDROID_KHR
	swapChainCreateInfo.minImageCount = pSwapChain->mDesc.mImageCount;
#else
	//TODO: thomas Fixme hack
	swapChainCreateInfo.minImageCount = caps.minImageCount;
#endif
	swapChainCreateInfo.imageFormat = surface_format.format;
	swapChainCreateInfo.imageColorSpace = surface_format.colorSpace;
	swapChainCreateInfo.imageExtent = extent;
	swapChainCreateInfo.imageArrayLayers = 1;
	swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	swapChainCreateInfo.imageSharingMode = sharing_mode;
	swapChainCreateInfo.queueFamilyIndexCount = queue_family_index_count;
	swapChainCreateInfo.pQueueFamilyIndices = queue_family_indices;
	swapChainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapChainCreateInfo.presentMode = present_mode;
	swapChainCreateInfo.clipped = VK_TRUE;
	swapChainCreateInfo.oldSwapchain = 0;
	vk_res = vkCreateSwapchainKHR(pRenderer->pVkDevice, &swapChainCreateInfo, NULL, &(pSwapChain->pSwapChain));
	ASSERT(VK_SUCCESS == vk_res);

	pSwapChain->mDesc.mColorFormat = TinyImageFormat_FromVkFormat((TinyImageFormat_VkFormat)surface_format.format);

	// Create rendertargets from swapchain
	uint32_t image_count = 0;
	vk_res = vkGetSwapchainImagesKHR(pRenderer->pVkDevice, pSwapChain->pSwapChain, &image_count, NULL);
	ASSERT(VK_SUCCESS == vk_res);

	ASSERT(image_count == pSwapChain->mDesc.mImageCount);

	pSwapChain->ppVkSwapChainImages = (VkImage*)conf_calloc(image_count, sizeof(*pSwapChain->ppVkSwapChainImages));
	ASSERT(pSwapChain->ppVkSwapChainImages);

	vk_res = vkGetSwapchainImagesKHR(pRenderer->pVkDevice, pSwapChain->pSwapChain, &image_count, pSwapChain->ppVkSwapChainImages);
	ASSERT(VK_SUCCESS == vk_res);

	RenderTargetDesc descColor = {};
	descColor.mWidth = pSwapChain->mDesc.mWidth;
	descColor.mHeight = pSwapChain->mDesc.mHeight;
	descColor.mDepth = 1;
	descColor.mArraySize = 1;
	descColor.mFormat = pSwapChain->mDesc.mColorFormat;
	descColor.mClearValue = pSwapChain->mDesc.mColorClearValue;
	descColor.mSampleCount = SAMPLE_COUNT_1;
	descColor.mSampleQuality = 0;

	pSwapChain->ppSwapchainRenderTargets =
		(RenderTarget**)conf_calloc(pSwapChain->mDesc.mImageCount, sizeof(*pSwapChain->ppSwapchainRenderTargets));

	// Populate the vk_image field and add the Vulkan texture objects
	for (uint32_t i = 0; i < pSwapChain->mDesc.mImageCount; ++i)
	{
		descColor.pNativeHandle = (void*)pSwapChain->ppVkSwapChainImages[i];
		addRenderTarget(pRenderer, &descColor, &pSwapChain->ppSwapchainRenderTargets[i]);
	}
	/************************************************************************/
	/************************************************************************/

	*ppSwapChain = pSwapChain;
}

void removeSwapChain(Renderer* pRenderer, SwapChain* pSwapChain)
{
	ASSERT(pRenderer);
	ASSERT(pSwapChain);

	for (uint32_t i = 0; i < pSwapChain->mDesc.mImageCount; ++i)
	{
		removeRenderTarget(pRenderer, pSwapChain->ppSwapchainRenderTargets[i]);
	}

	vkDestroySwapchainKHR(pRenderer->pVkDevice, pSwapChain->pSwapChain, NULL);
	vkDestroySurfaceKHR(pRenderer->pVkInstance, pSwapChain->pVkSurface, NULL);

	SAFE_FREE(pSwapChain->ppSwapchainRenderTargets);
	SAFE_FREE(pSwapChain->ppVkSwapChainImages);
	SAFE_FREE(pSwapChain);
}

void addBuffer(Renderer* pRenderer, const BufferDesc* pDesc, Buffer** pp_buffer)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(pDesc->mSize > 0);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	Buffer* pBuffer = (Buffer*)conf_calloc(1, sizeof(*pBuffer));
	ASSERT(pBuffer);

	pBuffer->mDesc = *pDesc;

	uint64_t allocationSize = pBuffer->mDesc.mSize;
	// Align the buffer size to multiples of the dynamic uniform buffer minimum size
	if (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	{
		uint64_t minAlignment = pRenderer->pActiveGpuSettings->mUniformBufferAlignment;
		allocationSize = round_up_64(allocationSize, minAlignment);
	}

	DECLARE_ZERO(VkBufferCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.size = allocationSize;
	add_info.usage = util_to_vk_buffer_usage(pBuffer->mDesc.mDescriptors, pDesc->mFormat != TinyImageFormat_UNDEFINED);
	add_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	add_info.queueFamilyIndexCount = 0;
	add_info.pQueueFamilyIndices = NULL;

	// Buffer can be used as dest in a transfer command (Uploading data to a storage buffer, Readback query data)
	if (pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_ONLY || pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_TO_CPU)
		add_info.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	const bool linkedMultiGpu = (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED && (pDesc->pSharedNodeIndices || pDesc->mNodeIndex));

	AllocatorMemoryRequirements vma_mem_reqs = { 0 };
	vma_mem_reqs.usage = (VmaMemoryUsage)pBuffer->mDesc.mMemoryUsage;
	vma_mem_reqs.flags = 0;
	if (pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_OWN_MEMORY_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	if (pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
	if (linkedMultiGpu)
		vma_mem_reqs.flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;

	BufferCreateInfo alloc_info = { &add_info };
	VkResult         vk_res = (VkResult)vk_createBuffer(pRenderer->pVmaAllocator, &alloc_info, &vma_mem_reqs, pBuffer);
	ASSERT(VK_SUCCESS == vk_res);
	/************************************************************************/
	// Buffer to be used on multiple GPUs
	/************************************************************************/
	if (linkedMultiGpu)
	{
		VmaAllocationInfo allocInfo = {};
		vmaGetAllocationInfo(pRenderer->pVmaAllocator, pBuffer->pVkAllocation, &allocInfo);
		/************************************************************************/
		// Set all the device indices to the index of the device where we will create the buffer
		/************************************************************************/
		uint32_t* pIndices = (uint32_t*)alloca(pRenderer->mLinkedNodeCount * sizeof(uint32_t));
		util_calculate_device_indices(pRenderer, pDesc->mNodeIndex, pDesc->pSharedNodeIndices, pDesc->mSharedNodeIndexCount, pIndices);
		/************************************************************************/
		// #TODO : Move this to the Vulkan memory allocator
		/************************************************************************/
		VkBindBufferMemoryInfoKHR            bindInfo = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO_KHR };
		VkBindBufferMemoryDeviceGroupInfoKHR bindDeviceGroup = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO_KHR };
		bindDeviceGroup.deviceIndexCount = pRenderer->mLinkedNodeCount;
		bindDeviceGroup.pDeviceIndices = pIndices;
		bindInfo.buffer = pBuffer->pVkBuffer;
		bindInfo.memory = allocInfo.deviceMemory;
		bindInfo.memoryOffset = allocInfo.offset;
		bindInfo.pNext = &bindDeviceGroup;
		vkBindBufferMemory2KHR(pRenderer->pVkDevice, 1, &bindInfo);
		/************************************************************************/
		/************************************************************************/
	}

	pBuffer->mCurrentState = RESOURCE_STATE_UNDEFINED;
	/************************************************************************/
	// Set descriptor data
	/************************************************************************/
	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) || (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_BUFFER) ||
		(pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER))
	{
		pBuffer->mVkBufferInfo.buffer = pBuffer->pVkBuffer;
		pBuffer->mVkBufferInfo.range = VK_WHOLE_SIZE;

		if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_BUFFER) || (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER))
		{
			pBuffer->mVkBufferInfo.offset = pBuffer->mDesc.mStructStride * pBuffer->mDesc.mFirstElement;
		}
		else
		{
			pBuffer->mVkBufferInfo.offset = 0;
		}
	}

	if (add_info.usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)
	{
		VkBufferViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL };
		viewInfo.buffer = pBuffer->pVkBuffer;
		viewInfo.flags = 0;
		viewInfo.format = (VkFormat)TinyImageFormat_ToVkFormat(pDesc->mFormat);
		viewInfo.offset = pDesc->mFirstElement * pDesc->mStructStride;
		viewInfo.range = pDesc->mElementCount * pDesc->mStructStride;
		VkFormatProperties formatProps = {};
		vkGetPhysicalDeviceFormatProperties(pRenderer->pVkActiveGPU, viewInfo.format, &formatProps);
		if (!(formatProps.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT))
		{
			LOGF(LogLevel::eWARNING, "Failed to create uniform texel buffer view for format %u", (uint32_t)pDesc->mFormat);
		}
		else
		{
			vkCreateBufferView(pRenderer->pVkDevice, &viewInfo, NULL, &pBuffer->pVkUniformTexelView);
		}
	}
	if (add_info.usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
	{
		VkBufferViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, NULL };
		viewInfo.buffer = pBuffer->pVkBuffer;
		viewInfo.flags = 0;
		viewInfo.format = (VkFormat)TinyImageFormat_ToVkFormat(pDesc->mFormat);
		viewInfo.offset = pDesc->mFirstElement * pDesc->mStructStride;
		viewInfo.range = pDesc->mElementCount * pDesc->mStructStride;
		VkFormatProperties formatProps = {};
		vkGetPhysicalDeviceFormatProperties(pRenderer->pVkActiveGPU, viewInfo.format, &formatProps);
		if (!(formatProps.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))
		{
			LOGF(LogLevel::eWARNING, "Failed to create storage texel buffer view for format %u", (uint32_t)pDesc->mFormat);
		}
		else
		{
			vkCreateBufferView(pRenderer->pVkDevice, &viewInfo, NULL, &pBuffer->pVkStorageTexelView);
		}
	}
	/************************************************************************/
	/************************************************************************/
	*pp_buffer = pBuffer;
}

void removeBuffer(Renderer* pRenderer, Buffer* pBuffer)
{
	ASSERT(pRenderer);
	ASSERT(pBuffer);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pBuffer->pVkBuffer);

	if (pBuffer->pVkUniformTexelView)
	{
		vkDestroyBufferView(pRenderer->pVkDevice, pBuffer->pVkUniformTexelView, NULL);
		pBuffer->pVkUniformTexelView = VK_NULL_HANDLE;
	}
	if (pBuffer->pVkStorageTexelView)
	{
		vkDestroyBufferView(pRenderer->pVkDevice, pBuffer->pVkStorageTexelView, NULL);
		pBuffer->pVkStorageTexelView = VK_NULL_HANDLE;
	}

	vk_destroyBuffer(pRenderer->pVmaAllocator, pBuffer);

	SAFE_FREE(pBuffer);
}

// Allocate Vulkan memory for the virtual page
bool allocateVirtualPage(Renderer* pRenderer, Texture* pTexture, VirtualTexturePage &virtualPage, uint32_t memoryTypeIndex)
{
	if (virtualPage.imageMemoryBind.memory != VK_NULL_HANDLE)
	{
		//already filled
		return false;
	};

	BufferDesc desc = {};
	desc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
	desc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
	desc.mFlags = BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT;
	desc.mFormat = pTexture->mDesc.mFormat;

	desc.mFirstElement = 0;
	desc.mElementCount = pTexture->mSparseVirtualTexturePageWidth * pTexture->mSparseVirtualTexturePageHeight;
	desc.mStructStride = sizeof(uint32_t);
	desc.mSize = desc.mElementCount * desc.mStructStride;
	addBuffer(pRenderer, &desc, &virtualPage.pIntermediateBuffer);

	virtualPage.imageMemoryBind = {};

	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = virtualPage.size;
	allocInfo.memoryTypeIndex = memoryTypeIndex;

	VkResult vk_res = (VkResult)vkAllocateMemory(pRenderer->pVkDevice, &allocInfo, nullptr, &virtualPage.imageMemoryBind.memory);
	assert(vk_res == VK_SUCCESS);

	VkImageSubresource subResource{};
	subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subResource.mipLevel = virtualPage.mipLevel;
	subResource.arrayLayer = virtualPage.layer;

	// Sparse image memory binding
	virtualPage.imageMemoryBind.subresource = subResource;
	virtualPage.imageMemoryBind.extent = virtualPage.extent;
	virtualPage.imageMemoryBind.offset = virtualPage.offset;

	return true;
}

// Release Vulkan memory allocated for this page
void releaseVirtualPage(Renderer* pRenderer, VirtualTexturePage &virtualPage, bool removeMemoryBind)
{
	//TODO: This should be also removed
	if (removeMemoryBind && virtualPage.imageMemoryBind.memory != VK_NULL_HANDLE)
	{
		vkFreeMemory(pRenderer->pVkDevice, virtualPage.imageMemoryBind.memory, nullptr);
		virtualPage.imageMemoryBind.memory = VK_NULL_HANDLE;
	}

	if(virtualPage.pIntermediateBuffer)
	{
		removeBuffer(pRenderer, virtualPage.pIntermediateBuffer);
		virtualPage.pIntermediateBuffer = NULL;
	}
}

VirtualTexturePage* addPage(Renderer* pRenderer, Texture* pTexture, VkOffset3D offset, VkExtent3D extent, const VkDeviceSize size, const uint32_t mipLevel, uint32_t layer)
{
	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;

	VirtualTexturePage newPage;
	newPage.offset = offset;
	newPage.extent = extent;
	newPage.size = size;
	newPage.mipLevel = mipLevel;
	newPage.layer = layer;
	newPage.index = static_cast<uint32_t>(pPageTable->size());	

	pPageTable->push_back(newPage);

	return &pPageTable->back();
}

void addTexture(Renderer* pRenderer, const TextureDesc* pDesc, Texture** ppTexture)
{
	ASSERT(pRenderer);
	ASSERT(pDesc && pDesc->mWidth && pDesc->mHeight && (pDesc->mDepth || pDesc->mArraySize));
	if (pDesc->mSampleCount > SAMPLE_COUNT_1 && pDesc->mMipLevels > 1)
	{
		LOGF(LogLevel::eERROR, "Multi-Sampled textures cannot have mip maps");
		ASSERT(false);
		return;
	}

	Texture* pTexture = (Texture*)conf_calloc(1, sizeof(*pTexture));
	ASSERT(pTexture);

	pTexture->mDesc = *pDesc;

	if (pDesc->pNativeHandle && !(pDesc->mFlags & TEXTURE_CREATION_FLAG_IMPORT_BIT))
	{
		pTexture->mOwnsImage = false;
		pTexture->pVkImage = (VkImage)pDesc->pNativeHandle;
	}
	else
	{
		pTexture->mOwnsImage = true;
	}

	VkImageUsageFlags additionalFlags = 0;
	if (pDesc->mStartState & RESOURCE_STATE_RENDER_TARGET)
		additionalFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	else if (pDesc->mStartState & RESOURCE_STATE_DEPTH_WRITE)
		additionalFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VkImageType image_type = VK_IMAGE_TYPE_MAX_ENUM;
	if (pDesc->mFlags & TEXTURE_CREATION_FLAG_FORCE_2D)
	{
		ASSERT(pDesc->mDepth == 1);
		image_type = VK_IMAGE_TYPE_2D;
	}
	else if (pDesc->mFlags & TEXTURE_CREATION_FLAG_FORCE_3D)
	{
		image_type = VK_IMAGE_TYPE_3D;
	}
	else
	{
		if (pDesc->mDepth > 1)
			image_type = VK_IMAGE_TYPE_3D;
		else if (pDesc->mHeight > 1)
			image_type = VK_IMAGE_TYPE_2D;
		else
			image_type = VK_IMAGE_TYPE_1D;
	}

	DescriptorType descriptors = pDesc->mDescriptors;
	bool           cubemapRequired = (DESCRIPTOR_TYPE_TEXTURE_CUBE == (descriptors & DESCRIPTOR_TYPE_TEXTURE_CUBE));
	bool           arrayRequired = false;

	if (VK_NULL_HANDLE == pTexture->pVkImage)
	{
		DECLARE_ZERO(VkImageCreateInfo, add_info);
		add_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		add_info.pNext = NULL;
		add_info.flags = 0;
		add_info.imageType = image_type;
		add_info.format = (VkFormat)TinyImageFormat_ToVkFormat(pDesc->mFormat);
		add_info.extent.width = pDesc->mWidth;
		add_info.extent.height = pDesc->mHeight;
		add_info.extent.depth = pDesc->mDepth;
		add_info.mipLevels = pDesc->mMipLevels;
		add_info.arrayLayers = pDesc->mArraySize;
		add_info.samples = util_to_vk_sample_count(pDesc->mSampleCount);
		add_info.tiling = (0 != pDesc->mHostVisible) ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
		add_info.usage = util_to_vk_image_usage(descriptors);
		add_info.usage |= additionalFlags;
		add_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		add_info.queueFamilyIndexCount = 0;
		add_info.pQueueFamilyIndices = NULL;
		add_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		if (cubemapRequired)
			add_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		if (arrayRequired)
			add_info.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT_KHR;

		if (VK_IMAGE_USAGE_SAMPLED_BIT & add_info.usage)
		{
			// Make it easy to copy to and from textures
			add_info.usage |= (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
		}

		ASSERT(pRenderer->capBits.canShaderReadFrom[pDesc->mFormat] && "GPU shader can't' read from this format");

		// TODO Deano move hostvisible flag to capbits structure
		// Verify that GPU supports this format
		DECLARE_ZERO(VkFormatProperties, format_props);
		vkGetPhysicalDeviceFormatProperties(pRenderer->pVkActiveGPU, add_info.format, &format_props);
		VkFormatFeatureFlags format_features = util_vk_image_usage_to_format_features(add_info.usage);

		if (pDesc->mHostVisible)
		{
			VkFormatFeatureFlags flags = format_props.linearTilingFeatures & format_features;
			ASSERT((0 != flags) && "Format is not supported for host visible images");
		}
		else
		{
			VkFormatFeatureFlags flags = format_props.optimalTilingFeatures & format_features;
			ASSERT((0 != flags) && "Format is not supported for GPU local images (i.e. not host visible images)");
		}

		const bool linkedMultiGpu = (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED) && (pDesc->pSharedNodeIndices || pDesc->mNodeIndex);

		AllocatorMemoryRequirements mem_reqs = { 0 };
		if (pDesc->mFlags & TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT)
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		if (linkedMultiGpu)
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DONT_BIND_BIT;
		mem_reqs.usage = (VmaMemoryUsage)VMA_MEMORY_USAGE_GPU_ONLY;

		VkExternalMemoryImageCreateInfoKHR externalInfo = { VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO_KHR, NULL };

#if defined(VK_USE_PLATFORM_WIN32_KHR)
		VkImportMemoryWin32HandleInfoKHR importInfo = { VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR, NULL };
#endif
		VkExportMemoryAllocateInfoKHR exportMemoryInfo = { VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR, NULL };

		if (gExternalMemoryExtension && pDesc->mFlags & TEXTURE_CREATION_FLAG_IMPORT_BIT)
		{
			add_info.pNext = &externalInfo;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
			struct ImportHandleInfo
			{
				void*                                 pHandle;
				VkExternalMemoryHandleTypeFlagBitsKHR mHandleType;
			};

			ImportHandleInfo* pHandleInfo = (ImportHandleInfo*)pDesc->pNativeHandle;
			importInfo.handle = pHandleInfo->pHandle;
			importInfo.handleType = pHandleInfo->mHandleType;

			externalInfo.handleTypes = pHandleInfo->mHandleType;

			mem_reqs.pUserData = &importInfo;
			// Allocate external (importable / exportable) memory as dedicated memory to avoid adding unnecessary complexity to the Vulkan Memory Allocator
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
#endif
		}
		else if (gExternalMemoryExtension && pDesc->mFlags & TEXTURE_CREATION_FLAG_EXPORT_BIT)
		{
#if defined(VK_USE_PLATFORM_WIN32_KHR)
			exportMemoryInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR;
#endif

			mem_reqs.pUserData = &exportMemoryInfo;
			// Allocate external (importable / exportable) memory as dedicated memory to avoid adding unnecessary complexity to the Vulkan Memory Allocator
			mem_reqs.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}

		TextureCreateInfo alloc_info = { pDesc, &add_info };
		VkResult          vk_res = (VkResult)vk_createTexture(pRenderer->pVmaAllocator, &alloc_info, &mem_reqs, pTexture);
		ASSERT(VK_SUCCESS == vk_res);
		/************************************************************************/
		// Texture to be used on multiple GPUs
		/************************************************************************/
		if (linkedMultiGpu)
		{
			VmaAllocationInfo allocInfo = {};
			vmaGetAllocationInfo(pRenderer->pVmaAllocator, pTexture->pVkAllocation, &allocInfo);
			/************************************************************************/
			// Set all the device indices to the index of the device where we will create the texture
			/************************************************************************/
			uint32_t* pIndices = (uint32_t*)alloca(pRenderer->mLinkedNodeCount * sizeof(uint32_t));
			util_calculate_device_indices(pRenderer, pDesc->mNodeIndex, pDesc->pSharedNodeIndices, pDesc->mSharedNodeIndexCount, pIndices);
			/************************************************************************/
			// #TODO : Move this to the Vulkan memory allocator
			/************************************************************************/
			VkBindImageMemoryInfoKHR            bindInfo = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO_KHR };
			VkBindImageMemoryDeviceGroupInfoKHR bindDeviceGroup = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO_KHR };
			bindDeviceGroup.deviceIndexCount = pRenderer->mLinkedNodeCount;
			bindDeviceGroup.pDeviceIndices = pIndices;
			bindInfo.image = pTexture->pVkImage;
			bindInfo.memory = allocInfo.deviceMemory;
			bindInfo.memoryOffset = allocInfo.offset;
			bindInfo.pNext = &bindDeviceGroup;
			vkBindImageMemory2KHR(pRenderer->pVkDevice, 1, &bindInfo);
			/************************************************************************/
			/************************************************************************/
		}

		pTexture->mCurrentState = RESOURCE_STATE_UNDEFINED;
	}
	/************************************************************************/
	// Create image view
	/************************************************************************/
	VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch (image_type)
	{
		case VK_IMAGE_TYPE_1D: view_type = pDesc->mArraySize > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D; break;
		case VK_IMAGE_TYPE_2D:
			if (cubemapRequired)
				view_type = (pDesc->mArraySize > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
			else
				view_type = pDesc->mArraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			break;
		case VK_IMAGE_TYPE_3D:
			if (pDesc->mArraySize > 1)
			{
				LOGF(LogLevel::eERROR, "Cannot support 3D Texture Array in Vulkan");
				ASSERT(false);
			}
			view_type = VK_IMAGE_VIEW_TYPE_3D;
			break;
		default: ASSERT(false && "Image Format not supported!"); break;
	}

	ASSERT(view_type != VK_IMAGE_VIEW_TYPE_MAX_ENUM && "Invalid Image View");

	VkImageViewCreateInfo srvDesc = {};
	// SRV
	srvDesc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	srvDesc.pNext = NULL;
	srvDesc.flags = 0;
	srvDesc.image = pTexture->pVkImage;
	srvDesc.viewType = view_type;
	srvDesc.format = (VkFormat)TinyImageFormat_ToVkFormat(pDesc->mFormat);
	srvDesc.components.r = VK_COMPONENT_SWIZZLE_R;
	srvDesc.components.g = VK_COMPONENT_SWIZZLE_G;
	srvDesc.components.b = VK_COMPONENT_SWIZZLE_B;
	srvDesc.components.a = VK_COMPONENT_SWIZZLE_A;
	srvDesc.subresourceRange.aspectMask = util_vk_determine_aspect_mask(srvDesc.format, false);
	srvDesc.subresourceRange.baseMipLevel = 0;
	srvDesc.subresourceRange.levelCount = pDesc->mMipLevels;
	srvDesc.subresourceRange.baseArrayLayer = 0;
	srvDesc.subresourceRange.layerCount = pDesc->mArraySize;
	pTexture->mVkAspectMask = util_vk_determine_aspect_mask(srvDesc.format, true);
	if (descriptors & DESCRIPTOR_TYPE_TEXTURE)
	{
		VkResult vk_res = vkCreateImageView(pRenderer->pVkDevice, &srvDesc, NULL, &pTexture->pVkSRVDescriptor);
		ASSERT(VK_SUCCESS == vk_res);
	}

	// SRV stencil
	if ((TinyImageFormat_IsStencilOnly(pDesc->mFormat)|| TinyImageFormat_IsDepthAndStencil(pDesc->mFormat))
				&& (descriptors & DESCRIPTOR_TYPE_TEXTURE))
	{
		pTexture->pVkSRVStencilDescriptor = (VkImageView*)conf_malloc(sizeof(VkImageView));
		srvDesc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
		VkResult vk_res = vkCreateImageView(pRenderer->pVkDevice, &srvDesc, NULL, pTexture->pVkSRVStencilDescriptor);
		ASSERT(VK_SUCCESS == vk_res);
	}

	// UAV
	if (descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
	{
		pTexture->pVkUAVDescriptors = (VkImageView*)conf_calloc(pDesc->mMipLevels, sizeof(VkImageView));
		VkImageViewCreateInfo uavDesc = srvDesc;
		// #NOTE : We dont support imageCube, imageCubeArray for consistency with other APIs
		// All cubemaps will be used as image2DArray for Image Load / Store ops
		if (uavDesc.viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY || uavDesc.viewType == VK_IMAGE_VIEW_TYPE_CUBE)
			uavDesc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		uavDesc.subresourceRange.levelCount = 1;
		for (uint32_t i = 0; i < pDesc->mMipLevels; ++i)
		{
			uavDesc.subresourceRange.baseMipLevel = i;
			VkResult vk_res = vkCreateImageView(pRenderer->pVkDevice, &uavDesc, NULL, &pTexture->pVkUAVDescriptors[i]);
			ASSERT(VK_SUCCESS == vk_res);
		}
	}
	/************************************************************************/
	/************************************************************************/
	// Get memory requirements that covers all mip levels
	DECLARE_ZERO(VkMemoryRequirements, vk_mem_reqs);
	vkGetImageMemoryRequirements(pRenderer->pVkDevice, pTexture->pVkImage, &vk_mem_reqs);
	pTexture->mTextureSize = vk_mem_reqs.size;

	if (pDesc->pDebugName)
	{
		pTexture->mDesc.pDebugName = (wchar_t*)conf_calloc(wcslen(pDesc->pDebugName) + 1, sizeof(wchar_t));
		wcscpy((wchar_t*)pTexture->mDesc.pDebugName, pDesc->pDebugName);
	}

	*ppTexture = pTexture;
}

void removeTexture(Renderer* pRenderer, Texture* pTexture)
{
	ASSERT(pRenderer);
	ASSERT(pTexture);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pTexture->pVkImage);

	if (pTexture->mOwnsImage)
		vk_destroyTexture(pRenderer->pVmaAllocator, pTexture);

	if (VK_NULL_HANDLE != pTexture->pVkSRVDescriptor)
		vkDestroyImageView(pRenderer->pVkDevice, pTexture->pVkSRVDescriptor, NULL);

	if (VK_NULL_HANDLE != pTexture->pVkSRVStencilDescriptor)
		vkDestroyImageView(pRenderer->pVkDevice, *pTexture->pVkSRVStencilDescriptor, NULL);

	if (pTexture->pVkUAVDescriptors)
	{
		for (uint32_t i = 0; i < pTexture->mDesc.mMipLevels; ++i)
		{
			vkDestroyImageView(pRenderer->pVkDevice, pTexture->pVkUAVDescriptors[i], NULL);
		}
	}

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;

	if(pPageTable)
	{
		for (int i = 0; i < (int)pPageTable->size(); i++)
		{
			releaseVirtualPage(pRenderer, (*pPageTable)[i], true);
		}

		pPageTable->set_capacity(0);
		SAFE_FREE(pTexture->pPages);
	}

	eastl::vector<VkSparseImageMemoryBind>* pImageMemory = (eastl::vector<VkSparseImageMemoryBind>*)pTexture->pSparseImageMemoryBinds;

	if (pImageMemory)
	{
		pImageMemory->set_capacity(0);
		SAFE_FREE(pTexture->pSparseImageMemoryBinds);
	}

	eastl::vector<VkSparseMemoryBind>* pOpaqueMemory = (eastl::vector<VkSparseMemoryBind>*)pTexture->pOpaqueMemoryBinds;

	if(pOpaqueMemory)
	{
		for (int i = 0; i < (int)pOpaqueMemory->size(); i++)
		{
			vkFreeMemory(pRenderer->pVkDevice, (*pOpaqueMemory)[i].memory, nullptr);
		}

		pOpaqueMemory->set_capacity(0);
		SAFE_FREE(pTexture->pOpaqueMemoryBinds);
	}

	if(pTexture->mVisibility)
		removeBuffer(pRenderer, pTexture->mVisibility);

	if (pTexture->mPrevVisibility)
		removeBuffer(pRenderer, pTexture->mPrevVisibility);

	if (pTexture->mAlivePage)
		removeBuffer(pRenderer, pTexture->mAlivePage);

	if (pTexture->mAlivePageCount)
		removeBuffer(pRenderer, pTexture->mAlivePageCount);

	if (pTexture->mRemovePage)
		removeBuffer(pRenderer, pTexture->mRemovePage);

	if (pTexture->mRemovePageCount)
		removeBuffer(pRenderer, pTexture->mRemovePageCount);

	if(pTexture->mVirtualImageData)
		conf_free(pTexture->mVirtualImageData);

	SAFE_FREE((wchar_t*)pTexture->mDesc.pDebugName);
	SAFE_FREE(pTexture->pVkSRVStencilDescriptor);
	SAFE_FREE(pTexture->pVkUAVDescriptors);
	SAFE_FREE(pTexture);
}

void addRenderTarget(Renderer* pRenderer, const RenderTargetDesc* pDesc, RenderTarget** ppRenderTarget)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(ppRenderTarget);

	bool const isDepth = TinyImageFormat_IsDepthOnly(pDesc->mFormat) ||
			TinyImageFormat_IsDepthAndStencil(pDesc->mFormat);

	ASSERT(!((isDepth) && (pDesc->mDescriptors & DESCRIPTOR_TYPE_RW_TEXTURE)) && "Cannot use depth stencil as UAV");

	((RenderTargetDesc*)pDesc)->mMipLevels = max(1U, pDesc->mMipLevels);

	RenderTarget* pRenderTarget = (RenderTarget*)conf_calloc(1, sizeof(*pRenderTarget));
	// Monotonically increasing thread safe id generation
	pRenderTarget->mId = tfrg_atomic64_add_relaxed(&gRenderTargetIds, 1);

	pRenderTarget->mDesc = *pDesc;
	TextureDesc textureDesc = {};
	textureDesc.mArraySize = pDesc->mArraySize;
	textureDesc.mClearValue = pDesc->mClearValue;
	textureDesc.mDepth = pDesc->mDepth;
	textureDesc.mFlags = pDesc->mFlags;
	textureDesc.mFormat = pDesc->mFormat;
	textureDesc.mHeight = pDesc->mHeight;
	textureDesc.mHostVisible = false;
	textureDesc.mMipLevels = pDesc->mMipLevels;
	textureDesc.mSampleCount = pDesc->mSampleCount;
	textureDesc.mSampleQuality = pDesc->mSampleQuality;
	textureDesc.mWidth = pDesc->mWidth;
	textureDesc.pNativeHandle = pDesc->pNativeHandle;
	textureDesc.mNodeIndex = pDesc->mNodeIndex;
	textureDesc.pSharedNodeIndices = pDesc->pSharedNodeIndices;
	textureDesc.mSharedNodeIndexCount = pDesc->mSharedNodeIndexCount;

	if (!isDepth)
		textureDesc.mStartState |= RESOURCE_STATE_RENDER_TARGET;
	else
		textureDesc.mStartState |= RESOURCE_STATE_DEPTH_WRITE;

	// Set this by default to be able to sample the rendertarget in shader
	textureDesc.mDescriptors = pDesc->mDescriptors;
	// Create SRV by default for a render target
	textureDesc.mDescriptors |= DESCRIPTOR_TYPE_TEXTURE;

	if (isDepth)
	{
		// Make sure depth/stencil format is supported - fall back to VK_FORMAT_D16_UNORM if not
		VkFormat vk_depth_stencil_format = (VkFormat) TinyImageFormat_ToVkFormat(pDesc->mFormat);
		if (VK_FORMAT_UNDEFINED != vk_depth_stencil_format)
		{
			DECLARE_ZERO(VkImageFormatProperties, properties);
			VkResult vk_res = vkGetPhysicalDeviceImageFormatProperties(
				pRenderer->pVkActiveGPU, vk_depth_stencil_format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
				VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &properties);
			// Fall back to something that's guaranteed to work
			if (VK_SUCCESS != vk_res)
			{
				textureDesc.mFormat = TinyImageFormat_D16_UNORM;
				LOGF(LogLevel::eWARNING, "Depth stencil format (%u) not supported. Falling back to D16 format", pDesc->mFormat);
			}
		}
	}

	::addTexture(pRenderer, &textureDesc, &pRenderTarget->pTexture);

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	if (pDesc->mDepth > 1)
		viewType = VK_IMAGE_VIEW_TYPE_3D;
	else if (pDesc->mHeight > 1)
		viewType = pDesc->mArraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	else
		viewType = pDesc->mArraySize > 1 ? VK_IMAGE_VIEW_TYPE_1D_ARRAY : VK_IMAGE_VIEW_TYPE_1D;

	uint32_t depthOrArraySize = pDesc->mArraySize * pDesc->mDepth;

	VkImageViewCreateInfo rtvDesc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL };
	rtvDesc.flags = 0;
	rtvDesc.image = pRenderTarget->pTexture->pVkImage;
	rtvDesc.viewType = viewType;
	rtvDesc.format = (VkFormat)TinyImageFormat_ToVkFormat(pRenderTarget->pTexture->mDesc.mFormat);
	rtvDesc.components.r = VK_COMPONENT_SWIZZLE_R;
	rtvDesc.components.g = VK_COMPONENT_SWIZZLE_G;
	rtvDesc.components.b = VK_COMPONENT_SWIZZLE_B;
	rtvDesc.components.a = VK_COMPONENT_SWIZZLE_A;
	rtvDesc.subresourceRange.aspectMask = util_vk_determine_aspect_mask(rtvDesc.format, true);
	rtvDesc.subresourceRange.baseMipLevel = 0;
	rtvDesc.subresourceRange.levelCount = 1;
	rtvDesc.subresourceRange.baseArrayLayer = 0;
	rtvDesc.subresourceRange.layerCount = depthOrArraySize;

	uint32_t numRTVs = pDesc->mMipLevels;
	if ((pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		numRTVs *= depthOrArraySize;

	pRenderTarget->pVkDescriptors = (VkImageView*)conf_calloc(numRTVs + 1, sizeof(VkImageView));
	vkCreateImageView(pRenderer->pVkDevice, &rtvDesc, NULL, &pRenderTarget->pVkDescriptors[0]);

	for (uint32_t i = 0; i < pDesc->mMipLevels; ++i)
	{
		rtvDesc.subresourceRange.baseMipLevel = i;
		if ((pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
			(pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		{
			for (uint32_t j = 0; j < depthOrArraySize; ++j)
			{
				rtvDesc.subresourceRange.layerCount = 1;
				rtvDesc.subresourceRange.baseArrayLayer = j;
				VkResult vkRes =
					vkCreateImageView(pRenderer->pVkDevice, &rtvDesc, NULL, &pRenderTarget->pVkDescriptors[1 + i * depthOrArraySize + j]);
				ASSERT(VK_SUCCESS == vkRes);
			}
		}
		else
		{
			VkResult vkRes = vkCreateImageView(pRenderer->pVkDevice, &rtvDesc, NULL, &pRenderTarget->pVkDescriptors[1 + i]);
			ASSERT(VK_SUCCESS == vkRes);
		}
	}

	*ppRenderTarget = pRenderTarget;
}

void removeRenderTarget(Renderer* pRenderer, RenderTarget* pRenderTarget)
{
	::removeTexture(pRenderer, pRenderTarget->pTexture);

	vkDestroyImageView(pRenderer->pVkDevice, pRenderTarget->pVkDescriptors[0], NULL);

	const uint32_t depthOrArraySize = pRenderTarget->mDesc.mArraySize * pRenderTarget->mDesc.mDepth;
	if ((pRenderTarget->mDesc.mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(pRenderTarget->mDesc.mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
	{
		for (uint32_t i = 0; i < pRenderTarget->mDesc.mMipLevels; ++i)
			for (uint32_t j = 0; j < depthOrArraySize; ++j)
				vkDestroyImageView(pRenderer->pVkDevice, pRenderTarget->pVkDescriptors[1 + i * depthOrArraySize + j], NULL);
	}
	else
	{
		for (uint32_t i = 0; i < pRenderTarget->mDesc.mMipLevels; ++i)
			vkDestroyImageView(pRenderer->pVkDevice, pRenderTarget->pVkDescriptors[1 + i], NULL);
	}

	SAFE_FREE(pRenderTarget->pVkDescriptors);
	SAFE_FREE(pRenderTarget);
}

void addSampler(Renderer* pRenderer, const SamplerDesc* pDesc, Sampler** pp_sampler)
{
	ASSERT(pRenderer);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(pDesc->mCompareFunc < MAX_COMPARE_MODES);

	Sampler* pSampler = (Sampler*)conf_calloc(1, sizeof(*pSampler));
	ASSERT(pSampler);

	DECLARE_ZERO(VkSamplerCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.magFilter = util_to_vk_filter(pDesc->mMagFilter);
	add_info.minFilter = util_to_vk_filter(pDesc->mMinFilter);
	add_info.mipmapMode = util_to_vk_mip_map_mode(pDesc->mMipMapMode);
	add_info.addressModeU = util_to_vk_address_mode(pDesc->mAddressU);
	add_info.addressModeV = util_to_vk_address_mode(pDesc->mAddressV);
	add_info.addressModeW = util_to_vk_address_mode(pDesc->mAddressW);
	add_info.mipLodBias = pDesc->mMipLodBias;
	add_info.anisotropyEnable = (pDesc->mMaxAnisotropy > 0.0f)? VK_TRUE : VK_FALSE;
	add_info.maxAnisotropy = pDesc->mMaxAnisotropy;
	add_info.compareEnable = (gVkComparisonFuncTranslator[pDesc->mCompareFunc] != VK_COMPARE_OP_NEVER) ? VK_TRUE : VK_FALSE;
	add_info.compareOp = gVkComparisonFuncTranslator[pDesc->mCompareFunc];
	add_info.minLod = 0.0f;
	add_info.maxLod = ((pDesc->mMipMapMode == MIPMAP_MODE_LINEAR) ? FLT_MAX : 0.0f);
	add_info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	add_info.unnormalizedCoordinates = VK_FALSE;

	VkResult vk_res = vkCreateSampler(pRenderer->pVkDevice, &add_info, NULL, &(pSampler->pVkSampler));
	ASSERT(VK_SUCCESS == vk_res);

	*pp_sampler = pSampler;
}

void removeSampler(Renderer* pRenderer, Sampler* pSampler)
{
	ASSERT(pRenderer);
	ASSERT(pSampler);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pSampler->pVkSampler);

	vkDestroySampler(pRenderer->pVkDevice, pSampler->pVkSampler, NULL);

	SAFE_FREE(pSampler);
}
/************************************************************************/
// Buffer Functions
/************************************************************************/
void mapBuffer(Renderer* pRenderer, Buffer* pBuffer, ReadRange* pRange)
{
	ASSERT(pBuffer->mDesc.mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to map non-cpu accessible resource");

	VkResult vk_res = vmaMapMemory(pRenderer->pVmaAllocator, pBuffer->pVkAllocation, &pBuffer->pCpuMappedAddress);
	ASSERT(vk_res == VK_SUCCESS);

	if (pRange)
	{
		pBuffer->pCpuMappedAddress = ((uint8_t*)pBuffer->pCpuMappedAddress + pRange->mOffset);
	}
}

void unmapBuffer(Renderer* pRenderer, Buffer* pBuffer)
{
	ASSERT(pBuffer->mDesc.mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to unmap non-cpu accessible resource");

	vmaUnmapMemory(pRenderer->pVmaAllocator, pBuffer->pVkAllocation);
	pBuffer->pCpuMappedAddress = NULL;
}


/************************************************************************/
// Descriptor Set Functions
/************************************************************************/
void addDescriptorSet(Renderer* pRenderer, const DescriptorSetDesc* pDesc, DescriptorSet** ppDescriptorSet)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(ppDescriptorSet);

	DescriptorSet* pDescriptorSet = (DescriptorSet*)conf_calloc(1, sizeof(*pDescriptorSet));
	ASSERT(pDescriptorSet);

	const RootSignature* pRootSignature = pDesc->pRootSignature;
	const DescriptorUpdateFrequency updateFreq = pDesc->mUpdateFrequency;
	const uint32_t nodeIndex = pDesc->mNodeIndex;

	pDescriptorSet->pRootSignature = pRootSignature;
	pDescriptorSet->mUpdateFrequency = updateFreq;
	pDescriptorSet->mDynamicOffsetCount = pRootSignature->mVkDynamicDescriptorCounts[updateFreq];
	pDescriptorSet->mNodeIndex = nodeIndex;
	pDescriptorSet->mMaxSets = pDesc->mMaxSets;

	if (pDescriptorSet->mDynamicOffsetCount)
	{
		pDescriptorSet->pDynamicOffsets = (uint32_t**)conf_calloc(pDescriptorSet->mMaxSets, sizeof(uint32_t*));
		pDescriptorSet->pDynamicSizes = (uint32_t**)conf_calloc(pDescriptorSet->mMaxSets, sizeof(uint32_t*));
		for (uint32_t i = 0; i < pDescriptorSet->mMaxSets; ++i)
		{
			pDescriptorSet->pDynamicOffsets[i] = (uint32_t*)conf_calloc(pDescriptorSet->mDynamicOffsetCount, sizeof(uint32_t));
			pDescriptorSet->pDynamicSizes[i] = (uint32_t*)conf_calloc(pDescriptorSet->mDynamicOffsetCount, sizeof(uint32_t));
		}
	}

	if (VK_NULL_HANDLE != pRootSignature->mVkDescriptorSetLayouts[updateFreq])
	{
		pDescriptorSet->pHandles = (VkDescriptorSet*)conf_calloc(pDesc->mMaxSets, sizeof(VkDescriptorSet));
		pDescriptorSet->ppUpdateData = (DescriptorUpdateData**)conf_calloc(pDesc->mMaxSets, sizeof(DescriptorUpdateData*));

		uint32_t descriptorCount = pRootSignature->mVkCumulativeDescriptorCounts[updateFreq];
		VkDescriptorSetLayout* pLayouts = (VkDescriptorSetLayout*)alloca(pDesc->mMaxSets * sizeof(VkDescriptorSetLayout));
		VkDescriptorSet** pHandles = (VkDescriptorSet**)alloca(pDesc->mMaxSets * sizeof(VkDescriptorSet*));

		for (uint32_t i = 0; i < pDesc->mMaxSets; ++i)
		{
			pLayouts[i] = pRootSignature->mVkDescriptorSetLayouts[updateFreq];
			pHandles[i] = &pDescriptorSet->pHandles[i];

			pDescriptorSet->ppUpdateData[i] = (DescriptorUpdateData*)conf_malloc(pRootSignature->mVkCumulativeDescriptorCounts[updateFreq] *
				sizeof(DescriptorUpdateData));
			memcpy(pDescriptorSet->ppUpdateData[i], pRootSignature->pUpdateTemplateData[updateFreq][pDescriptorSet->mNodeIndex], pRootSignature->mVkCumulativeDescriptorCounts[updateFreq] * sizeof(DescriptorUpdateData));
		}

		consume_descriptor_sets(pRenderer->pDescriptorPool, pLayouts, pHandles, pDesc->mMaxSets);
	}
	else
	{
		LOGF(LogLevel::eERROR, "NULL Descriptor Set Layout for update frequency %u. Cannot allocate descriptor set", (uint32_t)updateFreq);
		ASSERT(false && "NULL Descriptor Set Layout for update frequency. Cannot allocate descriptor set");
	}

	*ppDescriptorSet = pDescriptorSet;
}

void removeDescriptorSet(Renderer* pRenderer, DescriptorSet* pDescriptorSet)
{
	ASSERT(pRenderer);
	ASSERT(pDescriptorSet);

	if (pDescriptorSet->mDynamicOffsetCount)
	{
		for (uint32_t i = 0; i < pDescriptorSet->mMaxSets; ++i)
		{
			SAFE_FREE(pDescriptorSet->pDynamicOffsets[i]);
			SAFE_FREE(pDescriptorSet->pDynamicSizes[i]);
		}
	}

	for (uint32_t i = 0; i < pDescriptorSet->mMaxSets; ++i)
	{
		SAFE_FREE(pDescriptorSet->ppUpdateData[i]);
	}

	SAFE_FREE(pDescriptorSet->ppUpdateData);
	SAFE_FREE(pDescriptorSet->pHandles);
	SAFE_FREE(pDescriptorSet->pDynamicOffsets);
	SAFE_FREE(pDescriptorSet->pDynamicSizes);
	SAFE_FREE(pDescriptorSet);
}

void updateDescriptorSet(Renderer* pRenderer, uint32_t index, DescriptorSet* pDescriptorSet, uint32_t count, const DescriptorData* pParams)
{
#ifdef _DEBUG
#define VALIDATE_DESCRIPTOR(descriptor,...)																\
	if (!(descriptor))																					\
	{																									\
		eastl::string msg = __FUNCTION__ + eastl::string(" : ") + eastl::string().sprintf(__VA_ARGS__);	\
		LOGF(LogLevel::eERROR, msg.c_str());															\
		_FailedAssert(__FILE__, __LINE__, msg.c_str());													\
		continue;																						\
	}
#else
#define VALIDATE_DESCRIPTOR(descriptor,...)
#endif

	ASSERT(pRenderer);
	ASSERT(pDescriptorSet);
	ASSERT(pDescriptorSet->pHandles);
	ASSERT(index < pDescriptorSet->mMaxSets);

	const RootSignature* pRootSignature = pDescriptorSet->pRootSignature;
	DescriptorUpdateFrequency updateFreq = (DescriptorUpdateFrequency)pDescriptorSet->mUpdateFrequency;
	DescriptorUpdateData* pUpdateData = pDescriptorSet->ppUpdateData[index];
	bool update = false;

#ifdef ENABLE_RAYTRACING
	VkWriteDescriptorSet* raytracingWrites = NULL;
	VkWriteDescriptorSetAccelerationStructureNV* raytracingWritesNV = NULL;
	uint32_t raytracingWriteCount = 0;

	if (pRootSignature->mVkRaytracingDescriptorCounts[updateFreq])
	{
		raytracingWrites = (VkWriteDescriptorSet*)alloca(pRootSignature->mVkRaytracingDescriptorCounts[updateFreq] * sizeof(VkWriteDescriptorSet));
		raytracingWritesNV = (VkWriteDescriptorSetAccelerationStructureNV*)alloca(pRootSignature->mVkRaytracingDescriptorCounts[updateFreq] * sizeof(VkWriteDescriptorSetAccelerationStructureNV));
	}
#endif

	for (uint32_t i = 0; i < count; ++i)
	{
		const DescriptorData* pParam = pParams + i;
		uint32_t paramIndex = pParam->mIndex;

		VALIDATE_DESCRIPTOR(pParam->pName || (paramIndex != -1), "DescriptorData has NULL name and invalid index");

		const DescriptorInfo* pDesc = (paramIndex != -1) ? (pRootSignature->pDescriptors + paramIndex) : get_descriptor(pRootSignature, pParam->pName);
		if (paramIndex != -1)
		{
			VALIDATE_DESCRIPTOR(pDesc, "Invalid descriptor with param index (%u)", paramIndex);
		}
		else
		{
			VALIDATE_DESCRIPTOR(pDesc, "Invalid descriptor with param name (%s)", pParam->pName);
		}

		const DescriptorType type = pDesc->mDesc.type;
		const uint32_t arrayCount = max(1U, pParam->mCount);

		VALIDATE_DESCRIPTOR(pDesc->mDesc.set == updateFreq,
			"Descriptor (%s) - Mismatching update frequency and set index", pDesc->mDesc.name);

		switch (type)
		{
		case DESCRIPTOR_TYPE_SAMPLER:
		{
			// Index is invalid when descriptor is a static sampler
			VALIDATE_DESCRIPTOR(pDesc->mIndexInParent != -1,
				"Trying to update a static sampler (%s). All static samplers must be set in addRootSignature and cannot be updated later",
				pDesc->mDesc.name);

			VALIDATE_DESCRIPTOR(pParam->ppSamplers, "NULL Sampler (%s)", pDesc->mDesc.name);

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppSamplers[arr], "NULL Sampler (%s [%u] )", pDesc->mDesc.name, arr);

				pUpdateData[pDesc->mHandleIndex + arr].mImageInfo = { pParam->ppSamplers[arr]->pVkSampler, VK_NULL_HANDLE };
				update = true;
			}
			break;
		}
		case DESCRIPTOR_TYPE_TEXTURE:
		{
			VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL Texture (%s)", pDesc->mDesc.name);

			if (!pParam->mBindStencilResource)
			{
				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", pDesc->mDesc.name, arr);

					pUpdateData[pDesc->mHandleIndex + arr].mImageInfo =
					{
						VK_NULL_HANDLE,                                    // Sampler
						pParam->ppTextures[arr]->pVkSRVDescriptor,         // Image View
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL           // Image Layout
					};

					update = true;
				}
			}
			else
			{
				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", pDesc->mDesc.name, arr);

					pUpdateData[pDesc->mHandleIndex + arr].mImageInfo =
					{
						VK_NULL_HANDLE,                                    // Sampler
						*pParam->ppTextures[arr]->pVkSRVStencilDescriptor, // Image View
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL           // Image Layout
					};

					update = true;
				}
			}
			break;
		}
		case DESCRIPTOR_TYPE_RW_TEXTURE:
		{
			VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL RW Texture (%s)", pDesc->mDesc.name);
			const uint32_t mipSlice = pParam->mUAVMipSlice;

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL RW Texture (%s [%u] )", pDesc->mDesc.name, arr);
				VALIDATE_DESCRIPTOR(mipSlice < pParam->ppTextures[arr]->mDesc.mMipLevels, "Descriptor : (%s [%u] ) Mip Slice (%u) exceeds mip levels (%u)",
					pDesc->mDesc.name, arr, mipSlice, pParam->ppTextures[arr]->mDesc.mMipLevels);

				pUpdateData[pDesc->mHandleIndex + arr].mImageInfo =
				{
					VK_NULL_HANDLE,                                        // Sampler
					pParam->ppTextures[arr]->pVkUAVDescriptors[mipSlice],  // Image View
					VK_IMAGE_LAYOUT_GENERAL                                // Image Layout
				};

				update = true;
			}
			break;
		}
		case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		{
			if (pDesc->mVkType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Uniform Buffer (%s)", pDesc->mDesc.name);
				VALIDATE_DESCRIPTOR(pParam->ppBuffers[0], "NULL Uniform Buffer (%s [%u] )", pDesc->mDesc.name, 0);
				VALIDATE_DESCRIPTOR(arrayCount == 1, "Descriptor (%s) : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC does not support arrays", pDesc->mDesc.name);
				VALIDATE_DESCRIPTOR(pParam->pSizes, "Descriptor (%s) : Must provide pSizes for VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC", pDesc->mDesc.name);
				VALIDATE_DESCRIPTOR(pParam->pSizes[0] > 0, "Descriptor (%s) - pSizes[%u] is zero", pDesc->mDesc.name, 0);
				VALIDATE_DESCRIPTOR(pParam->pSizes[0] <= pRenderer->pVkActiveGPUProperties->properties.limits.maxUniformBufferRange,
					"Descriptor (%s) - pSizes[%u] is %ull which exceeds max size %u", pDesc->mDesc.name, 0,
					pParam->pSizes[0],
					pRenderer->pVkActiveGPUProperties->properties.limits.maxUniformBufferRange);

				pDescriptorSet->pDynamicOffsets[index][pDesc->mDynamicUniformIndex] = pParam->pOffsets ? (uint32_t)pParam->pOffsets[0] : 0;
				pUpdateData[pDesc->mHandleIndex + 0].mBufferInfo = pParam->ppBuffers[0]->mVkBufferInfo;
				pUpdateData[pDesc->mHandleIndex + 0].mBufferInfo.range = pParam->pSizes[0];

				// If this is a different size we have to update the VkDescriptorBufferInfo::range so a call to vkUpdateDescriptorSet is necessary
				if (pParam->pSizes[0] != (uint32_t)pDescriptorSet->pDynamicSizes[index][pDesc->mDynamicUniformIndex])
				{
					pDescriptorSet->pDynamicSizes[index][pDesc->mDynamicUniformIndex] = (uint32_t)pParam->pSizes[0];
					update = true;
				}

				break;
			}
		case DESCRIPTOR_TYPE_BUFFER:
		case DESCRIPTOR_TYPE_BUFFER_RAW:
		case DESCRIPTOR_TYPE_RW_BUFFER:
		case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
		{
			VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Buffer (%s)", pDesc->mDesc.name);

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Buffer (%s [%u] )", pDesc->mDesc.name, arr);

				pUpdateData[pDesc->mHandleIndex + arr].mBufferInfo = pParam->ppBuffers[arr]->mVkBufferInfo;
				if (pParam->pOffsets)
				{
					VALIDATE_DESCRIPTOR(pParam->pSizes, "Descriptor (%s) - pSizes must be provided with pOffsets", pDesc->mDesc.name);
					VALIDATE_DESCRIPTOR(pParam->pSizes[arr] > 0, "Descriptor (%s) - pSizes[%u] is zero", pDesc->mDesc.name, arr);
					VALIDATE_DESCRIPTOR(pParam->pSizes[arr] <= pRenderer->pVkActiveGPUProperties->properties.limits.maxUniformBufferRange,
						"Descriptor (%s) - pSizes[%u] is %ull which exceeds max size %u", pDesc->mDesc.name, arr,
						pParam->pSizes[arr],
						pRenderer->pVkActiveGPUProperties->properties.limits.maxUniformBufferRange);

					pUpdateData[pDesc->mHandleIndex + arr].mBufferInfo.offset = pParam->pOffsets[arr];
					pUpdateData[pDesc->mHandleIndex + arr].mBufferInfo.range = pParam->pSizes[arr];
				}

				update = true;
			}

		}
			break;
		}
		case DESCRIPTOR_TYPE_TEXEL_BUFFER:
		{
			VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Texel Buffer (%s)", pDesc->mDesc.name);

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Texel Buffer (%s [%u] )", pDesc->mDesc.name, arr);
				pUpdateData[pDesc->mHandleIndex + arr].mBuferView = pParam->ppBuffers[arr]->pVkUniformTexelView;
				update = true;
			}

			break;
		}
		case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER:
		{
			VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL RW Texel Buffer (%s)", pDesc->mDesc.name);

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL RW Texel Buffer (%s [%u] )", pDesc->mDesc.name, arr);
				pUpdateData[pDesc->mHandleIndex + arr].mBuferView = pParam->ppBuffers[arr]->pVkStorageTexelView;
				update = true;
			}

			break;
		}
#ifdef ENABLE_RAYTRACING
		case DESCRIPTOR_TYPE_RAY_TRACING:
		{
			VALIDATE_DESCRIPTOR(pParam->ppAccelerationStructures, "NULL Acceleration Structure (%s)", pDesc->mDesc.name);

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppAccelerationStructures[arr], "Acceleration Structure (%s [%u] )", pDesc->mDesc.name, arr);

				VkWriteDescriptorSet* pWrite = raytracingWrites + raytracingWriteCount;
				VkWriteDescriptorSetAccelerationStructureNV* pWriteNV = raytracingWritesNV + raytracingWriteCount;

				pWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				pWrite->pNext = pWriteNV;
				pWrite->dstSet = pDescriptorSet->pHandles[index];
				pWrite->descriptorCount = 1;
				pWrite->descriptorType = pDesc->mVkType;
				pWrite->dstArrayElement = arr;
				pWrite->dstBinding = pDesc->mDesc.reg;

				vk_FillRaytracingDescriptorData(pParam->ppAccelerationStructures[arr], pWriteNV);

				++raytracingWriteCount;
			}
			break;
		}
#endif
		default:
			break;
		}
	}

	// If this was called to just update a dynamic offset skip the update
	if (update)
		vkUpdateDescriptorSetWithTemplateKHR(pRenderer->pVkDevice, pDescriptorSet->pHandles[index],
			pRootSignature->mUpdateTemplates[updateFreq], pUpdateData);

#ifdef ENABLE_RAYTRACING
	// Raytracing Update Descriptor Set since it does not support update template
	if (raytracingWriteCount)
		vkUpdateDescriptorSets(pRenderer->pVkDevice, raytracingWriteCount, raytracingWrites, 0, NULL);
#endif
}

void cmdBindDescriptorSet(Cmd* pCmd, uint32_t index, DescriptorSet* pDescriptorSet)
{
	ASSERT(pCmd);
	ASSERT(pDescriptorSet);
	ASSERT(pDescriptorSet->pHandles);
	ASSERT(index < pDescriptorSet->mMaxSets);

	const RootSignature* pRootSignature = pDescriptorSet->pRootSignature;

	if (pCmd->pBoundRootSignature != pRootSignature)
	{
		pCmd->pBoundRootSignature = pRootSignature;

		// Vulkan requires to bind all descriptor sets upto the highest set number even if they are empty
		// Example: If shader uses only set 2, we still have to bind empty sets for set=0 and set=1
		for (uint32_t setIndex = 0; setIndex < DESCRIPTOR_UPDATE_FREQ_COUNT; ++setIndex)
		{
			if (pRootSignature->mVkEmptyDescriptorSets[setIndex] != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					pCmd->pVkCmdBuf, gPipelineBindPoint[pRootSignature->mPipelineType], pRootSignature->pPipelineLayout, setIndex, 1,
					&pRootSignature->mVkEmptyDescriptorSets[setIndex], 0, NULL);
			}
		}
	}

	vkCmdBindDescriptorSets(pCmd->pVkCmdBuf, gPipelineBindPoint[pRootSignature->mPipelineType],
		pRootSignature->pPipelineLayout, pDescriptorSet->mUpdateFrequency, 1, &pDescriptorSet->pHandles[index],
		pDescriptorSet->mDynamicOffsetCount, pDescriptorSet->mDynamicOffsetCount ? pDescriptorSet->pDynamicOffsets[index] : NULL);
}

void cmdBindPushConstants(Cmd* pCmd, RootSignature* pRootSignature, const char* pName, const void* pConstants)
{
	ASSERT(pCmd);
	ASSERT(pConstants);
	ASSERT(pRootSignature);
	ASSERT(pName);
	
	const DescriptorInfo* pDesc = get_descriptor(pRootSignature, pName);
	ASSERT(pDesc);
	ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == pDesc->mDesc.type);

	vkCmdPushConstants(pCmd->pVkCmdBuf, pRootSignature->pPipelineLayout,
		pDesc->mVkStages, 0, pDesc->mDesc.size, pConstants);
}

void cmdBindPushConstantsByIndex(Cmd* pCmd, RootSignature* pRootSignature, uint32_t paramIndex, const void* pConstants)
{
	ASSERT(pCmd);
	ASSERT(pConstants);
	ASSERT(pRootSignature);
	ASSERT(paramIndex >= 0 && paramIndex < pRootSignature->mDescriptorCount);

	const DescriptorInfo* pDesc = pRootSignature->pDescriptors + paramIndex;
	ASSERT(pDesc);
	ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == pDesc->mDesc.type);

	vkCmdPushConstants(pCmd->pVkCmdBuf, pRootSignature->pPipelineLayout,
		pDesc->mVkStages, 0, pDesc->mDesc.size, pConstants);
}
/************************************************************************/
// Shader Functions
/************************************************************************/
void addShaderBinary(Renderer* pRenderer, const BinaryShaderDesc* pDesc, Shader** ppShaderProgram)
{
	Shader* pShaderProgram = (Shader*)conf_calloc(1, sizeof(*pShaderProgram));

	conf_placement_new<Shader>(pShaderProgram);

	pShaderProgram->mStages = pDesc->mStages;

	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	uint32_t                        counter = 0;
	ShaderReflection                stageReflections[SHADER_STAGE_COUNT] = {};
	VkShaderModule                  modules[SHADER_STAGE_COUNT] = {};
	char*                           entryNames[SHADER_STAGE_COUNT] = {};

	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		ShaderStage stage_mask = (ShaderStage)(1 << i);
		if (stage_mask == (pShaderProgram->mStages & stage_mask))
		{
			DECLARE_ZERO(VkShaderModuleCreateInfo, create_info);
			create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			create_info.pNext = NULL;
			create_info.flags = 0;

			const BinaryShaderStageDesc* pStageDesc = nullptr;
			switch (stage_mask)
			{
				case SHADER_STAGE_VERT:
				{
					vk_createShaderReflection(
						(const uint8_t*)pDesc->mVert.pByteCode, (uint32_t)pDesc->mVert.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					create_info.codeSize = pDesc->mVert.mByteCodeSize;
					create_info.pCode = (const uint32_t*)pDesc->mVert.pByteCode;
					pStageDesc = &pDesc->mVert;
					VkResult vk_res = vkCreateShaderModule(pRenderer->pVkDevice, &create_info, NULL, &(modules[counter]));
					ASSERT(VK_SUCCESS == vk_res);
				}
				break;
				case SHADER_STAGE_TESC:
				{
					vk_createShaderReflection(
						(const uint8_t*)pDesc->mHull.pByteCode, (uint32_t)pDesc->mHull.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					create_info.codeSize = pDesc->mHull.mByteCodeSize;
					create_info.pCode = (const uint32_t*)pDesc->mHull.pByteCode;
					pStageDesc = &pDesc->mHull;
					VkResult vk_res = vkCreateShaderModule(pRenderer->pVkDevice, &create_info, NULL, &(modules[counter]));
					ASSERT(VK_SUCCESS == vk_res);
				}
				break;
				case SHADER_STAGE_TESE:
				{
					vk_createShaderReflection(
						(const uint8_t*)pDesc->mDomain.pByteCode, (uint32_t)pDesc->mDomain.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					create_info.codeSize = pDesc->mDomain.mByteCodeSize;
					create_info.pCode = (const uint32_t*)pDesc->mDomain.pByteCode;
					pStageDesc = &pDesc->mDomain;
					VkResult vk_res = vkCreateShaderModule(pRenderer->pVkDevice, &create_info, NULL, &(modules[counter]));
					ASSERT(VK_SUCCESS == vk_res);
				}
				break;
				case SHADER_STAGE_GEOM:
				{
					vk_createShaderReflection(
						(const uint8_t*)pDesc->mGeom.pByteCode, (uint32_t)pDesc->mGeom.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					create_info.codeSize = pDesc->mGeom.mByteCodeSize;
					create_info.pCode = (const uint32_t*)pDesc->mGeom.pByteCode;
					pStageDesc = &pDesc->mGeom;
					VkResult vk_res = vkCreateShaderModule(pRenderer->pVkDevice, &create_info, NULL, &(modules[counter]));
					ASSERT(VK_SUCCESS == vk_res);
				}
				break;
				case SHADER_STAGE_FRAG:
				{
					vk_createShaderReflection(
						(const uint8_t*)pDesc->mFrag.pByteCode, (uint32_t)pDesc->mFrag.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					create_info.codeSize = pDesc->mFrag.mByteCodeSize;
					create_info.pCode = (const uint32_t*)pDesc->mFrag.pByteCode;
					pStageDesc = &pDesc->mFrag;
					VkResult vk_res = vkCreateShaderModule(pRenderer->pVkDevice, &create_info, NULL, &(modules[counter]));
					ASSERT(VK_SUCCESS == vk_res);
				}
				break;
				case SHADER_STAGE_COMP:
#ifdef ENABLE_RAYTRACING
				case SHADER_STAGE_RAYTRACING:
#endif
				{
					vk_createShaderReflection(
						(const uint8_t*)pDesc->mComp.pByteCode, (uint32_t)pDesc->mComp.mByteCodeSize, stage_mask,
						&stageReflections[counter]);

					create_info.codeSize = pDesc->mComp.mByteCodeSize;
					create_info.pCode = (const uint32_t*)pDesc->mComp.pByteCode;
					pStageDesc = &pDesc->mComp;
					VkResult vk_res = vkCreateShaderModule(pRenderer->pVkDevice, &create_info, NULL, &(modules[counter]));
					ASSERT(VK_SUCCESS == vk_res);
				}
				break;
				default: ASSERT(false && "Shader Stage not supported!"); break;
			}

			entryNames[counter] = (char*)conf_calloc(strlen(pStageDesc->pEntryPoint) + 1, sizeof(char));
			memcpy(entryNames[counter], pStageDesc->pEntryPoint, strlen(pStageDesc->pEntryPoint));
			++counter;
		}
	}

	pShaderProgram->pEntryNames = (char**)conf_calloc(counter, sizeof(char*));
	memcpy(pShaderProgram->pEntryNames, entryNames, counter * sizeof(char*));

	pShaderProgram->pShaderModules = (VkShaderModule*)conf_calloc(counter, sizeof(VkShaderModule));
	memcpy(pShaderProgram->pShaderModules, modules, counter * sizeof(VkShaderModule));

	createPipelineReflection(stageReflections, counter, &pShaderProgram->mReflection);

	*ppShaderProgram = pShaderProgram;
}

void removeShader(Renderer* pRenderer, Shader* pShaderProgram)
{
	ASSERT(pRenderer);

	for (uint32_t i = 0; i < pShaderProgram->mReflection.mStageReflectionCount; ++i)
		SAFE_FREE((void*)pShaderProgram->pEntryNames[i]);

	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);

	if (pShaderProgram->mStages & SHADER_STAGE_VERT)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[pShaderProgram->mReflection.mVertexStageIndex], NULL);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_TESC)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[pShaderProgram->mReflection.mHullStageIndex], NULL);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_TESE)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[pShaderProgram->mReflection.mDomainStageIndex], NULL);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_GEOM)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[pShaderProgram->mReflection.mGeometryStageIndex], NULL);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_FRAG)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[pShaderProgram->mReflection.mPixelStageIndex], NULL);
	}

	if (pShaderProgram->mStages & SHADER_STAGE_COMP)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[0], NULL);
	}
#ifdef ENABLE_RAYTRACING
	if (pShaderProgram->mStages & SHADER_STAGE_RAYTRACING)
	{
		vkDestroyShaderModule(pRenderer->pVkDevice, pShaderProgram->pShaderModules[0], NULL);
	}
#endif

	destroyPipelineReflection(&pShaderProgram->mReflection);
	pShaderProgram->~Shader();
	SAFE_FREE(pShaderProgram->pEntryNames);
	SAFE_FREE(pShaderProgram->pShaderModules);
	SAFE_FREE(pShaderProgram);
}
/************************************************************************/
// Root Signature Functions
/************************************************************************/
typedef struct UpdateFrequencyLayoutInfo
{
	/// Array of all bindings in the descriptor set
	eastl::vector<VkDescriptorSetLayoutBinding> mBindings;
	/// Array of all descriptors in this descriptor set
	eastl::vector<DescriptorInfo*> mDescriptors;
	/// Array of all descriptors marked as dynamic in this descriptor set (applicable to DESCRIPTOR_TYPE_UNIFORM_BUFFER)
	eastl::vector<DescriptorInfo*> mDynamicDescriptors;
	/// Hash map to get index of the descriptor in the root signature
	eastl::hash_map<DescriptorInfo*, uint32_t> mDescriptorIndexMap;
} UpdateFrequencyLayoutInfo;

void addRootSignature(Renderer* pRenderer, const RootSignatureDesc* pRootSignatureDesc, RootSignature** ppRootSignature)
{
	RootSignature* pRootSignature = (RootSignature*)conf_calloc(1, sizeof(*pRootSignature));
	ASSERT(pRootSignature);

	pRootSignature->pDescriptorNameToIndexMap = conf_new(DescriptorIndexMap);
	ASSERT(pRootSignature->pDescriptorNameToIndexMap);

	eastl::vector<UpdateFrequencyLayoutInfo> layouts(DESCRIPTOR_UPDATE_FREQ_COUNT);
	eastl::vector<DescriptorInfo*>           pushConstantDescriptors;
	eastl::vector<ShaderResource>            shaderResources;
	eastl::hash_map<eastl::string, Sampler*> staticSamplerMap;

	for (uint32_t i = 0; i < pRootSignatureDesc->mStaticSamplerCount; ++i)
	{
		ASSERT(pRootSignatureDesc->ppStaticSamplers[i]);
		staticSamplerMap.insert({ { pRootSignatureDesc->ppStaticSamplerNames[i], pRootSignatureDesc->ppStaticSamplers[i] } });
	}

	// Collect all unique shader resources in the given shaders
	// Resources are parsed by name (two resources named "XYZ" in two shaders will be considered the same resource)
	for (uint32_t sh = 0; sh < pRootSignatureDesc->mShaderCount; ++sh)
	{
		PipelineReflection const* pReflection = &pRootSignatureDesc->ppShaders[sh]->mReflection;

		if (pReflection->mShaderStages & SHADER_STAGE_COMP)
			pRootSignature->mPipelineType = PIPELINE_TYPE_COMPUTE;
#ifdef ENABLE_RAYTRACING
		else if (pReflection->mShaderStages & SHADER_STAGE_RAYTRACING)
			pRootSignature->mPipelineType = PIPELINE_TYPE_RAYTRACING;
#endif
		else
			pRootSignature->mPipelineType = PIPELINE_TYPE_GRAPHICS;

		for (uint32_t i = 0; i < pReflection->mShaderResourceCount; ++i)
		{
			ShaderResource const* pRes = &pReflection->pShaderResources[i];
			uint32_t              setIndex = pRes->set;

			if (pRes->type == DESCRIPTOR_TYPE_ROOT_CONSTANT)
				setIndex = 0;

			eastl::string_hash_map<uint32_t>::iterator it =
				pRootSignature->pDescriptorNameToIndexMap->mMap.find(pRes->name);
			if (it == pRootSignature->pDescriptorNameToIndexMap->mMap.end())
			{
				pRootSignature->pDescriptorNameToIndexMap->mMap.insert(pRes->name, (uint32_t)shaderResources.size());
				shaderResources.emplace_back(*pRes);
			}
			else
			{
				if (shaderResources[it->second].reg != pRes->reg)
				{
					LOGF( LogLevel::eERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching binding. All shader resources "
						"shared by multiple shaders specified in addRootSignature "
						"must have the same binding and set",
						pRes->name);
					return;
				}
				if (shaderResources[it->second].set != pRes->set)
				{
					LOGF( LogLevel::eERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching set. All shader resources "
						"shared by multiple shaders specified in addRootSignature "
						"must have the same binding and set",
						pRes->name);
					return;
				}

				for (ShaderResource& res : shaderResources)
				{
					if (strcmp(res.name, it->first) == 0)
					{
						res.used_stages |= pRes->used_stages;
						break;
					}
				}
			}
		}
	}

	if ((uint32_t)shaderResources.size())
	{
		pRootSignature->mDescriptorCount = (uint32_t)shaderResources.size();
		pRootSignature->pDescriptors = (DescriptorInfo*)conf_calloc(pRootSignature->mDescriptorCount, sizeof(DescriptorInfo));
	}

	// Fill the descriptor array to be stored in the root signature
	for (uint32_t i = 0; i < (uint32_t)shaderResources.size(); ++i)
	{
		DescriptorInfo*           pDesc = &pRootSignature->pDescriptors[i];
		ShaderResource const*     pRes = &shaderResources[i];
		uint32_t                  setIndex = pRes->set;
		DescriptorUpdateFrequency updateFreq = (DescriptorUpdateFrequency)setIndex;

		// Copy the binding information generated from the shader reflection into the descriptor
		pDesc->mDesc.reg = pRes->reg;
		pDesc->mDesc.set = pRes->set;
		pDesc->mDesc.size = pRes->size;
		pDesc->mDesc.type = pRes->type;
		pDesc->mDesc.used_stages = pRes->used_stages;
		pDesc->mDesc.name_size = pRes->name_size;
		pDesc->mDesc.name = (const char*)conf_calloc(pDesc->mDesc.name_size + 1, sizeof(char));
		pDesc->mDesc.dim = pRes->dim;
		memcpy((char*)pDesc->mDesc.name, pRes->name, pRes->name_size);

		// If descriptor is not a root constant create a new layout binding for this descriptor and add it to the binding array
		if (pDesc->mDesc.type != DESCRIPTOR_TYPE_ROOT_CONSTANT)
		{
			VkDescriptorSetLayoutBinding binding = {};
			binding.binding = pDesc->mDesc.reg;
			binding.descriptorCount = pDesc->mDesc.size;
			binding.descriptorType = util_to_vk_descriptor_type(pDesc->mDesc.type);

			eastl::string name = pRes->name;
			name.make_lower();

			// If a user specified a uniform buffer to be used as a dynamic uniform buffer change its type to VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
			// Also log a message for debugging purpose
			if (name.find("rootcbv") != eastl::string::npos)
			{
				if (pDesc->mDesc.size == 1)
				{
					LOGF(LogLevel::eINFO, "Descriptor (%s) : User specified VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC", pDesc->mDesc.name);
					binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
				}
				else
				{
					LOGF(
						LogLevel::eWARNING, "Descriptor (%s) : Cannot use VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC for arrays",
						pDesc->mDesc.name);
				}
			}

			binding.stageFlags = util_to_vk_shader_stage_flags(pDesc->mDesc.used_stages);

			// Store the vulkan related info in the descriptor to avoid constantly calling the util_to_vk mapping functions
			pDesc->mVkType = binding.descriptorType;
			pDesc->mVkStages = binding.stageFlags;
			pDesc->mUpdateFrquency = updateFreq;

			if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			{
				layouts[setIndex].mDynamicDescriptors.emplace_back(pDesc);
			}

			// Find if the given descriptor is a static sampler
			decltype(staticSamplerMap)::iterator it = staticSamplerMap.find(pDesc->mDesc.name);
			if (it != staticSamplerMap.end())
			{
				LOGF(LogLevel::eINFO, "Descriptor (%s) : User specified Static Sampler", pDesc->mDesc.name);

				// Set the index to an invalid value so we can use this later for error checking if user tries to update a static sampler
				pDesc->mIndexInParent = -1;
				binding.pImmutableSamplers = &it->second->pVkSampler;
			}
			else
			{
				layouts[setIndex].mDescriptors.emplace_back(pDesc);
			}

			layouts[setIndex].mBindings.push_back(binding);
		}
		// If descriptor is a root constant, add it to the root constant array
		else
		{
			LOGF(LogLevel::eINFO, "Descriptor (%s) : User specified Push Constant", pDesc->mDesc.name);

			pDesc->mDesc.set = 0;
			pDesc->mVkStages = util_to_vk_shader_stage_flags(pDesc->mDesc.used_stages);
			setIndex = 0;
			pushConstantDescriptors.emplace_back(pDesc);
		}

		layouts[setIndex].mDescriptorIndexMap[pDesc] = i;
	}

	pRootSignature->mVkPushConstantCount = (uint32_t)pushConstantDescriptors.size();
	if (pRootSignature->mVkPushConstantCount)
		pRootSignature->pVkPushConstantRanges =
			(VkPushConstantRange*)conf_calloc(pRootSignature->mVkPushConstantCount, sizeof(*pRootSignature->pVkPushConstantRanges));

	// Create push constant ranges
	for (uint32_t i = 0; i < pRootSignature->mVkPushConstantCount; ++i)
	{
		VkPushConstantRange* pConst = &pRootSignature->pVkPushConstantRanges[i];
		DescriptorInfo*      pDesc = pushConstantDescriptors[i];
		pDesc->mIndexInParent = i;
		pConst->offset = 0;
		pConst->size = pDesc->mDesc.size;
		pConst->stageFlags = util_to_vk_shader_stage_flags(pDesc->mDesc.used_stages);
	}

	// Create descriptor layouts
	// Put most frequently changed params first
	for (uint32_t i = (uint32_t)layouts.size(); i-- > 0U;)
	{
		UpdateFrequencyLayoutInfo& layout = layouts[i];

		if (layouts[i].mBindings.size())
		{
			// sort table by type (CBV/SRV/UAV) by register
			eastl::stable_sort(layout.mBindings.begin(), layout.mBindings.end(), [](const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs) {
				return lhs.binding > rhs.binding;
			});
			eastl::stable_sort(layout.mBindings.begin(), layout.mBindings.end(), [](const VkDescriptorSetLayoutBinding& lhs, const VkDescriptorSetLayoutBinding& rhs) {
				return lhs.descriptorType > rhs.descriptorType;
			});
		}

		bool createLayout = layout.mBindings.size() > 0;
		// Check if we need to create an empty layout in case there is an empty set between two used sets
		// Example: set = 0 is used, set = 2 is used. In this case, set = 1 needs to exist even if it is empty
		if (!createLayout && i < layouts.size() - 1)
		{
			createLayout = pRootSignature->mVkDescriptorSetLayouts[i + 1] != VK_NULL_HANDLE;
		}

		if (createLayout)
		{
			VkDescriptorSetLayoutCreateInfo layoutInfo = {};
			layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			layoutInfo.pNext = NULL;
			layoutInfo.bindingCount = (uint32_t)layout.mBindings.size();
			layoutInfo.pBindings = layout.mBindings.data();
			layoutInfo.flags = 0;

			vkCreateDescriptorSetLayout(pRenderer->pVkDevice, &layoutInfo, NULL, &pRootSignature->mVkDescriptorSetLayouts[i]);
		}

		if (!layouts[i].mBindings.size())
			continue;

		pRootSignature->mVkDescriptorCounts[i] = (uint32_t)layout.mDescriptors.size();

		// Loop through descriptors belonging to this update frequency and increment the cumulative descriptor count
		for (uint32_t descIndex = 0; descIndex < (uint32_t)layout.mDescriptors.size(); ++descIndex)
		{
			DescriptorInfo* pDesc = layout.mDescriptors[descIndex];
			pDesc->mIndexInParent = descIndex;
			pDesc->mHandleIndex = pRootSignature->mVkCumulativeDescriptorCounts[i];
			pRootSignature->mVkCumulativeDescriptorCounts[i] += pDesc->mDesc.size;
		}

		eastl::sort(
			layout.mDynamicDescriptors.begin(), layout.mDynamicDescriptors.end(),
			[](DescriptorInfo* const lhs, DescriptorInfo* const rhs) { return lhs->mDesc.reg > rhs->mDesc.reg; });

		pRootSignature->mVkDynamicDescriptorCounts[i] = (uint32_t)layout.mDynamicDescriptors.size();
		for (uint32_t descIndex = 0; descIndex < pRootSignature->mVkDynamicDescriptorCounts[i]; ++descIndex)
		{
			DescriptorInfo* pDesc = layout.mDynamicDescriptors[descIndex];
			pDesc->mDynamicUniformIndex = descIndex;
		}
	}
	/************************************************************************/
	// Pipeline layout
	/************************************************************************/
	eastl::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	eastl::vector<VkPushConstantRange>   pushConstants(pRootSignature->mVkPushConstantCount);
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
		if (pRootSignature->mVkDescriptorSetLayouts[i])
			descriptorSetLayouts.emplace_back(pRootSignature->mVkDescriptorSetLayouts[i]);
	for (uint32_t i = 0; i < pRootSignature->mVkPushConstantCount; ++i)
		pushConstants[i] = pRootSignature->pVkPushConstantRanges[i];

	DECLARE_ZERO(VkPipelineLayoutCreateInfo, add_info);
	add_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	add_info.pNext = NULL;
	add_info.flags = 0;
	add_info.setLayoutCount = (uint32_t)descriptorSetLayouts.size();
	add_info.pSetLayouts = descriptorSetLayouts.data();
	add_info.pushConstantRangeCount = pRootSignature->mVkPushConstantCount;
	add_info.pPushConstantRanges = pushConstants.data();
	VkResult vk_res = vkCreatePipelineLayout(pRenderer->pVkDevice, &add_info, NULL, &(pRootSignature->pPipelineLayout));
	ASSERT(VK_SUCCESS == vk_res);
	/************************************************************************/
	// Update templates
	/************************************************************************/
	for (uint32_t setIndex = 0; setIndex < DESCRIPTOR_UPDATE_FREQ_COUNT; ++setIndex)
	{
		if (pRootSignature->mVkDescriptorCounts[setIndex])
		{
			const UpdateFrequencyLayoutInfo& layout = layouts[setIndex];
			VkDescriptorUpdateTemplateEntry* pEntries = (VkDescriptorUpdateTemplateEntry*)alloca(pRootSignature->mVkDescriptorCounts[setIndex] * sizeof(VkDescriptorUpdateTemplateEntry));
			uint32_t entryCount = 0;

			for (uint32_t nodeIndex = 0; nodeIndex < pRenderer->mLinkedNodeCount; ++nodeIndex)
			{
				pRootSignature->pUpdateTemplateData[setIndex][nodeIndex] = conf_calloc(pRootSignature->mVkCumulativeDescriptorCounts[setIndex], sizeof(DescriptorUpdateData));
			}

			// Fill the write descriptors with default values during initialize so the only thing we change in cmdBindDescriptors is the the VkBuffer / VkImageView objects
			for (uint32_t i = 0; i < (uint32_t)layout.mDescriptors.size(); ++i)
			{
				const DescriptorInfo* pDesc  = layout.mDescriptors[i];
				const uint64_t        offset = pDesc->mHandleIndex * sizeof(DescriptorUpdateData);

#ifdef VK_NV_RAY_TRACING_SPEC_VERSION
				// Raytracing descriptors dont support update template so we ignore them
				if (pDesc->mDesc.type == DESCRIPTOR_TYPE_RAY_TRACING)
				{
					pRootSignature->mVkRaytracingDescriptorCounts[setIndex] += pDesc->mDesc.size;
					continue;
				}
#endif

				pEntries[entryCount].descriptorCount = pDesc->mDesc.size;
				pEntries[entryCount].descriptorType = pDesc->mVkType;
				pEntries[entryCount].dstArrayElement = 0;
				pEntries[entryCount].dstBinding = pDesc->mDesc.reg;
				pEntries[entryCount].offset = offset;
				pEntries[entryCount].stride = sizeof(DescriptorUpdateData);

				for (uint32_t nodeIndex = 0; nodeIndex < pRenderer->mLinkedNodeCount; ++nodeIndex)
				{
					DescriptorUpdateData* pUpdateData = (DescriptorUpdateData*)pRootSignature->pUpdateTemplateData[setIndex][nodeIndex];

					const DescriptorType type = pDesc->mDesc.type;
					const uint32_t arrayCount = pDesc->mDesc.size;

					switch (type)
					{
					case DESCRIPTOR_TYPE_SAMPLER:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mImageInfo = { pRenderer->pDefaultSampler->pVkSampler, VK_NULL_HANDLE };
						break;
					}
					case DESCRIPTOR_TYPE_TEXTURE:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mImageInfo = { VK_NULL_HANDLE, pRenderer->pDefaultTextureSRV[nodeIndex][pDesc->mDesc.dim]->pVkSRVDescriptor, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
						break;
					}
					case DESCRIPTOR_TYPE_RW_TEXTURE:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mImageInfo = { VK_NULL_HANDLE, pRenderer->pDefaultTextureUAV[nodeIndex][pDesc->mDesc.dim]->pVkUAVDescriptors[0], VK_IMAGE_LAYOUT_GENERAL };
						break;
					}
					case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
					case DESCRIPTOR_TYPE_BUFFER:
					case DESCRIPTOR_TYPE_BUFFER_RAW:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mBufferInfo = pRenderer->pDefaultBufferSRV[nodeIndex]->mVkBufferInfo;
						break;
					}
					case DESCRIPTOR_TYPE_RW_BUFFER:
					case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mBufferInfo = pRenderer->pDefaultBufferUAV[nodeIndex]->mVkBufferInfo;
						break;
					}
					case DESCRIPTOR_TYPE_TEXEL_BUFFER:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mBuferView = pRenderer->pDefaultBufferSRV[nodeIndex]->pVkUniformTexelView;
						break;
					}
					case DESCRIPTOR_TYPE_RW_TEXEL_BUFFER:
					{
						for (uint32_t arr = 0; arr < arrayCount; ++arr)
							pUpdateData[pDesc->mHandleIndex + arr].mBuferView = pRenderer->pDefaultBufferUAV[nodeIndex]->pVkStorageTexelView;
						break;
					}
					default:
						break;
					}
				}

				++entryCount;
			}

			VkDescriptorUpdateTemplateCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO_KHR;
			createInfo.pNext = NULL;
			createInfo.descriptorSetLayout = pRootSignature->mVkDescriptorSetLayouts[setIndex];
			createInfo.descriptorUpdateEntryCount = entryCount;
			createInfo.pDescriptorUpdateEntries = pEntries;
			createInfo.pipelineBindPoint = gPipelineBindPoint[pRootSignature->mPipelineType];
			createInfo.pipelineLayout = pRootSignature->pPipelineLayout;
			createInfo.set = setIndex;
			createInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET_KHR;
			VkResult vkRes = vkCreateDescriptorUpdateTemplateKHR(pRenderer->pVkDevice, &createInfo, NULL, &pRootSignature->mUpdateTemplates[setIndex]);
			ASSERT(VK_SUCCESS == vkRes);
		}
		else if (VK_NULL_HANDLE != pRootSignature->mVkDescriptorSetLayouts[setIndex])
		{
			// Consume empty descriptor sets from empty descriptor set pool
			VkDescriptorSet* pSets[] = { &pRootSignature->mVkEmptyDescriptorSets[setIndex] };
			consume_descriptor_sets(pRenderer->pDescriptorPool, &pRootSignature->mVkDescriptorSetLayouts[setIndex], pSets, 1);
		}
	}

	*ppRootSignature = pRootSignature;
}

void removeRootSignature(Renderer* pRenderer, RootSignature* pRootSignature)
{
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		vkDestroyDescriptorSetLayout(pRenderer->pVkDevice, pRootSignature->mVkDescriptorSetLayouts[i], NULL);
		if (VK_NULL_HANDLE != pRootSignature->mUpdateTemplates[i])
			vkDestroyDescriptorUpdateTemplateKHR(pRenderer->pVkDevice, pRootSignature->mUpdateTemplates[i], NULL);

		for (uint32_t nodeIndex = 0; nodeIndex < pRenderer->mLinkedNodeCount; ++nodeIndex)
		{
			SAFE_FREE(pRootSignature->pUpdateTemplateData[i][nodeIndex]);
		}
	}

	for (uint32_t i = 0; i < pRootSignature->mDescriptorCount; ++i)
	{
		SAFE_FREE(pRootSignature->pDescriptors[i].mDesc.name);
	}

	// Need delete since the destructor frees allocated memory
	conf_delete(pRootSignature->pDescriptorNameToIndexMap);

	SAFE_FREE(pRootSignature->pDescriptors);
	SAFE_FREE(pRootSignature->pVkPushConstantRanges);

	vkDestroyPipelineLayout(pRenderer->pVkDevice, pRootSignature->pPipelineLayout, NULL);

	SAFE_FREE(pRootSignature);
}
/************************************************************************/
// Pipeline State Functions
/************************************************************************/
static void addGraphicsPipelineImpl(Renderer* pRenderer, const GraphicsPipelineDesc* pDesc, Pipeline** ppPipeline)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(pDesc->pShaderProgram);
	ASSERT(pDesc->pRootSignature);

	Pipeline* pPipeline = (Pipeline*)conf_calloc(1, sizeof(*pPipeline));
	ASSERT(pPipeline);

	conf_placement_new<Pipeline>(pPipeline);

	const Shader*       pShaderProgram = pDesc->pShaderProgram;
	const VertexLayout* pVertexLayout = pDesc->pVertexLayout;

	memcpy(&(pPipeline->mGraphics), pDesc, sizeof(*pDesc));
	pPipeline->mType = PIPELINE_TYPE_GRAPHICS;

	// Create tempporary renderpass for pipeline creation
	RenderPassDesc renderPassDesc = { 0 };
	RenderPass*    pRenderPass = NULL;
	renderPassDesc.mRenderTargetCount = pDesc->mRenderTargetCount;
	renderPassDesc.pColorFormats = pDesc->pColorFormats;
	renderPassDesc.mSampleCount = pDesc->mSampleCount;
	renderPassDesc.mDepthStencilFormat = pDesc->mDepthStencilFormat;
	add_render_pass(pRenderer, &renderPassDesc, &pRenderPass);

	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	for (uint32_t i = 0; i < pShaderProgram->mReflection.mStageReflectionCount; ++i)
		ASSERT(VK_NULL_HANDLE != pShaderProgram->pShaderModules[i]);

	// Pipeline
	{
		uint32_t stage_count = 0;
		DECLARE_ZERO(VkPipelineShaderStageCreateInfo, stages[5]);
		for (uint32_t i = 0; i < 5; ++i)
		{
			ShaderStage stage_mask = (ShaderStage)(1 << i);
			if (stage_mask == (pShaderProgram->mStages & stage_mask))
			{
				stages[stage_count].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
				stages[stage_count].pNext = NULL;
				stages[stage_count].flags = 0;
				stages[stage_count].pSpecializationInfo = NULL;
				switch (stage_mask)
				{
					case SHADER_STAGE_VERT:
					{
						stages[stage_count].pName =
							pShaderProgram->mReflection.mStageReflections[pShaderProgram->mReflection.mVertexStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_VERTEX_BIT;
						stages[stage_count].module = pShaderProgram->pShaderModules[pShaderProgram->mReflection.mVertexStageIndex];
					}
					break;
					case SHADER_STAGE_TESC:
					{
						stages[stage_count].pName =
							pShaderProgram->mReflection.mStageReflections[pShaderProgram->mReflection.mHullStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
						stages[stage_count].module = pShaderProgram->pShaderModules[pShaderProgram->mReflection.mHullStageIndex];
					}
					break;
					case SHADER_STAGE_TESE:
					{
						stages[stage_count].pName =
							pShaderProgram->mReflection.mStageReflections[pShaderProgram->mReflection.mDomainStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
						stages[stage_count].module = pShaderProgram->pShaderModules[pShaderProgram->mReflection.mDomainStageIndex];
					}
					break;
					case SHADER_STAGE_GEOM:
					{
						stages[stage_count].pName =
							pShaderProgram->mReflection.mStageReflections[pShaderProgram->mReflection.mGeometryStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
						stages[stage_count].module = pShaderProgram->pShaderModules[pShaderProgram->mReflection.mGeometryStageIndex];
					}
					break;
					case SHADER_STAGE_FRAG:
					{
						stages[stage_count].pName =
							pShaderProgram->mReflection.mStageReflections[pShaderProgram->mReflection.mPixelStageIndex].pEntryPoint;
						stages[stage_count].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
						stages[stage_count].module = pShaderProgram->pShaderModules[pShaderProgram->mReflection.mPixelStageIndex];
					}
					break;
					default: ASSERT(false && "Shader Stage not supported!"); break;
				}
				++stage_count;
			}
		}

		// Make sure there's a shader
		ASSERT(0 != stage_count);

		uint32_t                          input_binding_count = 0;
		VkVertexInputBindingDescription   input_bindings[MAX_VERTEX_BINDINGS] = { { 0 } };
		uint32_t                          input_attribute_count = 0;
		VkVertexInputAttributeDescription input_attributes[MAX_VERTEX_ATTRIBS] = { { 0 } };

		// Make sure there's attributes
		if (pVertexLayout != NULL)
		{
			// Ignore everything that's beyond max_vertex_attribs
			uint32_t attrib_count = pVertexLayout->mAttribCount > MAX_VERTEX_ATTRIBS ? MAX_VERTEX_ATTRIBS : pVertexLayout->mAttribCount;
			uint32_t binding_value = UINT32_MAX;

			// Initial values
			for (uint32_t i = 0; i < attrib_count; ++i)
			{
				const VertexAttrib* attrib = &(pVertexLayout->mAttribs[i]);

				if (binding_value != attrib->mBinding)
				{
					binding_value = attrib->mBinding;
					++input_binding_count;
				}

				input_bindings[input_binding_count - 1].binding = binding_value;
				if (attrib->mRate == VERTEX_ATTRIB_RATE_INSTANCE)
				{
					input_bindings[input_binding_count - 1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
				}
				else
				{
					input_bindings[input_binding_count - 1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
				}
				input_bindings[input_binding_count - 1].stride += TinyImageFormat_BitSizeOfBlock(attrib->mFormat) / 8;

				input_attributes[input_attribute_count].location = attrib->mLocation;
				input_attributes[input_attribute_count].binding = attrib->mBinding;
				input_attributes[input_attribute_count].format = (VkFormat)TinyImageFormat_ToVkFormat(attrib->mFormat);
				input_attributes[input_attribute_count].offset = attrib->mOffset;
				++input_attribute_count;
			}
		}

		DECLARE_ZERO(VkPipelineVertexInputStateCreateInfo, vi);
		vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vi.pNext = NULL;
		vi.flags = 0;
		vi.vertexBindingDescriptionCount = input_binding_count;
		vi.pVertexBindingDescriptions = input_bindings;
		vi.vertexAttributeDescriptionCount = input_attribute_count;
		vi.pVertexAttributeDescriptions = input_attributes;

		VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		switch (pDesc->mPrimitiveTopo)
		{
			case PRIMITIVE_TOPO_POINT_LIST: topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
			case PRIMITIVE_TOPO_LINE_LIST: topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
			case PRIMITIVE_TOPO_LINE_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
			case PRIMITIVE_TOPO_TRI_STRIP: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
			case PRIMITIVE_TOPO_PATCH_LIST: topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST; break;
			case PRIMITIVE_TOPO_TRI_LIST: topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
			default: ASSERT(false && "Primitive Topo not supported!"); break;
		}
		DECLARE_ZERO(VkPipelineInputAssemblyStateCreateInfo, ia);
		ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		ia.pNext = NULL;
		ia.flags = 0;
		ia.topology = topology;
		ia.primitiveRestartEnable = VK_FALSE;

		DECLARE_ZERO(VkPipelineTessellationStateCreateInfo, ts);
		if ((pShaderProgram->mStages & SHADER_STAGE_TESC) && (pShaderProgram->mStages & SHADER_STAGE_TESE))
		{
			ts.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
			ts.pNext = NULL;
			ts.flags = 0;
			ts.patchControlPoints =
				pShaderProgram->mReflection.mStageReflections[pShaderProgram->mReflection.mHullStageIndex].mNumControlPoint;
		}

		DECLARE_ZERO(VkPipelineViewportStateCreateInfo, vs);
		vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		vs.pNext = NULL;
		vs.flags = 0;
		// we are using dynimic viewports but we must set the count to 1
		vs.viewportCount = 1;
		vs.pViewports = NULL;
		vs.scissorCount = 1;
		vs.pScissors = NULL;

		BlendState*      pBlendState = pDesc->pBlendState != NULL ? pDesc->pBlendState : pRenderer->pDefaultBlendState;
		DepthState*      pDepthState = pDesc->pDepthState != NULL ? pDesc->pDepthState : pRenderer->pDefaultDepthState;
		RasterizerState* pRasterizerState = pDesc->pRasterizerState != NULL ? pDesc->pRasterizerState : pRenderer->pDefaultRasterizerState;

		DECLARE_ZERO(VkPipelineRasterizationStateCreateInfo, rs);
		rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rs.pNext = NULL;
		rs.flags = 0;
		rs.depthClampEnable = pRasterizerState->DepthClampEnable;
		rs.rasterizerDiscardEnable = VK_FALSE;
		rs.polygonMode = pRasterizerState->PolygonMode;
		rs.cullMode = pRasterizerState->CullMode;
		rs.frontFace = pRasterizerState->FrontFace;
		rs.depthBiasEnable = pRasterizerState->DepthBiasEnable;
		rs.depthBiasConstantFactor = pRasterizerState->DepthBiasConstantFactor;
		rs.depthBiasClamp = pRasterizerState->DepthBiasClamp;
		rs.depthBiasSlopeFactor = pRasterizerState->DepthBiasSlopeFactor;
		rs.lineWidth = pRasterizerState->LineWidth;

		DECLARE_ZERO(VkPipelineMultisampleStateCreateInfo, ms);
		ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		ms.pNext = NULL;
		ms.flags = 0;
		ms.rasterizationSamples = util_to_vk_sample_count(pDesc->mSampleCount);
		ms.sampleShadingEnable = VK_FALSE;
		ms.minSampleShading = 0.0f;
		ms.pSampleMask = 0;
		ms.alphaToCoverageEnable = VK_FALSE;
		ms.alphaToOneEnable = VK_FALSE;

		/// TODO: Dont create depth state if no depth stencil bound
		DECLARE_ZERO(VkPipelineDepthStencilStateCreateInfo, ds);
		ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		ds.pNext = NULL;
		ds.flags = 0;
		ds.depthTestEnable = pDepthState->DepthTestEnable;
		ds.depthWriteEnable = pDepthState->DepthWriteEnable;
		ds.depthCompareOp = pDepthState->DepthCompareOp;
		ds.depthBoundsTestEnable = pDepthState->DepthBoundsTestEnable;
		ds.stencilTestEnable = pDepthState->StencilTestEnable;
		ds.front = pDepthState->Front;
		ds.back = pDepthState->Back;
		ds.minDepthBounds = pDepthState->MinDepthBounds;
		ds.maxDepthBounds = pDepthState->MaxDepthBounds;

		DECLARE_ZERO(VkPipelineColorBlendStateCreateInfo, cb);
		cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		cb.pNext = NULL;
		cb.flags = 0;
		cb.logicOpEnable = pBlendState->LogicOpEnable;
		cb.logicOp = pBlendState->LogicOp;
		cb.attachmentCount = pDesc->mRenderTargetCount;
		cb.pAttachments = pBlendState->RTBlendStates;
		cb.blendConstants[0] = 0.0f;
		cb.blendConstants[1] = 0.0f;
		cb.blendConstants[2] = 0.0f;
		cb.blendConstants[3] = 0.0f;

		DECLARE_ZERO(VkDynamicState, dyn_states[5]);
		dyn_states[0] = VK_DYNAMIC_STATE_VIEWPORT;
		dyn_states[1] = VK_DYNAMIC_STATE_SCISSOR;
		dyn_states[2] = VK_DYNAMIC_STATE_DEPTH_BIAS;
		dyn_states[3] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
		dyn_states[4] = VK_DYNAMIC_STATE_DEPTH_BOUNDS;
		DECLARE_ZERO(VkPipelineDynamicStateCreateInfo, dy);
		dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dy.pNext = NULL;
		dy.flags = 0;
		dy.dynamicStateCount = 5;
		dy.pDynamicStates = dyn_states;

		DECLARE_ZERO(VkGraphicsPipelineCreateInfo, add_info);
		add_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		add_info.pNext = NULL;
		add_info.flags = 0;
		add_info.stageCount = stage_count;
		add_info.pStages = stages;
		add_info.pVertexInputState = &vi;
		add_info.pInputAssemblyState = &ia;

		if ((pShaderProgram->mStages & SHADER_STAGE_TESC) && (pShaderProgram->mStages & SHADER_STAGE_TESE))
			add_info.pTessellationState = &ts;
		else
			add_info.pTessellationState = NULL;    // set tessellation state to null if we have no tessellation

		add_info.pViewportState = &vs;
		add_info.pRasterizationState = &rs;
		add_info.pMultisampleState = &ms;
		add_info.pDepthStencilState = &ds;
		add_info.pColorBlendState = &cb;
		add_info.pDynamicState = &dy;
		add_info.layout = pDesc->pRootSignature->pPipelineLayout;
		add_info.renderPass = pRenderPass->pRenderPass;
		add_info.subpass = 0;
		add_info.basePipelineHandle = VK_NULL_HANDLE;
		add_info.basePipelineIndex = -1;
		VkResult vk_res = vkCreateGraphicsPipelines(pRenderer->pVkDevice, VK_NULL_HANDLE, 1, &add_info, NULL, &(pPipeline->pVkPipeline));
		ASSERT(VK_SUCCESS == vk_res);

		remove_render_pass(pRenderer, pRenderPass);
	}

	*ppPipeline = pPipeline;
}

static void addComputePipelineImpl(Renderer* pRenderer, const ComputePipelineDesc* pDesc, Pipeline** ppPipeline)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(pDesc->pShaderProgram);
	ASSERT(pDesc->pRootSignature);
	ASSERT(pRenderer->pVkDevice != VK_NULL_HANDLE);
	ASSERT(pDesc->pShaderProgram->pShaderModules[0] != VK_NULL_HANDLE);

	Pipeline* pPipeline = (Pipeline*)conf_calloc(1, sizeof(*pPipeline));
	ASSERT(pPipeline);

	conf_placement_new<Pipeline>(pPipeline);

	memcpy(&(pPipeline->mCompute), pDesc, sizeof(*pDesc));
	pPipeline->mType = PIPELINE_TYPE_COMPUTE;

	// Pipeline
	{
		DECLARE_ZERO(VkPipelineShaderStageCreateInfo, stage);
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.pNext = NULL;
		stage.flags = 0;
		stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		stage.module = pDesc->pShaderProgram->pShaderModules[0];
		stage.pName = pDesc->pShaderProgram->mReflection.mStageReflections[0].pEntryPoint;
		stage.pSpecializationInfo = NULL;

		DECLARE_ZERO(VkComputePipelineCreateInfo, create_info);
		create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		create_info.pNext = NULL;
		create_info.flags = 0;
		create_info.stage = stage;
		create_info.layout = pDesc->pRootSignature->pPipelineLayout;
		create_info.basePipelineHandle = 0;
		create_info.basePipelineIndex = 0;
		VkResult vk_res = vkCreateComputePipelines(pRenderer->pVkDevice, VK_NULL_HANDLE, 1, &create_info, NULL, &(pPipeline->pVkPipeline));
		ASSERT(VK_SUCCESS == vk_res);
	}

	*ppPipeline = pPipeline;
}

void addPipeline(Renderer* pRenderer, const PipelineDesc* pDesc, Pipeline** ppPipeline)
{
	switch (pDesc->mType)
	{
		case(PIPELINE_TYPE_COMPUTE):
		{
			addComputePipelineImpl(pRenderer, &pDesc->mComputeDesc, ppPipeline);
			break;
		}
		case(PIPELINE_TYPE_GRAPHICS):
		{
			addGraphicsPipelineImpl(pRenderer, &pDesc->mGraphicsDesc, ppPipeline);
			break;
		}
#ifdef ENABLE_RAYTRACING
		case(PIPELINE_TYPE_RAYTRACING):
		{
			vk_addRaytracingPipeline(&pDesc->mRaytracingDesc, ppPipeline);
			break;
		}
#endif
		default:
		{
			ASSERT(false);
			ppPipeline = NULL;
			break;
		}
	}
}

void removePipeline(Renderer* pRenderer, Pipeline* pPipeline)
{
	ASSERT(pRenderer);
	ASSERT(pPipeline);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pPipeline->pVkPipeline);

#ifdef ENABLE_RAYTRACING
	SAFE_FREE(pPipeline->ppShaderStageNames);
#endif

	vkDestroyPipeline(pRenderer->pVkDevice, pPipeline->pVkPipeline, NULL);
	pPipeline->~Pipeline();
	SAFE_FREE(pPipeline);
}

void addBlendState(Renderer* pRenderer, const BlendStateDesc* pDesc, BlendState** ppBlendState)
{
	int blendDescIndex = 0;
#ifdef _DEBUG

	for (int i = 0; i < MAX_RENDER_TARGET_ATTACHMENTS; ++i)
	{
		if (pDesc->mRenderTargetMask & (1 << i))
		{
			ASSERT(pDesc->mSrcFactors[blendDescIndex] < BlendConstant::MAX_BLEND_CONSTANTS);
			ASSERT(pDesc->mDstFactors[blendDescIndex] < BlendConstant::MAX_BLEND_CONSTANTS);
			ASSERT(pDesc->mSrcAlphaFactors[blendDescIndex] < BlendConstant::MAX_BLEND_CONSTANTS);
			ASSERT(pDesc->mDstAlphaFactors[blendDescIndex] < BlendConstant::MAX_BLEND_CONSTANTS);
			ASSERT(pDesc->mBlendModes[blendDescIndex] < BlendMode::MAX_BLEND_MODES);
			ASSERT(pDesc->mBlendAlphaModes[blendDescIndex] < BlendMode::MAX_BLEND_MODES);
		}

		if (pDesc->mIndependentBlend)
			++blendDescIndex;
	}

	blendDescIndex = 0;
#endif

	BlendState blendState = {};

	memset(blendState.RTBlendStates, 0, sizeof(blendState.RTBlendStates));
	for (int i = 0; i < MAX_RENDER_TARGET_ATTACHMENTS; ++i)
	{
		if (pDesc->mRenderTargetMask & (1 << i))
		{
			VkBool32 blendEnable =
				(gVkBlendConstantTranslator[pDesc->mSrcFactors[blendDescIndex]] != VK_BLEND_FACTOR_ONE ||
				 gVkBlendConstantTranslator[pDesc->mDstFactors[blendDescIndex]] != VK_BLEND_FACTOR_ZERO ||
				 gVkBlendConstantTranslator[pDesc->mSrcAlphaFactors[blendDescIndex]] != VK_BLEND_FACTOR_ONE ||
				 gVkBlendConstantTranslator[pDesc->mDstAlphaFactors[blendDescIndex]] != VK_BLEND_FACTOR_ZERO);

			blendState.RTBlendStates[i].blendEnable = blendEnable;
			blendState.RTBlendStates[i].colorWriteMask = pDesc->mMasks[blendDescIndex];
			blendState.RTBlendStates[i].srcColorBlendFactor = gVkBlendConstantTranslator[pDesc->mSrcFactors[blendDescIndex]];
			blendState.RTBlendStates[i].dstColorBlendFactor = gVkBlendConstantTranslator[pDesc->mDstFactors[blendDescIndex]];
			blendState.RTBlendStates[i].colorBlendOp = gVkBlendOpTranslator[pDesc->mBlendModes[blendDescIndex]];
			blendState.RTBlendStates[i].srcAlphaBlendFactor = gVkBlendConstantTranslator[pDesc->mSrcAlphaFactors[blendDescIndex]];
			blendState.RTBlendStates[i].dstAlphaBlendFactor = gVkBlendConstantTranslator[pDesc->mDstAlphaFactors[blendDescIndex]];
			blendState.RTBlendStates[i].alphaBlendOp = gVkBlendOpTranslator[pDesc->mBlendAlphaModes[blendDescIndex]];
		}

		if (pDesc->mIndependentBlend)
			++blendDescIndex;
	}

	blendState.LogicOpEnable = false;
	blendState.LogicOp = VK_LOGIC_OP_CLEAR;

	*ppBlendState = (BlendState*)conf_malloc(sizeof(blendState));
	memcpy(*ppBlendState, &blendState, sizeof(blendState));
}

void removeBlendState(BlendState* pBlendState) { SAFE_FREE(pBlendState); }

void addDepthState(Renderer* pRenderer, const DepthStateDesc* pDesc, DepthState** ppDepthState)
{
	ASSERT(pDesc->mDepthFunc < CompareMode::MAX_COMPARE_MODES);
	ASSERT(pDesc->mStencilFrontFunc < CompareMode::MAX_COMPARE_MODES);
	ASSERT(pDesc->mStencilFrontFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mDepthFrontFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mStencilFrontPass < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mStencilBackFunc < CompareMode::MAX_COMPARE_MODES);
	ASSERT(pDesc->mStencilBackFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mDepthBackFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mStencilBackPass < StencilOp::MAX_STENCIL_OPS);

	DepthState depthState = {};
	depthState.DepthTestEnable = pDesc->mDepthTest;
	depthState.DepthWriteEnable = pDesc->mDepthWrite;
	depthState.DepthCompareOp = gVkComparisonFuncTranslator[pDesc->mDepthFunc];
	depthState.StencilTestEnable = pDesc->mStencilTest;

	depthState.Front.failOp = gVkStencilOpTranslator[pDesc->mStencilFrontFail];
	depthState.Front.passOp = gVkStencilOpTranslator[pDesc->mStencilFrontPass];
	depthState.Front.depthFailOp = gVkStencilOpTranslator[pDesc->mDepthFrontFail];
	depthState.Front.compareOp = VkCompareOp(pDesc->mStencilFrontFunc);
	depthState.Front.compareMask = pDesc->mStencilReadMask;
	depthState.Front.writeMask = pDesc->mStencilWriteMask;
	depthState.Front.reference = 0;

	depthState.Back.failOp = gVkStencilOpTranslator[pDesc->mStencilBackFail];
	depthState.Back.passOp = gVkStencilOpTranslator[pDesc->mStencilBackPass];
	depthState.Back.depthFailOp = gVkStencilOpTranslator[pDesc->mDepthBackFail];
	depthState.Back.compareOp = gVkComparisonFuncTranslator[pDesc->mStencilBackFunc];
	depthState.Back.compareMask = pDesc->mStencilReadMask;
	depthState.Back.writeMask = pDesc->mStencilWriteMask;    // devsh fixed
	depthState.Back.reference = 0;

	depthState.DepthBoundsTestEnable = false;
	depthState.MinDepthBounds = 0;
	depthState.MaxDepthBounds = 1;

	*ppDepthState = (DepthState*)conf_malloc(sizeof(depthState));
	memcpy(*ppDepthState, &depthState, sizeof(depthState));
}

void removeDepthState(DepthState* pDepthState) { SAFE_FREE(pDepthState); }

void addRasterizerState(Renderer* pRenderer, const RasterizerStateDesc* pDesc, RasterizerState** ppRasterizerState)
{
	ASSERT(pDesc->mFillMode < FillMode::MAX_FILL_MODES);
	ASSERT(pDesc->mCullMode < CullMode::MAX_CULL_MODES);
	ASSERT(pDesc->mFrontFace == FRONT_FACE_CCW || pDesc->mFrontFace == FRONT_FACE_CW);

	RasterizerState rasterizerState = {};

	rasterizerState.DepthClampEnable = VK_TRUE;
	rasterizerState.PolygonMode = gVkFillModeTranslator[pDesc->mFillMode];
	rasterizerState.CullMode = gVkCullModeTranslator[pDesc->mCullMode];
	rasterizerState.FrontFace = gVkFrontFaceTranslator[pDesc->mFrontFace];
	rasterizerState.DepthBiasEnable = (pDesc->mDepthBias != 0) ? VK_TRUE : VK_FALSE;
	rasterizerState.DepthBiasConstantFactor = float(pDesc->mDepthBias);
	rasterizerState.DepthBiasClamp = 0.f;
	rasterizerState.DepthBiasSlopeFactor = pDesc->mSlopeScaledDepthBias;
	rasterizerState.LineWidth = 1;

	*ppRasterizerState = (RasterizerState*)conf_malloc(sizeof(rasterizerState));
	memcpy(*ppRasterizerState, &rasterizerState, sizeof(rasterizerState));
}

void removeRasterizerState(RasterizerState* pRasterizerState) { SAFE_FREE(pRasterizerState); }
/************************************************************************/
// Command buffer functions
/************************************************************************/
void beginCmd(Cmd* pCmd)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	// reset buffer to conf_free memory
	vkResetCommandBuffer(pCmd->pVkCmdBuf, 0);

	DECLARE_ZERO(VkCommandBufferBeginInfo, begin_info);
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	begin_info.pInheritanceInfo = NULL;

	VkDeviceGroupCommandBufferBeginInfoKHR deviceGroupBeginInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO_KHR };
	deviceGroupBeginInfo.pNext = NULL;
	if (pCmd->pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED)
	{
		deviceGroupBeginInfo.deviceMask = (1 << pCmd->mNodeIndex);
		begin_info.pNext = &deviceGroupBeginInfo;
	}

	VkResult vk_res = vkBeginCommandBuffer(pCmd->pVkCmdBuf, &begin_info);
	ASSERT(VK_SUCCESS == vk_res);

	// Reset CPU side data
	pCmd->pBoundRootSignature = NULL;
}

void endCmd(Cmd* pCmd)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	if (pCmd->pVkActiveRenderPass)
	{
		vkCmdEndRenderPass(pCmd->pVkCmdBuf);
	}

	pCmd->pVkActiveRenderPass = VK_NULL_HANDLE;

	VkResult vk_res = vkEndCommandBuffer(pCmd->pVkCmdBuf);
	ASSERT(VK_SUCCESS == vk_res);
}

void cmdBindRenderTargets(
	Cmd* pCmd, uint32_t renderTargetCount, RenderTarget** ppRenderTargets, RenderTarget* pDepthStencil,
	const LoadActionsDesc* pLoadActions /* = NULL*/, uint32_t* pColorArraySlices, uint32_t* pColorMipSlices, uint32_t depthArraySlice,
	uint32_t depthMipSlice)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	if (pCmd->pVkActiveRenderPass)
	{
		vkCmdEndRenderPass(pCmd->pVkCmdBuf);
		pCmd->pVkActiveRenderPass = VK_NULL_HANDLE;
	}

	if (!renderTargetCount && !pDepthStencil)
		return;

	uint64_t renderPassHash = 0;
	uint64_t frameBufferHash = 0;

	// Generate hash for render pass and frame buffer
	// NOTE:
	// Render pass does not care about underlying VkImageView. It only cares about the format and sample count of the attachments.
	// We hash those two values to generate render pass hash
	// Frame buffer is the actual array of all the VkImageViews
	// We hash the texture id associated with the render target to generate frame buffer hash
	for (uint32_t i = 0; i < renderTargetCount; ++i)
	{
		uint32_t hashValues[] = {
			(uint32_t)ppRenderTargets[i]->mDesc.mFormat,
			(uint32_t)ppRenderTargets[i]->mDesc.mSampleCount,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionsColor[i] : 0,
		};
		renderPassHash = eastl::mem_hash<uint32_t>()(hashValues, 3, renderPassHash);
		frameBufferHash = eastl::mem_hash<uint64_t>()(&ppRenderTargets[i]->mId, 1, frameBufferHash);
	}
	if (pDepthStencil)
	{
		uint32_t hashValues[] = {
			(uint32_t)pDepthStencil->mDesc.mFormat,
			(uint32_t)pDepthStencil->mDesc.mSampleCount,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionDepth : 0,
			pLoadActions ? (uint32_t)pLoadActions->mLoadActionStencil : 0,
		};
		renderPassHash = eastl::mem_hash<uint32_t>()(hashValues, 4, renderPassHash);
		frameBufferHash = eastl::mem_hash<uint64_t>()(&pDepthStencil->mId, 1, frameBufferHash);
	}
	if (pColorArraySlices)
		frameBufferHash = eastl::mem_hash<uint32_t>()(pColorArraySlices, renderTargetCount, frameBufferHash);
	if (pColorMipSlices)
		frameBufferHash = eastl::mem_hash<uint32_t>()(pColorMipSlices, renderTargetCount, frameBufferHash);
	if (depthArraySlice != -1)
		frameBufferHash = eastl::mem_hash<uint32_t>()(&depthArraySlice, 1, frameBufferHash);
	if (depthMipSlice != -1)
		frameBufferHash = eastl::mem_hash<uint32_t>()(&depthMipSlice, 1, frameBufferHash);

	SampleCount sampleCount = renderTargetCount ? ppRenderTargets[0]->mDesc.mSampleCount : pDepthStencil->mDesc.mSampleCount;

	RenderPassMap&  renderPassMap = get_render_pass_map();
	FrameBufferMap& frameBufferMap = get_frame_buffer_map();

	const RenderPassMapIt  pNode = renderPassMap.find(renderPassHash);
	const FrameBufferMapIt pFrameBufferNode = frameBufferMap.find(frameBufferHash);

	RenderPass*  pRenderPass = NULL;
	FrameBuffer* pFrameBuffer = NULL;

	// If a render pass of this combination already exists just use it or create a new one
	if (pNode != renderPassMap.end())
	{
		pRenderPass = pNode->second;
	}
	else
	{
		TinyImageFormat colorFormats[MAX_RENDER_TARGET_ATTACHMENTS] = {};
		TinyImageFormat depthStencilFormat = TinyImageFormat_UNDEFINED;
		for (uint32_t i = 0; i < renderTargetCount; ++i)
		{
			colorFormats[i] = ppRenderTargets[i]->mDesc.mFormat;
		}
		if (pDepthStencil)
		{
			depthStencilFormat = pDepthStencil->mDesc.mFormat;
		}

		RenderPassDesc renderPassDesc = {};
		renderPassDesc.mRenderTargetCount = renderTargetCount;
		renderPassDesc.mSampleCount = sampleCount;
		renderPassDesc.pColorFormats = colorFormats;
		renderPassDesc.mDepthStencilFormat = depthStencilFormat;
		renderPassDesc.pLoadActionsColor = pLoadActions ? pLoadActions->mLoadActionsColor : NULL;
		renderPassDesc.mLoadActionDepth = pLoadActions ? pLoadActions->mLoadActionDepth : LOAD_ACTION_DONTCARE;
		renderPassDesc.mLoadActionStencil = pLoadActions ? pLoadActions->mLoadActionStencil : LOAD_ACTION_DONTCARE;
		add_render_pass(pCmd->pRenderer, &renderPassDesc, &pRenderPass);

		// No need of a lock here since this map is per thread
		renderPassMap.insert({{ renderPassHash, pRenderPass }});
	}

	// If a frame buffer of this combination already exists just use it or create a new one
	if (pFrameBufferNode != frameBufferMap.end())
	{
		pFrameBuffer = pFrameBufferNode->second;
	}
	else
	{
		FrameBufferDesc desc = { 0 };
		desc.mRenderTargetCount = renderTargetCount;
		desc.pDepthStencil = pDepthStencil;
		desc.ppRenderTargets = ppRenderTargets;
		desc.pRenderPass = pRenderPass;
		desc.pColorArraySlices = pColorArraySlices;
		desc.pColorMipSlices = pColorMipSlices;
		desc.mDepthArraySlice = depthArraySlice;
		desc.mDepthMipSlice = depthMipSlice;
		add_framebuffer(pCmd->pRenderer, &desc, &pFrameBuffer);

		// No need of a lock here since this map is per thread
		frameBufferMap.insert({{ frameBufferHash, pFrameBuffer }});
	}

	DECLARE_ZERO(VkRect2D, render_area);
	render_area.offset.x = 0;
	render_area.offset.y = 0;
	render_area.extent.width = pFrameBuffer->mWidth;
	render_area.extent.height = pFrameBuffer->mHeight;

	uint32_t     clearValueCount = renderTargetCount;
	VkClearValue clearValues[MAX_RENDER_TARGET_ATTACHMENTS + 1] = {};
	if (pLoadActions)
	{
		for (uint32_t i = 0; i < renderTargetCount; ++i)
		{
			ClearValue clearValue = pLoadActions->mClearColorValues[i];
			clearValues[i].color = {{ clearValue.r, clearValue.g, clearValue.b, clearValue.a }};
		}
		if (pDepthStencil)
		{
			clearValues[renderTargetCount].depthStencil = { pLoadActions->mClearDepth.depth, pLoadActions->mClearDepth.stencil };
			++clearValueCount;
		}
	}

	DECLARE_ZERO(VkRenderPassBeginInfo, begin_info);
	begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin_info.pNext = NULL;
	begin_info.renderPass = pRenderPass->pRenderPass;
	begin_info.framebuffer = pFrameBuffer->pFramebuffer;
	begin_info.renderArea = render_area;
	begin_info.clearValueCount = clearValueCount;
	begin_info.pClearValues = clearValues;

	vkCmdBeginRenderPass(pCmd->pVkCmdBuf, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
	pCmd->pVkActiveRenderPass = pRenderPass->pRenderPass;
}

void cmdSetViewport(Cmd* pCmd, float x, float y, float width, float height, float minDepth, float maxDepth)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	DECLARE_ZERO(VkViewport, viewport);
	viewport.x = x;
	viewport.y = y + height;
	viewport.width = width;
	viewport.height = -height;
	viewport.minDepth = minDepth;
	viewport.maxDepth = maxDepth;
	vkCmdSetViewport(pCmd->pVkCmdBuf, 0, 1, &viewport);
}

void cmdSetScissor(Cmd* pCmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	DECLARE_ZERO(VkRect2D, rect);
	rect.offset.x = x;
	rect.offset.y = y;
	rect.extent.width = width;
	rect.extent.height = height;
	vkCmdSetScissor(pCmd->pVkCmdBuf, 0, 1, &rect);
}

void cmdBindPipeline(Cmd* pCmd, Pipeline* pPipeline)
{
	ASSERT(pCmd);
	ASSERT(pPipeline);
	ASSERT(pCmd->pVkCmdBuf != VK_NULL_HANDLE);

	VkPipelineBindPoint pipeline_bind_point = gPipelineBindPoint[pPipeline->mType];
	vkCmdBindPipeline(pCmd->pVkCmdBuf, pipeline_bind_point, pPipeline->pVkPipeline);
}

void cmdBindIndexBuffer(Cmd* pCmd, Buffer* pBuffer, uint64_t offset)
{
	ASSERT(pCmd);
	ASSERT(pBuffer);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	VkIndexType vk_index_type = (INDEX_TYPE_UINT16 == pBuffer->mDesc.mIndexType) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
	vkCmdBindIndexBuffer(pCmd->pVkCmdBuf, pBuffer->pVkBuffer, pBuffer->mPositionInHeap + offset, vk_index_type);
}

void cmdBindVertexBuffer(Cmd* pCmd, uint32_t bufferCount, Buffer** ppBuffers, uint64_t* pOffsets)
{
	ASSERT(pCmd);
	ASSERT(0 != bufferCount);
	ASSERT(ppBuffers);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	const uint32_t max_buffers = pCmd->pRenderer->pVkActiveGPUProperties->properties.limits.maxVertexInputBindings;
	uint32_t       capped_buffer_count = bufferCount > max_buffers ? max_buffers : bufferCount;

	// No upper bound for this, so use 64 for now
	ASSERT(capped_buffer_count < 64);

	DECLARE_ZERO(VkBuffer, buffers[64]);
	DECLARE_ZERO(VkDeviceSize, offsets[64]);

	for (uint32_t i = 0; i < capped_buffer_count; ++i)
	{
		buffers[i] = ppBuffers[i]->pVkBuffer;
		offsets[i] = (ppBuffers[i]->mPositionInHeap + (pOffsets ? pOffsets[i] : 0));
	}

	vkCmdBindVertexBuffers(pCmd->pVkCmdBuf, 0, capped_buffer_count, buffers, offsets);
}

void cmdDraw(Cmd* pCmd, uint32_t vertex_count, uint32_t first_vertex)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	vkCmdDraw(pCmd->pVkCmdBuf, vertex_count, 1, first_vertex, 0);
}

void cmdDrawInstanced(Cmd* pCmd, uint32_t vertexCount, uint32_t firstVertex, uint32_t instanceCount, uint32_t firstInstance)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	vkCmdDraw(pCmd->pVkCmdBuf, vertexCount, instanceCount, firstVertex, firstInstance);
}

void cmdDrawIndexed(Cmd* pCmd, uint32_t index_count, uint32_t first_index, uint32_t first_vertex)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	vkCmdDrawIndexed(pCmd->pVkCmdBuf, index_count, 1, first_index, first_vertex, 0);
}

void cmdDrawIndexedInstanced(
	Cmd* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t instanceCount, uint32_t firstInstance, uint32_t firstVertex)
{
	ASSERT(pCmd);
	ASSERT(VK_NULL_HANDLE != pCmd->pVkCmdBuf);

	vkCmdDrawIndexed(pCmd->pVkCmdBuf, indexCount, instanceCount, firstIndex, firstVertex, firstInstance);
}

void cmdDispatch(Cmd* pCmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	ASSERT(pCmd);
	ASSERT(pCmd->pVkCmdBuf != VK_NULL_HANDLE);

	vkCmdDispatch(pCmd->pVkCmdBuf, groupCountX, groupCountY, groupCountZ);
}

void cmdResourceBarrier(Cmd* pCmd, uint32_t numBufferBarriers, BufferBarrier* pBufferBarriers, uint32_t numTextureBarriers, TextureBarrier* pTextureBarriers)
{
	VkImageMemoryBarrier* imageBarriers =
		numTextureBarriers ? (VkImageMemoryBarrier*)alloca(numTextureBarriers * sizeof(VkImageMemoryBarrier)) : NULL;
	uint32_t imageBarrierCount = 0;

	VkBufferMemoryBarrier* bufferBarriers =
		numBufferBarriers ? (VkBufferMemoryBarrier*)alloca(numBufferBarriers * sizeof(VkBufferMemoryBarrier)) : NULL;
	uint32_t bufferBarrierCount = 0;

	VkAccessFlags srcAccessFlags = 0;
	VkAccessFlags dstAccessFlags = 0;

	for (uint32_t i = 0; i < numBufferBarriers; ++i)
	{
		BufferBarrier* pTrans = &pBufferBarriers[i];
		Buffer*        pBuffer = pTrans->pBuffer;

		if (!(pTrans->mNewState & pBuffer->mCurrentState))
		{
			VkBufferMemoryBarrier* pBufferBarrier = &bufferBarriers[bufferBarrierCount++];
			pBufferBarrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			pBufferBarrier->pNext = NULL;

			pBufferBarrier->buffer = pBuffer->pVkBuffer;
			pBufferBarrier->size = VK_WHOLE_SIZE;
			pBufferBarrier->offset = 0;

			pBufferBarrier->srcAccessMask = util_to_vk_access_flags(pBuffer->mCurrentState);
			pBufferBarrier->dstAccessMask = util_to_vk_access_flags(pTrans->mNewState);

			pBufferBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			pBufferBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			pBuffer->mCurrentState = pTrans->mNewState;

			srcAccessFlags |= pBufferBarrier->srcAccessMask;
			dstAccessFlags |= pBufferBarrier->dstAccessMask;
		}
		else if (pTrans->mNewState == RESOURCE_STATE_UNORDERED_ACCESS)
		{
			VkBufferMemoryBarrier* pBufferBarrier = &bufferBarriers[bufferBarrierCount++];
			pBufferBarrier->sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
			pBufferBarrier->pNext = NULL;

			pBufferBarrier->buffer = pBuffer->pVkBuffer;
			pBufferBarrier->size = VK_WHOLE_SIZE;
			pBufferBarrier->offset = 0;

			pBufferBarrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			pBufferBarrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;

			pBufferBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			pBufferBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			srcAccessFlags |= pBufferBarrier->srcAccessMask;
			dstAccessFlags |= pBufferBarrier->dstAccessMask;
		}
	}
	for (uint32_t i = 0; i < numTextureBarriers; ++i)
	{
		TextureBarrier* pTrans = &pTextureBarriers[i];
		Texture*        pTexture = pTrans->pTexture;

		if (!(pTrans->mNewState & pTexture->mCurrentState))
		{
			VkImageMemoryBarrier* pImageBarrier = &imageBarriers[imageBarrierCount++];
			pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			pImageBarrier->pNext = NULL;

			pImageBarrier->image = pTexture->pVkImage;
			pImageBarrier->subresourceRange.aspectMask = pTexture->mVkAspectMask;
			pImageBarrier->subresourceRange.baseMipLevel = 0;
			pImageBarrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			pImageBarrier->subresourceRange.baseArrayLayer = 0;
			pImageBarrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

			pImageBarrier->srcAccessMask = util_to_vk_access_flags(pTexture->mCurrentState);
			pImageBarrier->dstAccessMask = util_to_vk_access_flags(pTrans->mNewState);
			pImageBarrier->oldLayout = util_to_vk_image_layout(pTexture->mCurrentState);
			pImageBarrier->newLayout = util_to_vk_image_layout(pTrans->mNewState);

			pImageBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			pImageBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			pTexture->mCurrentState = pTrans->mNewState;

			srcAccessFlags |= pImageBarrier->srcAccessMask;
			dstAccessFlags |= pImageBarrier->dstAccessMask;
		}
		else if (pTrans->mNewState == RESOURCE_STATE_UNORDERED_ACCESS)
		{
			VkImageMemoryBarrier* pImageBarrier = &imageBarriers[imageBarrierCount++];
			pImageBarrier->sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			pImageBarrier->pNext = NULL;

			pImageBarrier->image = pTexture->pVkImage;
			pImageBarrier->subresourceRange.aspectMask = pTexture->mVkAspectMask;
			pImageBarrier->subresourceRange.baseMipLevel = 0;
			pImageBarrier->subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
			pImageBarrier->subresourceRange.baseArrayLayer = 0;
			pImageBarrier->subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

			pImageBarrier->srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			pImageBarrier->dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
			pImageBarrier->oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			pImageBarrier->newLayout = VK_IMAGE_LAYOUT_GENERAL;

			pImageBarrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			pImageBarrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			srcAccessFlags |= pImageBarrier->srcAccessMask;
			dstAccessFlags |= pImageBarrier->dstAccessMask;
		}
	}

	VkPipelineStageFlags srcStageMask = util_determine_pipeline_stage_flags(srcAccessFlags, pCmd->pCmdPool->mCmdPoolDesc.mCmdPoolType);
	VkPipelineStageFlags dstStageMask = util_determine_pipeline_stage_flags(dstAccessFlags, pCmd->pCmdPool->mCmdPoolDesc.mCmdPoolType);

	if (bufferBarrierCount || imageBarrierCount)
	{
		vkCmdPipelineBarrier(
			pCmd->pVkCmdBuf, srcStageMask, dstStageMask, 0, 0, NULL, bufferBarrierCount, bufferBarriers, imageBarrierCount,
			imageBarriers);
	}
}

void cmdUpdateBuffer(Cmd* pCmd, Buffer* pBuffer, uint64_t dstOffset, Buffer* pSrcBuffer, uint64_t srcOffset, uint64_t size)
{
	ASSERT(pCmd);
	ASSERT(pSrcBuffer);
	ASSERT(pSrcBuffer->pVkBuffer);
	ASSERT(pBuffer);
	ASSERT(pBuffer->pVkBuffer);
	ASSERT(srcOffset + size <= pSrcBuffer->mDesc.mSize);
	ASSERT(dstOffset + size <= pBuffer->mDesc.mSize);

	DECLARE_ZERO(VkBufferCopy, region);
	region.srcOffset = pSrcBuffer->mPositionInHeap + srcOffset;
	region.dstOffset = pBuffer->mPositionInHeap + dstOffset;
	region.size = (VkDeviceSize)size;
	vkCmdCopyBuffer(pCmd->pVkCmdBuf, pSrcBuffer->pVkBuffer, pBuffer->pVkBuffer, 1, &region);
}

void cmdUpdateSubresource(Cmd* pCmd, Texture* pTexture, Buffer* pSrcBuffer, SubresourceDataDesc* pSubresourceDesc)
{
	VkBufferImageCopy pCopy;
	pCopy.bufferOffset = pSrcBuffer->mPositionInHeap + pSubresourceDesc->mBufferOffset;
	pCopy.bufferRowLength = 0;
	pCopy.bufferImageHeight = 0;
	pCopy.imageSubresource.aspectMask = pTexture->mVkAspectMask;
	pCopy.imageSubresource.mipLevel = pSubresourceDesc->mMipLevel;
	pCopy.imageSubresource.baseArrayLayer = pSubresourceDesc->mArrayLayer;
	pCopy.imageSubresource.layerCount = 1;
	pCopy.imageOffset.x = pSubresourceDesc->mRegion.mXOffset;
	pCopy.imageOffset.y = pSubresourceDesc->mRegion.mYOffset;
	pCopy.imageOffset.z = pSubresourceDesc->mRegion.mZOffset;
	pCopy.imageExtent.width = pSubresourceDesc->mRegion.mWidth;
	pCopy.imageExtent.height = pSubresourceDesc->mRegion.mHeight;
	pCopy.imageExtent.depth = pSubresourceDesc->mRegion.mDepth;

	vkCmdCopyBufferToImage(pCmd->pVkCmdBuf, pSrcBuffer->pVkBuffer, pTexture->pVkImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &pCopy);
}
/************************************************************************/
// Queue Fence Semaphore Functions
/************************************************************************/
void acquireNextImage(Renderer* pRenderer, SwapChain* pSwapChain, Semaphore* pSignalSemaphore, Fence* pFence, uint32_t* pImageIndex)
{
	ASSERT(pRenderer);
	ASSERT(VK_NULL_HANDLE != pRenderer->pVkDevice);
	ASSERT(VK_NULL_HANDLE != pSwapChain->pSwapChain);
	ASSERT(pSignalSemaphore || pFence);

	VkResult vk_res = {};

	if (pFence != NULL)
	{
		vk_res =
			vkAcquireNextImageKHR(pRenderer->pVkDevice, pSwapChain->pSwapChain, UINT64_MAX, VK_NULL_HANDLE, pFence->pVkFence, pImageIndex);

		// If swapchain is out of date, let caller know by setting image index to -1
		if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			*pImageIndex = -1;
			vkResetFences(pRenderer->pVkDevice, 1, &pFence->pVkFence);
			pFence->mSubmitted = false;
			return;
		}

		pFence->mSubmitted = true;
	}
	else
	{
		vk_res = vkAcquireNextImageKHR(
			pRenderer->pVkDevice, pSwapChain->pSwapChain, UINT64_MAX, pSignalSemaphore->pVkSemaphore, VK_NULL_HANDLE, pImageIndex);

		// If swapchain is out of date, let caller know by setting image index to -1
		if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
		{
			*pImageIndex = -1;
			pSignalSemaphore->mSignaled = false;
			return;
		}

		ASSERT(VK_SUCCESS == vk_res);
		pSignalSemaphore->mSignaled = true;
	}

	pRenderer->mCurrentFrameIdx = (pRenderer->mCurrentFrameIdx + 1) % pSwapChain->mDesc.mImageCount;
}

void queueSubmit(
	Queue* pQueue, uint32_t cmdCount, Cmd** ppCmds, Fence* pFence, uint32_t waitSemaphoreCount, Semaphore** ppWaitSemaphores,
	uint32_t signalSemaphoreCount, Semaphore** ppSignalSemaphores)
{
	ASSERT(pQueue);
	ASSERT(cmdCount > 0);
	ASSERT(ppCmds);
	if (waitSemaphoreCount > 0)
	{
		ASSERT(ppWaitSemaphores);
	}
	if (signalSemaphoreCount > 0)
	{
		ASSERT(ppSignalSemaphores);
	}

	ASSERT(VK_NULL_HANDLE != pQueue->pVkQueue);

	cmdCount = cmdCount > MAX_SUBMIT_CMDS ? MAX_SUBMIT_CMDS : cmdCount;
	waitSemaphoreCount = waitSemaphoreCount > MAX_SUBMIT_WAIT_SEMAPHORES ? MAX_SUBMIT_WAIT_SEMAPHORES : waitSemaphoreCount;
	signalSemaphoreCount = signalSemaphoreCount > MAX_SUBMIT_SIGNAL_SEMAPHORES ? MAX_SUBMIT_SIGNAL_SEMAPHORES : signalSemaphoreCount;

	VkCommandBuffer* cmds = (VkCommandBuffer*)alloca(cmdCount * sizeof(VkCommandBuffer));
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		cmds[i] = ppCmds[i]->pVkCmdBuf;
	}

	VkSemaphore*          wait_semaphores = waitSemaphoreCount ? (VkSemaphore*)alloca(waitSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	VkPipelineStageFlags* wait_masks = (VkPipelineStageFlags*)alloca(waitSemaphoreCount * sizeof(VkPipelineStageFlags));
	uint32_t              waitCount = 0;
	for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
	{
		if (ppWaitSemaphores[i]->mSignaled)
		{
			wait_semaphores[waitCount] = ppWaitSemaphores[i]->pVkSemaphore;
			wait_masks[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			++waitCount;

			ppWaitSemaphores[i]->mSignaled = false;
		}
	}

	VkSemaphore* signal_semaphores = signalSemaphoreCount ? (VkSemaphore*)alloca(signalSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	uint32_t     signalCount = 0;
	for (uint32_t i = 0; i < signalSemaphoreCount; ++i)
	{
		if (!ppSignalSemaphores[i]->mSignaled)
		{
			signal_semaphores[signalCount] = ppSignalSemaphores[i]->pVkSemaphore;
			ppSignalSemaphores[i]->mCurrentNodeIndex = pQueue->mQueueDesc.mNodeIndex;
			ppSignalSemaphores[signalCount]->mSignaled = true;
			++signalCount;
		}
	}

	DECLARE_ZERO(VkSubmitInfo, submit_info);
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.pNext = NULL;
	submit_info.waitSemaphoreCount = waitCount;
	submit_info.pWaitSemaphores = wait_semaphores;
	submit_info.pWaitDstStageMask = wait_masks;
	submit_info.commandBufferCount = cmdCount;
	submit_info.pCommandBuffers = cmds;
	submit_info.signalSemaphoreCount = signalCount;
	submit_info.pSignalSemaphores = signal_semaphores;

	VkDeviceGroupSubmitInfo deviceGroupSubmitInfo = { VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO_KHR };
	if (pQueue->pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED)
	{
		uint32_t* pVkDeviceMasks = NULL;
		uint32_t* pSignalIndices = NULL;
		uint32_t* pWaitIndices = NULL;
		deviceGroupSubmitInfo.pNext = NULL;
		deviceGroupSubmitInfo.commandBufferCount = submit_info.commandBufferCount;
		deviceGroupSubmitInfo.signalSemaphoreCount = submit_info.signalSemaphoreCount;
		deviceGroupSubmitInfo.waitSemaphoreCount = submit_info.waitSemaphoreCount;

		pVkDeviceMasks = (uint32_t*)alloca(deviceGroupSubmitInfo.commandBufferCount * sizeof(uint32_t));
		pSignalIndices = (uint32_t*)alloca(deviceGroupSubmitInfo.signalSemaphoreCount * sizeof(uint32_t));
		pWaitIndices = (uint32_t*)alloca(deviceGroupSubmitInfo.waitSemaphoreCount * sizeof(uint32_t));

		for (uint32_t i = 0; i < deviceGroupSubmitInfo.commandBufferCount; ++i)
		{
			pVkDeviceMasks[i] = (1 << ppCmds[i]->mNodeIndex);
		}
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.signalSemaphoreCount; ++i)
		{
			pSignalIndices[i] = pQueue->mQueueDesc.mNodeIndex;
		}
		for (uint32_t i = 0; i < deviceGroupSubmitInfo.waitSemaphoreCount; ++i)
		{
			pWaitIndices[i] = ppWaitSemaphores[i]->mCurrentNodeIndex;
		}

		deviceGroupSubmitInfo.pCommandBufferDeviceMasks = pVkDeviceMasks;
		deviceGroupSubmitInfo.pSignalSemaphoreDeviceIndices = pSignalIndices;
		deviceGroupSubmitInfo.pWaitSemaphoreDeviceIndices = pWaitIndices;
		submit_info.pNext = &deviceGroupSubmitInfo;
	}

	VkResult vk_res = vkQueueSubmit(pQueue->pVkQueue, 1, &submit_info, pFence ? pFence->pVkFence : VK_NULL_HANDLE);
	ASSERT(VK_SUCCESS == vk_res);

	if (pFence)
		pFence->mSubmitted = true;
}

void queuePresent(
	Queue* pQueue, SwapChain* pSwapChain, uint32_t swapChainImageIndex, uint32_t waitSemaphoreCount, Semaphore** ppWaitSemaphores)
{
	ASSERT(pQueue);
	if (waitSemaphoreCount > 0)
	{
		ASSERT(ppWaitSemaphores);
	}

	ASSERT(VK_NULL_HANDLE != pQueue->pVkQueue);

	VkSemaphore* wait_semaphores = waitSemaphoreCount ? (VkSemaphore*)alloca(waitSemaphoreCount * sizeof(VkSemaphore)) : NULL;
	waitSemaphoreCount = waitSemaphoreCount > MAX_PRESENT_WAIT_SEMAPHORES ? MAX_PRESENT_WAIT_SEMAPHORES : waitSemaphoreCount;
	uint32_t waitCount = 0;
	for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
	{
		if (ppWaitSemaphores[i]->mSignaled)
		{
			wait_semaphores[waitCount] = ppWaitSemaphores[i]->pVkSemaphore;
			ppWaitSemaphores[i]->mSignaled = false;
			++waitCount;
		}
	}

	DECLARE_ZERO(VkPresentInfoKHR, present_info);
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.pNext = NULL;
	present_info.waitSemaphoreCount = waitCount;
	present_info.pWaitSemaphores = wait_semaphores;
	present_info.swapchainCount = 1;
	present_info.pSwapchains = &(pSwapChain->pSwapChain);
	present_info.pImageIndices = &(swapChainImageIndex);
	present_info.pResults = NULL;

	VkResult vk_res = vkQueuePresentKHR(pSwapChain->pPresentQueue ? pSwapChain->pPresentQueue : pQueue->pVkQueue, &present_info);
	if (vk_res == VK_ERROR_OUT_OF_DATE_KHR)
	{
		// TODO : Fix bug where we get this error if window is closed before able to present queue.
	}
	else
		ASSERT(VK_SUCCESS == vk_res);
}

void waitForFences(Renderer* pRenderer, uint32_t fenceCount, Fence** ppFences)
{
	ASSERT(pRenderer);
	ASSERT(fenceCount);
	ASSERT(ppFences);

	VkFence* pFences = (VkFence*)alloca(fenceCount * sizeof(VkFence));
	uint32_t numValidFences = 0;
	for (uint32_t i = 0; i < fenceCount; ++i)
	{
		if (ppFences[i]->mSubmitted)
			pFences[numValidFences++] = ppFences[i]->pVkFence;
	}

	if (numValidFences)
	{
		vkWaitForFences(pRenderer->pVkDevice, numValidFences, pFences, VK_TRUE, UINT64_MAX);
		vkResetFences(pRenderer->pVkDevice, numValidFences, pFences);
	}

	for (uint32_t i = 0; i < fenceCount; ++i)
		ppFences[i]->mSubmitted = false;
}


void waitQueueIdle(Queue* pQueue)
{
	vkQueueWaitIdle(pQueue->pVkQueue);
}

void getFenceStatus(Renderer* pRenderer, Fence* pFence, FenceStatus* pFenceStatus)
{
	*pFenceStatus = FENCE_STATUS_COMPLETE;

	if (pFence->mSubmitted)
	{
		VkResult vkRes = vkGetFenceStatus(pRenderer->pVkDevice, pFence->pVkFence);
		if (vkRes == VK_SUCCESS)
		{
			vkResetFences(pRenderer->pVkDevice, 1, &pFence->pVkFence);
			pFence->mSubmitted = false;
		}

		*pFenceStatus = vkRes == VK_SUCCESS ? FENCE_STATUS_COMPLETE : FENCE_STATUS_INCOMPLETE;
	}
	else
	{
		*pFenceStatus = FENCE_STATUS_NOTSUBMITTED;
	}
}
/************************************************************************/
// Utility functions
/************************************************************************/
TinyImageFormat getRecommendedSwapchainFormat(bool hintHDR)
{
	//TODO: figure out this properly. BGRA not supported on android
#ifndef VK_USE_PLATFORM_ANDROID_KHR
	return TinyImageFormat_B8G8R8A8_UNORM;
#else
	return TinyImageFormat_R8G8B8A8_UNORM;
#endif
}

/************************************************************************/
// Indirect draw functions
/************************************************************************/
void addIndirectCommandSignature(Renderer* pRenderer, const CommandSignatureDesc* pDesc, CommandSignature** ppCommandSignature)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);

	CommandSignature* pCommandSignature = (CommandSignature*)conf_calloc(1, sizeof(CommandSignature));
	pCommandSignature->mDesc = *pDesc;

	for (uint32_t i = 0; i < pDesc->mIndirectArgCount; ++i)    // counting for all types;
	{
		switch (pDesc->pArgDescs[i].mType)
		{
			case INDIRECT_DRAW:
				pCommandSignature->mDrawType = INDIRECT_DRAW;
				pCommandSignature->mDrawCommandStride += sizeof(IndirectDrawArguments);
				break;
			case INDIRECT_DRAW_INDEX:
				pCommandSignature->mDrawType = INDIRECT_DRAW_INDEX;
				pCommandSignature->mDrawCommandStride += sizeof(IndirectDrawIndexArguments);
				break;
			case INDIRECT_DISPATCH:
				pCommandSignature->mDrawType = INDIRECT_DISPATCH;
				pCommandSignature->mDrawCommandStride += sizeof(IndirectDispatchArguments);
				break;
			default: LOGF(LogLevel::eERROR, "Vulkan runtime only supports IndirectDraw, IndirectDrawIndex and IndirectDispatch at this point"); break;
		}
	}

	pCommandSignature->mDrawCommandStride = round_up(pCommandSignature->mDrawCommandStride, 16);

	*ppCommandSignature = pCommandSignature;
}

void removeIndirectCommandSignature(Renderer* pRenderer, CommandSignature* pCommandSignature) { SAFE_FREE(pCommandSignature); }

void cmdExecuteIndirect(
	Cmd* pCmd, CommandSignature* pCommandSignature, uint maxCommandCount, Buffer* pIndirectBuffer, uint64_t bufferOffset,
	Buffer* pCounterBuffer, uint64_t counterBufferOffset)
{
	if (pCommandSignature->mDrawType == INDIRECT_DRAW)
	{
		if (pCounterBuffer && pfnVkCmdDrawIndirectCountKHR)
			pfnVkCmdDrawIndirectCountKHR(
				pCmd->pVkCmdBuf, pIndirectBuffer->pVkBuffer, bufferOffset, pCounterBuffer->pVkBuffer, counterBufferOffset, maxCommandCount,
				pCommandSignature->mDrawCommandStride);
		else
			vkCmdDrawIndirect(
				pCmd->pVkCmdBuf, pIndirectBuffer->pVkBuffer, bufferOffset, maxCommandCount, pCommandSignature->mDrawCommandStride);
	}
	else if (pCommandSignature->mDrawType == INDIRECT_DRAW_INDEX)
	{
		if (pCounterBuffer && pfnVkCmdDrawIndexedIndirectCountKHR)
			pfnVkCmdDrawIndexedIndirectCountKHR(
				pCmd->pVkCmdBuf, pIndirectBuffer->pVkBuffer, bufferOffset, pCounterBuffer->pVkBuffer, counterBufferOffset, maxCommandCount,
				pCommandSignature->mDrawCommandStride);
		else
			vkCmdDrawIndexedIndirect(
				pCmd->pVkCmdBuf, pIndirectBuffer->pVkBuffer, bufferOffset, maxCommandCount, pCommandSignature->mDrawCommandStride);
	}
	else if (pCommandSignature->mDrawType == INDIRECT_DISPATCH)
	{
		vkCmdDispatchIndirect(pCmd->pVkCmdBuf, pIndirectBuffer->pVkBuffer, bufferOffset);
	}
}
/************************************************************************/
// Query Heap Implementation
/************************************************************************/
VkQueryType util_to_vk_query_type(QueryType type)
{
	switch (type)
	{
		case QUERY_TYPE_TIMESTAMP: return VK_QUERY_TYPE_TIMESTAMP;
		case QUERY_TYPE_PIPELINE_STATISTICS: return VK_QUERY_TYPE_PIPELINE_STATISTICS;
		case QUERY_TYPE_OCCLUSION: return VK_QUERY_TYPE_OCCLUSION;
		default: ASSERT(false && "Invalid query heap type"); return VK_QUERY_TYPE_MAX_ENUM;
	}
}

void getTimestampFrequency(Queue* pQueue, double* pFrequency)
{
	ASSERT(pQueue);
	ASSERT(pFrequency);

	// The framework is using ticks per sec as frequency. Vulkan is nano sec per tick.
	// Handle the conversion logic here.
	*pFrequency = 1.0f / ((double)pQueue->pRenderer->pVkActiveGPUProperties->properties.limits
							  .timestampPeriod /*ns/tick number of nanoseconds required for a timestamp query to be incremented by 1*/
						  * 1e-9);             // convert to ticks/sec (DX12 standard)
}

void addQueryPool(Renderer* pRenderer, const QueryPoolDesc* pDesc, QueryPool** ppQueryPool)
{
	QueryPool* pQueryPool = (QueryPool*)conf_calloc(1, sizeof(*pQueryPool));
	pQueryPool->mDesc = *pDesc;

	VkQueryPoolCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.pNext = NULL;
	createInfo.queryCount = pDesc->mQueryCount;
	createInfo.queryType = util_to_vk_query_type(pDesc->mType);
	createInfo.flags = 0;
	createInfo.pipelineStatistics = 0;
	vkCreateQueryPool(pRenderer->pVkDevice, &createInfo, NULL, &pQueryPool->pVkQueryPool);

	*ppQueryPool = pQueryPool;
}

void removeQueryPool(Renderer* pRenderer, QueryPool* pQueryPool)
{
	vkDestroyQueryPool(pRenderer->pVkDevice, pQueryPool->pVkQueryPool, NULL);
	SAFE_FREE(pQueryPool);
}

void cmdResetQueryPool(Cmd* pCmd, QueryPool* pQueryPool, uint32_t startQuery, uint32_t queryCount)
{
	vkCmdResetQueryPool(pCmd->pVkCmdBuf, pQueryPool->pVkQueryPool, startQuery, queryCount);
}

void cmdBeginQuery(Cmd* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	QueryType type = pQueryPool->mDesc.mType;
	switch (type)
	{
		case QUERY_TYPE_TIMESTAMP:
			vkCmdWriteTimestamp(pCmd->pVkCmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pQueryPool->pVkQueryPool, pQuery->mIndex);
			break;
		case QUERY_TYPE_PIPELINE_STATISTICS: break;
		case QUERY_TYPE_OCCLUSION: break;
		default: break;
	}
}

void cmdEndQuery(Cmd* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	QueryType type = pQueryPool->mDesc.mType;
	switch (type)
	{
		case QUERY_TYPE_TIMESTAMP:
			vkCmdWriteTimestamp(pCmd->pVkCmdBuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pQueryPool->pVkQueryPool, pQuery->mIndex);
			break;
		case QUERY_TYPE_PIPELINE_STATISTICS: break;
		case QUERY_TYPE_OCCLUSION: break;
		default: break;
	}
}

void cmdResolveQuery(Cmd* pCmd, QueryPool* pQueryPool, Buffer* pReadbackBuffer, uint32_t startQuery, uint32_t queryCount)
{
	vkCmdCopyQueryPoolResults(
		pCmd->pVkCmdBuf, pQueryPool->pVkQueryPool, startQuery, queryCount, pReadbackBuffer->pVkBuffer, 0, sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
}
/************************************************************************/
// Memory Stats Implementation
/************************************************************************/
void calculateMemoryStats(Renderer* pRenderer, char** stats) { vmaBuildStatsString(pRenderer->pVmaAllocator, stats, 0); }

void freeMemoryStats(Renderer* pRenderer, char* stats) { vmaFreeStatsString(pRenderer->pVmaAllocator, stats); }
/************************************************************************/
// Debug Marker Implementation
/************************************************************************/
void cmdBeginDebugMarker(Cmd* pCmd, float r, float g, float b, const char* pName)
{
	if (gDebugMarkerSupport)
	{
#ifdef USE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT markerInfo = {};
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pLabelName = pName;
		vkCmdBeginDebugUtilsLabelEXT(pCmd->pVkCmdBuf, &markerInfo);
#else
		VkDebugMarkerMarkerInfoEXT markerInfo = {};
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pMarkerName = pName;
		vkCmdDebugMarkerBeginEXT(pCmd->pVkCmdBuf, &markerInfo);
#endif
	}
}

void cmdEndDebugMarker(Cmd* pCmd)
{
	if (gDebugMarkerSupport)
	{
#ifdef USE_DEBUG_UTILS_EXTENSION
		vkCmdEndDebugUtilsLabelEXT(pCmd->pVkCmdBuf);
#else
		vkCmdDebugMarkerEndEXT(pCmd->pVkCmdBuf);
#endif

	}
}

void cmdAddDebugMarker(Cmd* pCmd, float r, float g, float b, const char* pName)
{
	if (gDebugMarkerSupport)
	{
#ifdef USE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsLabelEXT markerInfo = {};
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pLabelName = pName;
		vkCmdInsertDebugUtilsLabelEXT(pCmd->pVkCmdBuf, &markerInfo);
#else
		VkDebugMarkerMarkerInfoEXT markerInfo = {};
		markerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_MARKER_INFO_EXT;
		markerInfo.color[0] = r;
		markerInfo.color[1] = g;
		markerInfo.color[2] = b;
		markerInfo.color[3] = 1.0f;
		markerInfo.pMarkerName = pName;
		vkCmdDebugMarkerInsertEXT(pCmd->pVkCmdBuf, &markerInfo);
#endif
	}
}
/************************************************************************/
// Resource Debug Naming Interface
/************************************************************************/
void setBufferName(Renderer* pRenderer, Buffer* pBuffer, const char* pName)
{
	ASSERT(pRenderer);
	ASSERT(pBuffer);
	ASSERT(pName);

	if (gDebugMarkerSupport)
	{
#ifdef USE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
		nameInfo.objectHandle = (uint64_t)pBuffer->pVkBuffer;
		nameInfo.pObjectName = pName;
		vkSetDebugUtilsObjectNameEXT(pRenderer->pVkDevice, &nameInfo);
#else
		VkDebugMarkerObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT;
		nameInfo.object = (uint64_t)pBuffer->pVkBuffer;
		nameInfo.pObjectName = pName;
		vkDebugMarkerSetObjectNameEXT(pRenderer->pVkDevice, &nameInfo);
#endif
	}
}

void setTextureName(Renderer* pRenderer, Texture* pTexture, const char* pName)
{
	ASSERT(pRenderer);
	ASSERT(pTexture);
	ASSERT(pName);

	if (gDebugMarkerSupport)
	{
#ifdef USE_DEBUG_UTILS_EXTENSION
		VkDebugUtilsObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
		nameInfo.objectHandle = (uint64_t)pTexture->pVkImage;
		nameInfo.pObjectName = pName;
		vkSetDebugUtilsObjectNameEXT(pRenderer->pVkDevice, &nameInfo);
#else
		VkDebugMarkerObjectNameInfoEXT nameInfo = {};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
		nameInfo.object = (uint64_t)pTexture->pVkImage;
		nameInfo.pObjectName = pName;
		vkDebugMarkerSetObjectNameEXT(pRenderer->pVkDevice, &nameInfo);
#endif
	}
}

uint32_t getMemoryType(uint32_t typeBits, VkPhysicalDeviceMemoryProperties memoryProperties, VkMemoryPropertyFlags properties, VkBool32 *memTypeFound = nullptr)
{
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
	{
		if ((typeBits & 1) == 1)
		{
			if ((memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				if (memTypeFound)
				{
					*memTypeFound = true;
				}
				return i;
			}
		}
		typeBits >>= 1;
	}

	if (memTypeFound)
	{
		*memTypeFound = false;
		return 0;
	}
	else
	{
		LOGF(LogLevel::eERROR, "Could not find a matching memory type");
		ASSERT(0);
		return 0;
	}
}

/************************************************************************/
// Virtual Texture
/************************************************************************/
uvec3 alignedDivision(const VkExtent3D& extent, const VkExtent3D& granularity)
{
	uvec3 res;
	res.setX( extent.width / granularity.width + ((extent.width  % granularity.width) ? 1u : 0u));
	res.setY( extent.height / granularity.height + ((extent.height % granularity.height) ? 1u : 0u));
	res.setZ( extent.depth / granularity.depth + ((extent.depth  % granularity.depth) ? 1u : 0u));
	return res;
}

// Call before sparse binding to update memory bind list etc.
void updateSparseBindInfo(Texture* pTexture, Queue* pQueue)
{
	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;
	eastl::vector<VkSparseImageMemoryBind>* pImageMemory = (eastl::vector<VkSparseImageMemoryBind>*)pTexture->pSparseImageMemoryBinds;
	eastl::vector<VkSparseMemoryBind>* pOpaqueMemoryBinds = (eastl::vector<VkSparseMemoryBind>*)pTexture->pOpaqueMemoryBinds;

	// Update list of memory-backed sparse image memory binds
	pImageMemory->resize(pPageTable->size());
	uint32_t index = 0;
	for (int i = 0; i < (int)pPageTable->size(); i++)
	{
		(*pImageMemory)[index] = (*pPageTable)[i].imageMemoryBind;
		index++;
	}
	// Update sparse bind info
	pTexture->mBindSparseInfo = {};
	pTexture->mBindSparseInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
	
	// Image memory binds
	pTexture->mImageMemoryBindInfo = {};
	pTexture->mImageMemoryBindInfo.image = pTexture->pVkImage;
	pTexture->mImageMemoryBindInfo.bindCount = static_cast<uint32_t>(pImageMemory->size());
	pTexture->mImageMemoryBindInfo.pBinds = pImageMemory->data();
	pTexture->mBindSparseInfo.imageBindCount = (pTexture->mImageMemoryBindInfo.bindCount > 0) ? 1 : 0;
	pTexture->mBindSparseInfo.pImageBinds = &pTexture->mImageMemoryBindInfo;

	// Opaque image memory binds (mip tail)
	pTexture->mOpaqueMemoryBindInfo.image = pTexture->pVkImage;
	pTexture->mOpaqueMemoryBindInfo.bindCount = static_cast<uint32_t>(pOpaqueMemoryBinds->size());
	pTexture->mOpaqueMemoryBindInfo.pBinds = pOpaqueMemoryBinds->data();
	pTexture->mBindSparseInfo.imageOpaqueBindCount = (pTexture->mOpaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
	pTexture->mBindSparseInfo.pImageOpaqueBinds = &pTexture->mOpaqueMemoryBindInfo;
}

void releasePage(Cmd* pCmd, Renderer* pRenderer, Texture* pTexture)
{
	vkDeviceWaitIdle(pRenderer->pVkDevice);

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;
	eastl::vector<VkSparseImageMemoryBind>* pImageMemory = (eastl::vector<VkSparseImageMemoryBind>*)pTexture->pSparseImageMemoryBinds;
	eastl::vector<VkSparseMemoryBind>* pOpaqueMemoryBinds = (eastl::vector<VkSparseMemoryBind>*)pTexture->pOpaqueMemoryBinds;

	uint removePageCount;
	memcpy(&removePageCount, pTexture->mRemovePageCount->pCpuMappedAddress, sizeof(uint));

	if (removePageCount == 0)
		return;

	eastl::vector<uint32_t> RemovePageTable;
	RemovePageTable.resize(removePageCount);	

	memcpy(RemovePageTable.data(), pTexture->mRemovePage->pCpuMappedAddress, sizeof(uint));

	for (int i = 0; i < (int)removePageCount; ++i)
	{		
		uint32_t RemoveIndex = RemovePageTable[i];
		releaseVirtualPage(pRenderer, (*pPageTable)[RemoveIndex], false);
	}
}

// Fill a complete mip level
// Need to get visibility info first then fill them
void fillVirtualTexture(Cmd* pCmd, Renderer* pRenderer, Texture* pTexture, Fence* pFence)
{
	TextureBarrier barriers[] = {
					{ pTexture, RESOURCE_STATE_COPY_DEST }
	};
	cmdResourceBarrier(pCmd, 0, NULL, 1, barriers);

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;
	eastl::vector<VkSparseImageMemoryBind>* pImageMemory = (eastl::vector<VkSparseImageMemoryBind>*)pTexture->pSparseImageMemoryBinds;
	eastl::vector<VkSparseMemoryBind>* pOpaqueMemoryBinds = (eastl::vector<VkSparseMemoryBind>*)pTexture->pOpaqueMemoryBinds;

	pImageMemory->set_capacity(0);

	uint alivePageCount;
	memcpy(&alivePageCount, pTexture->mAlivePageCount->pCpuMappedAddress, sizeof(uint));

	eastl::vector<uint> VisibilityData;
	VisibilityData.resize(alivePageCount);
	memcpy(VisibilityData.data(), pTexture->mAlivePage->pCpuMappedAddress, VisibilityData.size() * sizeof(uint));

	for (int i = 0; i < (int)VisibilityData.size(); ++i)
	{
		uint pageIndex = VisibilityData[i];
		VirtualTexturePage* pPage = &(*pPageTable)[pageIndex];

		if(allocateVirtualPage(pRenderer, pTexture, *pPage, pTexture->mSparseMemoryTypeIndex))
		{
			void* pData = (void*)((unsigned char*)pTexture->mVirtualImageData + (pageIndex * pPage->size));

			memcpy(pPage->pIntermediateBuffer->pCpuMappedAddress, pData, pPage->size);

			//Copy image to VkImage	
			VkBufferImageCopy region = {};
			region.bufferOffset = 0;
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.mipLevel = pPage->mipLevel;
			region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;		
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;

			region.imageOffset = { pPage->offset.x, pPage->offset.y, 0 };
			region.imageExtent = { (uint32_t)pTexture->mSparseVirtualTexturePageWidth, (uint32_t)pTexture->mSparseVirtualTexturePageHeight, 1 };

			vkCmdCopyBufferToImage(
				pCmd->pVkCmdBuf,
				pPage->pIntermediateBuffer->pVkBuffer,
				pTexture->pVkImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&region);

			// Update list of memory-backed sparse image memory binds
			pImageMemory->push_back(pPage->imageMemoryBind);
		}		
	}

	// Update sparse bind info
	if(pImageMemory->size() > 0)
	{
		pTexture->mBindSparseInfo = {};
		pTexture->mBindSparseInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;

		// Image memory binds
		pTexture->mImageMemoryBindInfo = {};
		pTexture->mImageMemoryBindInfo.image = pTexture->pVkImage;
		pTexture->mImageMemoryBindInfo.bindCount = static_cast<uint32_t>(pImageMemory->size());
		pTexture->mImageMemoryBindInfo.pBinds = pImageMemory->data();
		pTexture->mBindSparseInfo.imageBindCount = (pTexture->mImageMemoryBindInfo.bindCount > 0) ? 1 : 0;
		pTexture->mBindSparseInfo.pImageBinds = &pTexture->mImageMemoryBindInfo;

		// Opaque image memory binds (mip tail)
		pTexture->mOpaqueMemoryBindInfo.image = pTexture->pVkImage;
		pTexture->mOpaqueMemoryBindInfo.bindCount = static_cast<uint32_t>(pOpaqueMemoryBinds->size());
		pTexture->mOpaqueMemoryBindInfo.pBinds = pOpaqueMemoryBinds->data();
		pTexture->mBindSparseInfo.imageOpaqueBindCount = (pTexture->mOpaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
		pTexture->mBindSparseInfo.pImageOpaqueBinds = &pTexture->mOpaqueMemoryBindInfo;

		VkResult  vk_res = vkQueueBindSparse(pCmd->pCmdPool->pQueue->pVkQueue, (uint32_t)1, &pTexture->mBindSparseInfo, VK_NULL_HANDLE);
		ASSERT(VK_SUCCESS == vk_res);
	}
}

// Fill specific mipLevel
void fillVirtualTextureLevel(Cmd* pCmd, Renderer* pRenderer, Texture* pTexture, uint32_t mipLevel)
{
	vkDeviceWaitIdle(pRenderer->pVkDevice);

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;

	//Bind data
	eastl::vector<VkSparseImageMemoryBind>* pImageMemory = (eastl::vector<VkSparseImageMemoryBind>*)pTexture->pSparseImageMemoryBinds;
	eastl::vector<VkSparseMemoryBind>* pOpaqueMemoryBinds = (eastl::vector<VkSparseMemoryBind>*)pTexture->pOpaqueMemoryBinds;

	for (int i = 0; i < (int)pPageTable->size(); i++)
	{
		VirtualTexturePage* pPage = &(*pPageTable)[i];
		uint32_t pageIndex = pPage->index;
		int globalOffset = 0;

		if ((pPage->mipLevel == mipLevel) && (pPage->imageMemoryBind.memory == VK_NULL_HANDLE))
		{
			if (allocateVirtualPage(pRenderer, pTexture, *pPage, pTexture->mSparseMemoryTypeIndex))
			{
				void* pData = (void*)((unsigned char*)pTexture->mVirtualImageData + (pageIndex * (uint32_t)pPage->size));

				//CPU to GPU
				memcpy(pPage->pIntermediateBuffer->pCpuMappedAddress, pData, pPage->size);

				//Copy image to VkImage	
				VkBufferImageCopy region = {};
				region.bufferOffset = 0;
				region.bufferRowLength = 0;
				region.bufferImageHeight = 0;

				region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				region.imageSubresource.mipLevel = mipLevel;
				region.imageSubresource.baseArrayLayer = 0;
				region.imageSubresource.layerCount = 1;

				region.imageOffset = { pPage->offset.x, pPage->offset.y, 0 };
				region.imageExtent = { (uint32_t)pTexture->mSparseVirtualTexturePageWidth, (uint32_t)pTexture->mSparseVirtualTexturePageHeight, 1 };

				vkCmdCopyBufferToImage(
					pCmd->pVkCmdBuf,
					pPage->pIntermediateBuffer->pVkBuffer,
					pTexture->pVkImage,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&region);
			}			
			// Update list of memory-backed sparse image memory binds
			pImageMemory->push_back(pPage->imageMemoryBind);
		}
	}

	// Update sparse bind info
	{
		pTexture->mBindSparseInfo = {};
		pTexture->mBindSparseInfo.sType = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;

		// Image memory binds
		pTexture->mImageMemoryBindInfo = {};
		pTexture->mImageMemoryBindInfo.image = pTexture->pVkImage;
		pTexture->mImageMemoryBindInfo.bindCount = static_cast<uint32_t>(pImageMemory->size());
		pTexture->mImageMemoryBindInfo.pBinds = pImageMemory->data();
		pTexture->mBindSparseInfo.imageBindCount = (pTexture->mImageMemoryBindInfo.bindCount > 0) ? 1 : 0;
		pTexture->mBindSparseInfo.pImageBinds = &pTexture->mImageMemoryBindInfo;

		// Opaque image memory binds (mip tail)
		pTexture->mOpaqueMemoryBindInfo.image = pTexture->pVkImage;
		pTexture->mOpaqueMemoryBindInfo.bindCount = static_cast<uint32_t>(pOpaqueMemoryBinds->size());
		pTexture->mOpaqueMemoryBindInfo.pBinds = pOpaqueMemoryBinds->data();
		pTexture->mBindSparseInfo.imageOpaqueBindCount = (pTexture->mOpaqueMemoryBindInfo.bindCount > 0) ? 1 : 0;
		pTexture->mBindSparseInfo.pImageOpaqueBinds = &pTexture->mOpaqueMemoryBindInfo;

		VkResult  vk_res = vkQueueBindSparse(pCmd->pCmdPool->pQueue->pVkQueue, (uint32_t)1, &pTexture->mBindSparseInfo, VK_NULL_HANDLE);
		ASSERT(VK_SUCCESS == vk_res);
	}
}

void addVirtualTexture(Renderer * pRenderer, const TextureDesc * pDesc, Texture ** ppTexture, void* pImageData)
{
	ASSERT(pRenderer);
	Texture* pTexture = (Texture*)conf_calloc(1, sizeof(*pTexture));
	ASSERT(pTexture);

	uint32_t imageSize = 0;
	uint32_t mipSize = pDesc->mWidth * pDesc->mHeight * pDesc->mDepth;
	while (mipSize > 0)
	{
		imageSize += mipSize;
		mipSize /= 4;
	}

	pTexture->mVirtualImageData = (char*)conf_malloc(imageSize * sizeof(uint32_t));
	memcpy(pTexture->mVirtualImageData, pImageData, imageSize * sizeof(uint32_t));

	// Create command buffer to transition resources to the correct state
	Queue*   graphicsQueue = NULL;
	CmdPool* cmdPool = NULL;
	Cmd*     cmd = NULL;

	QueueDesc queueDesc = {};
	queueDesc.mType = CMD_POOL_DIRECT;
	addQueue(pRenderer, &queueDesc, &graphicsQueue);

	addCmdPool(pRenderer, graphicsQueue, false, &cmdPool);
	addCmd(cmdPool, false, &cmd);

	// Transition resources
	beginCmd(cmd);

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	pTexture->mDesc = *pDesc;
	pTexture->mOwnsImage = true;

	VkImageCreateInfo add_info = {};
	add_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	add_info.flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT | VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
	add_info.imageType = VK_IMAGE_TYPE_2D;
	add_info.format = format;
	add_info.extent.width = pDesc->mWidth;
	add_info.extent.height = pDesc->mHeight;
	add_info.extent.depth = pDesc->mDepth;
	add_info.mipLevels = pDesc->mMipLevels;
	add_info.arrayLayers = 1;
	add_info.samples = VK_SAMPLE_COUNT_1_BIT;
	add_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	add_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	add_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	add_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;	

	VkResult vk_res = (VkResult)vkCreateImage(pRenderer->pVkDevice, &add_info, nullptr, &pTexture->pVkImage);	
	assert(vk_res == VK_SUCCESS);

	// Get memory requirements
	VkMemoryRequirements sparseImageMemoryReqs;
	// Sparse image memory requirement counts
	vkGetImageMemoryRequirements(pRenderer->pVkDevice, pTexture->pVkImage, &sparseImageMemoryReqs);

	// Check requested image size against hardware sparse limit
	if (sparseImageMemoryReqs.size > pRenderer->mVkGpuProperties[0].properties.limits.sparseAddressSpaceSize)
	{
		LOGF(LogLevel::eERROR, "Requested sparse image size exceeds supportes sparse address space size!");
		return;
	}

	// Get sparse memory requirements
	// Count
	uint32_t sparseMemoryReqsCount = 32;
	eastl::vector<VkSparseImageMemoryRequirements> sparseMemoryReqs(sparseMemoryReqsCount);
	vkGetImageSparseMemoryRequirements(pRenderer->pVkDevice, pTexture->pVkImage, &sparseMemoryReqsCount, sparseMemoryReqs.data());
	
	if (sparseMemoryReqsCount == 0)
	{
		LOGF(LogLevel::eERROR, "No memory requirements for the sparse image!");
		return;
	}
	sparseMemoryReqs.resize(sparseMemoryReqsCount);

	// Get actual requirements
	vkGetImageSparseMemoryRequirements(pRenderer->pVkDevice, pTexture->pVkImage, &sparseMemoryReqsCount, sparseMemoryReqs.data());

	pTexture->mSparseVirtualTexturePageWidth = sparseMemoryReqs[0].formatProperties.imageGranularity.width;
	pTexture->mSparseVirtualTexturePageHeight = sparseMemoryReqs[0].formatProperties.imageGranularity.height;

	LOGF(LogLevel::eINFO, "Sparse image memory requirements: %d", sparseMemoryReqsCount);

	for (int i = 0; i< (int)sparseMemoryReqs.size(); ++i)
	{
		VkSparseImageMemoryRequirements reqs = sparseMemoryReqs[i];
		//todo:multiple reqs
		pTexture->mMipTailStart = reqs.imageMipTailFirstLod;
	}

	pTexture->mLastFilledMip = pTexture->mMipTailStart - 1;

	// Get sparse image requirements for the color aspect
	VkSparseImageMemoryRequirements sparseMemoryReq;
	bool colorAspectFound = false;
	for (int i = 0; i < (int)sparseMemoryReqs.size(); ++i)
	{
		VkSparseImageMemoryRequirements reqs = sparseMemoryReqs[i];

		if (reqs.formatProperties.aspectMask & VK_IMAGE_ASPECT_COLOR_BIT)
		{
			sparseMemoryReq = reqs;
			colorAspectFound = true;
			break;
		}
	}
	if (!colorAspectFound)
	{
		LOGF(LogLevel::eERROR, "Could not find sparse image memory requirements for color aspect bit!");
		return;
	}

	// todo:
	// Calculate number of required sparse memory bindings by alignment
	assert((sparseImageMemoryReqs.size % sparseImageMemoryReqs.alignment) == 0);
	pTexture->mSparseMemoryTypeIndex = getMemoryType(sparseImageMemoryReqs.memoryTypeBits, pRenderer->mVkGpuMemoryProperties[0], VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Get sparse bindings
	uint32_t sparseBindsCount = static_cast<uint32_t>(sparseImageMemoryReqs.size / sparseImageMemoryReqs.alignment);
	eastl::vector<VkSparseMemoryBind>	sparseMemoryBinds(sparseBindsCount);

	// Check if the format has a single mip tail for all layers or one mip tail for each layer
	// The mip tail contains all mip levels > sparseMemoryReq.imageMipTailFirstLod
	bool singleMipTail = sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT;

	pTexture->pPages = (eastl::vector<VirtualTexturePage>*)conf_calloc(1, sizeof(eastl::vector<VirtualTexturePage>));
	pTexture->pSparseImageMemoryBinds = (eastl::vector<VkSparseImageMemoryBind>*)conf_calloc(1, sizeof(eastl::vector<VkSparseImageMemoryBind>));
	pTexture->pOpaqueMemoryBinds = (eastl::vector<VkSparseMemoryBind>*)conf_calloc(1, sizeof(eastl::vector<VkSparseMemoryBind>));

	eastl::vector<VkSparseMemoryBind>* pOpaqueMemoryBinds = (eastl::vector<VkSparseMemoryBind>*)pTexture->pOpaqueMemoryBinds;

	// Sparse bindings for each mip level of all layers outside of the mip tail
	for (uint32_t layer = 0; layer < 1; layer++)
	{
		// sparseMemoryReq.imageMipTailFirstLod is the first mip level that's stored inside the mip tail
		for (uint32_t mipLevel = 0; mipLevel < sparseMemoryReq.imageMipTailFirstLod; mipLevel++)
		{
			VkExtent3D extent;
			extent.width	= max(add_info.extent.width >> mipLevel, 1u);
			extent.height = max(add_info.extent.height >> mipLevel, 1u);
			extent.depth	= max(add_info.extent.depth >> mipLevel, 1u);

			VkImageSubresource subResource{};
			subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subResource.mipLevel = mipLevel;
			subResource.arrayLayer = layer;

			// Aligned sizes by image granularity
			VkExtent3D imageGranularity = sparseMemoryReq.formatProperties.imageGranularity;
			uvec3 sparseBindCounts = alignedDivision(extent, imageGranularity);
			uvec3 lastBlockExtent;
			lastBlockExtent.setX((extent.width % imageGranularity.width) ? extent.width % imageGranularity.width : imageGranularity.width);
			lastBlockExtent.setY((extent.height % imageGranularity.height) ? extent.height % imageGranularity.height : imageGranularity.height);
			lastBlockExtent.setZ((extent.depth % imageGranularity.depth) ? extent.depth % imageGranularity.depth : imageGranularity.depth);

			// Alllocate memory for some blocks
			uint32_t index = 0;
			for (uint32_t z = 0; z < sparseBindCounts.getZ(); z++)
			{
				for (uint32_t y = 0; y < sparseBindCounts.getY(); y++)
				{
					for (uint32_t x = 0; x < sparseBindCounts.getX(); x++)
					{
						// Offset 
						VkOffset3D offset;
						offset.x = x * imageGranularity.width;
						offset.y = y * imageGranularity.height;
						offset.z = z * imageGranularity.depth;
						// Size of the page
						VkExtent3D extent;
						extent.width = (x == sparseBindCounts.getX() - 1) ? lastBlockExtent.getX() : imageGranularity.width;
						extent.height = (y == sparseBindCounts.getY() - 1) ? lastBlockExtent.getY() : imageGranularity.height;
						extent.depth = (z == sparseBindCounts.getZ() - 1) ? lastBlockExtent.getZ() : imageGranularity.depth;

						// Add new virtual page
						VirtualTexturePage *newPage = addPage(pRenderer, pTexture, offset, extent, pTexture->mSparseVirtualTexturePageWidth * pTexture->mSparseVirtualTexturePageHeight * sizeof(uint), mipLevel, layer);
						newPage->imageMemoryBind.subresource = subResource;

						index++;
					}
				}
			}
		}

		// Check if format has one mip tail per layer
		if ((!singleMipTail) && (sparseMemoryReq.imageMipTailFirstLod < pTexture->mDesc.mMipLevels))
		{
			// Allocate memory for the mip tail
			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = sparseMemoryReq.imageMipTailSize;
			allocInfo.memoryTypeIndex = pTexture->mSparseMemoryTypeIndex;

			VkDeviceMemory deviceMemory;
			vk_res = vkAllocateMemory(pRenderer->pVkDevice, &allocInfo, nullptr, &deviceMemory);
			assert(vk_res == VK_SUCCESS);

			// (Opaque) sparse memory binding
			VkSparseMemoryBind sparseMemoryBind{};
			sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset + layer * sparseMemoryReq.imageMipTailStride;
			sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
			sparseMemoryBind.memory = deviceMemory;

			pOpaqueMemoryBinds->push_back(sparseMemoryBind);
		}
	} // end layers and mips

	LOGF(LogLevel::eINFO, "Virtual Texture info: Dim %d x %d Pages %d", pTexture->mDesc.mWidth, pTexture->mDesc.mHeight, (uint32_t)(((eastl::vector<VirtualTexturePage>*)pTexture->pPages)->size()));

	// Check if format has one mip tail for all layers
	if ((sparseMemoryReq.formatProperties.flags & VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT) && (sparseMemoryReq.imageMipTailFirstLod < pTexture->mDesc.mMipLevels))
	{
		// Allocate memory for the mip tail
		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = sparseMemoryReq.imageMipTailSize;
		allocInfo.memoryTypeIndex = pTexture->mSparseMemoryTypeIndex;

		VkDeviceMemory deviceMemory;
		vk_res = vkAllocateMemory(pRenderer->pVkDevice, &allocInfo, nullptr, &deviceMemory);
		assert(vk_res == VK_SUCCESS);

		// (Opaque) sparse memory binding
		VkSparseMemoryBind sparseMemoryBind{};
		sparseMemoryBind.resourceOffset = sparseMemoryReq.imageMipTailOffset;
		sparseMemoryBind.size = sparseMemoryReq.imageMipTailSize;
		sparseMemoryBind.memory = deviceMemory;

		pOpaqueMemoryBinds->push_back(sparseMemoryBind);
	}

	pTexture->mLastFilledMip = pTexture->mMipTailStart - 1;

	/************************************************************************/
	// Create image view
	/************************************************************************/
	VkImageViewCreateInfo view = {};
	view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view.image = pTexture->pVkImage;
	view.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view.format = format;
	view.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };
	view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	view.subresourceRange.baseMipLevel = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount = 1;
	view.subresourceRange.levelCount = pDesc->mMipLevels;
	view.image = pTexture->pVkImage;
	pTexture->mVkAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	vk_res = vkCreateImageView(pRenderer->pVkDevice, &view, NULL, &pTexture->pVkSRVDescriptor);
	ASSERT(VK_SUCCESS == vk_res);

	eastl::vector<TextureBarrier> textureBarriers;

	textureBarriers.push_back(TextureBarrier{ pTexture, RESOURCE_STATE_COPY_DEST });
	uint32_t textureBarrierCount = (uint32_t)textureBarriers.size();
	cmdResourceBarrier(cmd, 0, NULL, textureBarrierCount, textureBarriers.data());

	// Fill smallest (non-tail) mip map level
	fillVirtualTextureLevel(cmd, pRenderer, pTexture, pTexture->mLastFilledMip);

	endCmd(cmd);

	queueSubmit(graphicsQueue, 1, &cmd, NULL, 0, NULL, 0, NULL);
	waitQueueIdle(graphicsQueue);

	// Delete command buffer
	removeCmd(cmdPool, cmd);
	removeCmdPool(pRenderer, cmdPool);
	removeQueue(graphicsQueue);

	*ppTexture = pTexture;
}

void updateVirtualTexture(Renderer* pRenderer, Queue* pQueue, Texture* pTexture)
{
	if (pTexture->mVisibility)
	{
		// Create command buffer to transition resources to the correct state		
		CmdPool* cmdPool = NULL;
		Cmd*     cmd = NULL;

		addCmdPool(pRenderer, pQueue, false, &cmdPool);
		addCmd(cmdPool, false, &cmd);

		// Transition resources
		beginCmd(cmd);

		releasePage(cmd, pRenderer, pTexture);
		fillVirtualTexture(cmd, pRenderer, pTexture, NULL);		

		endCmd(cmd);

		queueSubmit(pQueue, 1, &cmd, NULL, 0, NULL, 0, NULL);
		waitQueueIdle(pQueue);

		// Delete command buffer
		removeCmd(cmdPool, cmd);
		removeCmdPool(pRenderer, cmdPool);	

		uint alivePageCount[4] = { 0, 0, 0, 0 };
		memcpy(pTexture->mAlivePageCount->pCpuMappedAddress, alivePageCount, sizeof(uint) * 4);
		memcpy(pTexture->mRemovePageCount->pCpuMappedAddress, alivePageCount, sizeof(uint) * 4);
	}	
}

#endif
#include "../../../Common_3/ThirdParty/OpenSource/volk/volk.c"
#if defined(VK_USE_DISPATCH_TABLES)
#include "../../../Common_3/ThirdParty/OpenSource/volk/volkForgeExt.c"
#endif
#endif
