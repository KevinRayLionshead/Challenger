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

#ifdef DIRECT3D12

#define RENDERER_IMPLEMENTATION

#ifdef _DURANGO
#include "..\..\..\Xbox\Common_3\Renderer\XBoxPrivateHeaders.h"
#else
#define IID_ARGS IID_PPV_ARGS
#endif

// Pull in minimal Windows headers
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include "../IRenderer.h"

#include "../../ThirdParty/OpenSource/EASTL/sort.h"
#include "../../ThirdParty/OpenSource/EASTL/string.h"
#include "../../ThirdParty/OpenSource/EASTL/unordered_map.h"
#include "../../ThirdParty/OpenSource/EASTL/string_hash_map.h"

#include "../../ThirdParty/OpenSource/winpixeventruntime/Include/WinPixEventRuntime/pix3.h"

#include "../../ThirdParty/OpenSource/renderdoc/renderdoc_app.h"

#include "../../ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"
#include "../../ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"

#include "../../OS/Interfaces/ILog.h"
#include "../../OS/Core/RingBuffer.h"
#include "../../OS/Core/GPUConfig.h"

#include "Direct3D12CapBuilder.h"
#include "Direct3D12Hooks.h"
#include "Direct3D12MemoryAllocator.h"

#if !defined(_WIN32)
#error "Windows is needed!"
#endif

//
// C++ is the only language supported by D3D12:
//   https://msdn.microsoft.com/en-us/library/windows/desktop/dn899120(v=vs.85).aspx
//
#if !defined(__cplusplus)
#error "D3D12 requires C++! Sorry!"
#endif


#if !defined(_DURANGO)
// Prefer Higher Performance GPU on switchable GPU systems
extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 1;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#include "../../OS/Interfaces/IMemory.h"

#define D3D12_GPU_VIRTUAL_ADDRESS_NULL ((D3D12_GPU_VIRTUAL_ADDRESS)0)
#define D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN ((D3D12_GPU_VIRTUAL_ADDRESS)-1)

extern void
			d3d12_createShaderReflection(const uint8_t* shaderCode, uint32_t shaderSize, ShaderStage shaderStage, ShaderReflection* pOutReflection);
extern long d3d12_createBuffer(
	MemoryAllocator* pAllocator, const BufferCreateInfo* pCreateInfo, const AllocatorMemoryRequirements* pMemoryRequirements,
	Buffer* pBuffer);
extern void d3d12_destroyBuffer(MemoryAllocator* pAllocator, struct Buffer* pBuffer);
extern long d3d12_createTexture(
	MemoryAllocator* pAllocator, const TextureCreateInfo* pCreateInfo, const AllocatorMemoryRequirements* pMemoryRequirements,
	Texture* pTexture);
extern void d3d12_destroyTexture(MemoryAllocator* pAllocator, struct Texture* pTexture);

//stubs for durango because Direct3D12Raytracing.cpp is not used on XBOX
#if defined(ENABLE_RAYTRACING)
extern void d3d12_addRaytracingPipeline(const RaytracingPipelineDesc* pDesc, Pipeline** ppPipeline);
extern void d3d12_fillRaytracingDescriptorHandle(AccelerationStructure* pAccelerationStructure, uint64_t* pHandle);
extern void d3d12_cmdBindRaytracingPipeline(Cmd* pCmd, Pipeline* pPipeline);
#endif

// clang-format off
D3D12_BLEND_OP gDx12BlendOpTranslator[BlendMode::MAX_BLEND_MODES] =
{
	D3D12_BLEND_OP_ADD,
	D3D12_BLEND_OP_SUBTRACT,
	D3D12_BLEND_OP_REV_SUBTRACT,
	D3D12_BLEND_OP_MIN,
	D3D12_BLEND_OP_MAX,
};

D3D12_BLEND gDx12BlendConstantTranslator[BlendConstant::MAX_BLEND_CONSTANTS] =
{
	D3D12_BLEND_ZERO,
	D3D12_BLEND_ONE,
	D3D12_BLEND_SRC_COLOR,
	D3D12_BLEND_INV_SRC_COLOR,
	D3D12_BLEND_DEST_COLOR,
	D3D12_BLEND_INV_DEST_COLOR,
	D3D12_BLEND_SRC_ALPHA,
	D3D12_BLEND_INV_SRC_ALPHA,
	D3D12_BLEND_DEST_ALPHA,
	D3D12_BLEND_INV_DEST_ALPHA,
	D3D12_BLEND_SRC_ALPHA_SAT,
	D3D12_BLEND_BLEND_FACTOR,
	D3D12_BLEND_INV_BLEND_FACTOR,
};

D3D12_COMPARISON_FUNC gDx12ComparisonFuncTranslator[CompareMode::MAX_COMPARE_MODES] =
{
	D3D12_COMPARISON_FUNC_NEVER,
	D3D12_COMPARISON_FUNC_LESS,
	D3D12_COMPARISON_FUNC_EQUAL,
	D3D12_COMPARISON_FUNC_LESS_EQUAL,
	D3D12_COMPARISON_FUNC_GREATER,
	D3D12_COMPARISON_FUNC_NOT_EQUAL,
	D3D12_COMPARISON_FUNC_GREATER_EQUAL,
	D3D12_COMPARISON_FUNC_ALWAYS,
};

D3D12_STENCIL_OP gDx12StencilOpTranslator[StencilOp::MAX_STENCIL_OPS] =
{
	D3D12_STENCIL_OP_KEEP,
	D3D12_STENCIL_OP_ZERO,
	D3D12_STENCIL_OP_REPLACE,
	D3D12_STENCIL_OP_INVERT,
	D3D12_STENCIL_OP_INCR,
	D3D12_STENCIL_OP_DECR,
	D3D12_STENCIL_OP_INCR_SAT,
	D3D12_STENCIL_OP_DECR_SAT,
};

D3D12_CULL_MODE gDx12CullModeTranslator[MAX_CULL_MODES] =
{
	D3D12_CULL_MODE_NONE,
	D3D12_CULL_MODE_BACK,
	D3D12_CULL_MODE_FRONT,
};

D3D12_FILL_MODE gDx12FillModeTranslator[MAX_FILL_MODES] =
{
	D3D12_FILL_MODE_SOLID,
	D3D12_FILL_MODE_WIREFRAME,
};

const D3D12_COMMAND_LIST_TYPE gDx12CmdTypeTranslator[CmdPoolType::MAX_CMD_TYPE] =
{
	D3D12_COMMAND_LIST_TYPE_DIRECT,
	D3D12_COMMAND_LIST_TYPE_BUNDLE,
	D3D12_COMMAND_LIST_TYPE_COPY,
	D3D12_COMMAND_LIST_TYPE_COMPUTE
};

const D3D12_COMMAND_QUEUE_FLAGS gDx12QueueFlagTranslator[QueueFlag::MAX_QUEUE_FLAG] =
{
	D3D12_COMMAND_QUEUE_FLAG_NONE,
	D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT
};

const D3D12_COMMAND_QUEUE_PRIORITY gDx12QueuePriorityTranslator[QueuePriority::MAX_QUEUE_PRIORITY]
{
	D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
	D3D12_COMMAND_QUEUE_PRIORITY_HIGH,
#ifndef _DURANGO
	D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME,
#endif
};
// clang-format on

// =================================================================================================
// IMPLEMENTATION
// =================================================================================================

#if defined(RENDERER_IMPLEMENTATION)

#ifndef _DURANGO
#include "../../ThirdParty/OpenSource/DirectXShaderCompiler/dxcapi.use.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#endif

#define SAFE_FREE(p_var)  \
	if (p_var)            \
	{                     \
		conf_free(p_var); \
	}

#if defined(__cplusplus)
#define DECLARE_ZERO(type, var) type var = {};
#else
#define DECLARE_ZERO(type, var) type var = { 0 };
#endif

#define SAFE_RELEASE(p_var) \
	if (p_var)              \
	{                       \
		p_var->Release();   \
		p_var = NULL;       \
	}

// Internal utility functions (may become external one day)
uint64_t                    util_dx_determine_storage_counter_offset(uint64_t buffer_size);
DXGI_FORMAT                 util_to_dx_uav_format(DXGI_FORMAT defaultFormat);
DXGI_FORMAT                 util_to_dx_dsv_format(DXGI_FORMAT defaultFormat);
DXGI_FORMAT                 util_to_dx_srv_format(DXGI_FORMAT defaultFormat);
DXGI_FORMAT                 util_to_dx_stencil_format(DXGI_FORMAT defaultFormat);
DXGI_FORMAT                 util_to_dx_swapchain_format(TinyImageFormat format);
D3D12_SHADER_VISIBILITY     util_to_dx_shader_visibility(ShaderStage stages);
D3D12_DESCRIPTOR_RANGE_TYPE util_to_dx_descriptor_range(DescriptorType type);
D3D12_RESOURCE_STATES       util_to_dx_resource_state(ResourceState state);
D3D12_FILTER util_to_dx_filter(FilterType minFilter, FilterType magFilter, MipMapMode mipMapMode, bool aniso, bool comparisonFilterEnabled);
D3D12_TEXTURE_ADDRESS_MODE    util_to_dx_texture_address_mode(AddressMode addressMode);
D3D12_PRIMITIVE_TOPOLOGY_TYPE util_to_dx_primitive_topology_type(PrimitiveTopology topology);

//
// internal functions start with a capital letter / API starts with a small letter

// internal functions are capital first letter and capital letter of the next word

// Functions points for functions that need to be loaded
PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER           fnD3D12CreateRootSignatureDeserializer = NULL;
PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE           fnD3D12SerializeVersionedRootSignature = NULL;
PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER fnD3D12CreateVersionedRootSignatureDeserializer = NULL;

// Declare hooks for platform specific behavior
PFN_HOOK_ADD_DESCRIPTIOR_HEAP              fnHookAddDescriptorHeap = NULL;
PFN_HOOK_POST_INIT_RENDERER                fnHookPostInitRenderer = NULL;
PFN_HOOK_POST_REMOVE_RENDERER              fnHookPostRemoveRenderer = NULL;
PFN_HOOK_ADD_BUFFER                        fnHookAddBuffer = NULL;
PFN_HOOK_ENABLE_DEBUG_LAYER                fnHookEnableDebugLayer = NULL;
PFN_HOOK_HEAP_DESC                         fnHookHeapDesc = NULL;
PFN_HOOK_GET_RECOMMENDED_SWAP_CHAIN_FORMAT fnHookGetRecommendedSwapChainFormat = NULL;
PFN_HOOK_MODIFY_SWAP_CHAIN_DESC            fnHookModifySwapChainDesc = NULL;
PFN_HOOK_GET_SWAP_CHAIN_IMAGE_INDEX        fnHookGetSwapChainImageIndex = NULL;
PFN_HOOK_SHADER_COMPILE_FLAGS              fnHookShaderCompileFlags = NULL;
PFN_HOOK_RESOURCE_ALLOCATION_INFO          fnHookResourceAllocationInfo = NULL;
PFN_HOOK_SPECIAL_BUFFER_ALLOCATION         fnHookSpecialBufferAllocation = NULL;
PFN_HOOK_SPECIAL_TEXTURE_ALLOCATION        fnHookSpecialTextureAllocation = NULL;
PFN_HOOK_RESOURCE_FLAGS                    fnHookResourceFlags = NULL;
/************************************************************************/
// Descriptor Heap Defines
/************************************************************************/
typedef struct DescriptorHeapProperties
{
	uint32_t                    mMaxDescriptors;
	D3D12_DESCRIPTOR_HEAP_FLAGS mFlags;
} DescriptorHeapProperties;

DescriptorHeapProperties gCpuDescriptorHeapProperties[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] =
{
	{ 1024 * 256, D3D12_DESCRIPTOR_HEAP_FLAG_NONE },    // CBV SRV UAV
	{ 2048, D3D12_DESCRIPTOR_HEAP_FLAG_NONE },          // Sampler
	{ 512, D3D12_DESCRIPTOR_HEAP_FLAG_NONE },           // RTV
	{ 512, D3D12_DESCRIPTOR_HEAP_FLAG_NONE },           // DSV
};

/************************************************************************/
// Descriptor Heap Structures
/************************************************************************/
/// CPU Visible Heap to store all the resources needing CPU read / write operations - Textures/Buffers/RTV
typedef struct DescriptorHeap
{
	typedef struct DescriptorHandle
	{
		D3D12_CPU_DESCRIPTOR_HANDLE mCpu;
		D3D12_GPU_DESCRIPTOR_HANDLE mGpu;
	} DescriptorHandle;

	/// DX Heap
	ID3D12DescriptorHeap*           pCurrentHeap;
	/// Lock for multi-threaded descriptor allocations
	Mutex*                          pMutex;
	ID3D12Device*                   pDevice;
	D3D12_CPU_DESCRIPTOR_HANDLE*    pHandles;
	/// Start position in the heap
	DescriptorHandle                mStartHandle;
	/// Free List used for CPU only descriptor heaps
	eastl::vector<DescriptorHandle> mFreeList;
	/// Description
	D3D12_DESCRIPTOR_HEAP_DESC      mDesc;
	/// DescriptorInfo Increment Size
	uint32_t                        mDescriptorSize;
	/// Used
	tfrg_atomic32_t                 mUsedDescriptors;
} DescriptorHeap;

typedef struct DescriptorIndexMap
{
	eastl::string_hash_map<uint32_t> mMap;
} DescriptorIndexMap;

typedef struct DescriptorSet
{
	uint64_t*                   pCbvSrvUavHandles;
	uint64_t*                   pSamplerHandles;
	const RootSignature*        pRootSignature;
	D3D12_GPU_VIRTUAL_ADDRESS** pRootAddresses;
	uint16_t                    mMaxSets;
	uint8_t                     mUpdateFrequency;
	uint8_t                     mNodeIndex;
	uint8_t                     mRootAddressCount;
	uint8_t                     mCbvSrvUavRootIndex;
	uint8_t                     mSamplerRootIndex;
} DescriptorSet;
/************************************************************************/
// Static Descriptor Heap Implementation
/************************************************************************/
static void add_descriptor_heap(ID3D12Device* pDevice, const D3D12_DESCRIPTOR_HEAP_DESC* pDesc, DescriptorHeap** ppDescHeap)
{
	uint32_t numDescriptors = pDesc->NumDescriptors;
	if (fnHookAddDescriptorHeap != NULL)
		numDescriptors = fnHookAddDescriptorHeap(pDesc->Type, numDescriptors);

	DescriptorHeap* pHeap = (DescriptorHeap*)conf_calloc(1, sizeof(*pHeap));

	pHeap->pMutex = (Mutex*)conf_calloc(1, sizeof(Mutex));
	pHeap->pMutex->Init();
	pHeap->pDevice = pDevice;

	// Keep 32 aligned for easy remove
	numDescriptors = round_up(numDescriptors, 32);

	D3D12_DESCRIPTOR_HEAP_DESC Desc = *pDesc;
	Desc.NumDescriptors = numDescriptors;

	pHeap->mDesc = Desc;

	HRESULT hres = pDevice->CreateDescriptorHeap(&Desc, IID_ARGS(&pHeap->pCurrentHeap));
	ASSERT(SUCCEEDED(hres));

	pHeap->mStartHandle.mCpu = pHeap->pCurrentHeap->GetCPUDescriptorHandleForHeapStart();
	pHeap->mStartHandle.mGpu = pHeap->pCurrentHeap->GetGPUDescriptorHandleForHeapStart();
	pHeap->mDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(pHeap->mDesc.Type);
	if (Desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
		pHeap->pHandles = (D3D12_CPU_DESCRIPTOR_HANDLE*)conf_calloc(Desc.NumDescriptors, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));

	*ppDescHeap = pHeap;
}

/// Resets the CPU Handle to start of heap and clears all stored resource ids
static void reset_descriptor_heap(DescriptorHeap* pHeap)
{
	pHeap->mUsedDescriptors = 0;
	pHeap->mFreeList.clear();
}

static void remove_descriptor_heap(DescriptorHeap* pHeap)
{
	SAFE_RELEASE(pHeap->pCurrentHeap);

	// Need delete since object frees allocated memory in destructor
	pHeap->pMutex->Destroy();
	conf_free(pHeap->pMutex);

	pHeap->mFreeList.~vector();

	SAFE_FREE(pHeap->pHandles);
	SAFE_FREE(pHeap);
}

static DescriptorHeap::DescriptorHandle consume_descriptor_handles(DescriptorHeap* pHeap, uint32_t descriptorCount)
{
	if (pHeap->mUsedDescriptors + descriptorCount > pHeap->mDesc.NumDescriptors)
	{
		MutexLock lock(*pHeap->pMutex);

		if ((pHeap->mDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))
		{
			uint32_t currentOffset = pHeap->mUsedDescriptors;
			D3D12_DESCRIPTOR_HEAP_DESC desc = pHeap->mDesc;
			while(pHeap->mUsedDescriptors + descriptorCount > desc.NumDescriptors)
				desc.NumDescriptors <<= 1;
			ID3D12Device* pDevice = pHeap->pDevice;
			SAFE_RELEASE(pHeap->pCurrentHeap);
			pDevice->CreateDescriptorHeap(&desc, IID_ARGS(&pHeap->pCurrentHeap));
			pHeap->mDesc = desc;
			pHeap->mStartHandle.mCpu = pHeap->pCurrentHeap->GetCPUDescriptorHandleForHeapStart();
			pHeap->mStartHandle.mGpu = pHeap->pCurrentHeap->GetGPUDescriptorHandleForHeapStart();

			uint32_t* rangeSizes = (uint32_t*)alloca(pHeap->mUsedDescriptors * sizeof(uint32_t));
			uint32_t usedDescriptors = tfrg_atomic32_load_relaxed(&pHeap->mUsedDescriptors);
			for (uint32_t i = 0; i < pHeap->mUsedDescriptors; ++i)
				rangeSizes[i] = 1;
			pDevice->CopyDescriptors(1, &pHeap->mStartHandle.mCpu, &usedDescriptors,
				pHeap->mUsedDescriptors, pHeap->pHandles, rangeSizes,
				pHeap->mDesc.Type);
			D3D12_CPU_DESCRIPTOR_HANDLE* pNewHandles = (D3D12_CPU_DESCRIPTOR_HANDLE*)conf_calloc(pHeap->mDesc.NumDescriptors, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
			memcpy(pNewHandles, pHeap->pHandles, pHeap->mUsedDescriptors * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
			SAFE_FREE(pHeap->pHandles);
			pHeap->pHandles = pNewHandles;
		}
		else if (descriptorCount == 1 && pHeap->mFreeList.size())
		{
			DescriptorHeap::DescriptorHandle ret = pHeap->mFreeList.back();
			pHeap->mFreeList.pop_back();
			return ret;
		}
	}

	uint32_t usedDescriptors = tfrg_atomic32_add_relaxed(&pHeap->mUsedDescriptors, descriptorCount);
	DescriptorHeap::DescriptorHandle ret =
	{
		{ pHeap->mStartHandle.mCpu.ptr + usedDescriptors * pHeap->mDescriptorSize },
		{ pHeap->mStartHandle.mGpu.ptr + usedDescriptors * pHeap->mDescriptorSize },
	};


	return ret;
}

void return_cpu_descriptor_handle(DescriptorHeap* pHeap, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	ASSERT((pHeap->mDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) == 0);
	pHeap->mFreeList.push_back({ handle, D3D12_GPU_VIRTUAL_ADDRESS_NULL });
}

static void copy_descriptor_handle(DescriptorHeap* pHeap, D3D12_CPU_DESCRIPTOR_HANDLE& srcHandle, uint64_t& dstHandle, uint32_t index)
{
	pHeap->pHandles[(dstHandle / pHeap->mDescriptorSize) + index] = srcHandle;
	pHeap->pDevice->CopyDescriptorsSimple(
		1,
		{ pHeap->mStartHandle.mCpu.ptr + dstHandle + (index * pHeap->mDescriptorSize) },
		srcHandle,
		pHeap->mDesc.Type);
}
/************************************************************************/
// Multi GPU Helper Functions
/************************************************************************/
uint32_t util_calculate_shared_node_mask(Renderer* pRenderer)
{
	if (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED)
		return (1 << pRenderer->mLinkedNodeCount) - 1;
	else
		return 0;
}

uint32_t util_calculate_node_mask(Renderer* pRenderer, uint32_t i)
{
	if (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED)
		return (1 << i);
	else
		return 0;
}
/************************************************************************/
/************************************************************************/
using DescriptorNameToIndexMap = eastl::string_hash_map<uint32_t>;

const DescriptorInfo* get_descriptor(const RootSignature* pRootSignature, const char* pResName)
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
// Globals
/************************************************************************/
static const uint32_t gDescriptorTableDWORDS = 1;
static const uint32_t gRootDescriptorDWORDS = 2;
static const uint32_t gMaxRootConstantsPerRootParam = 4U;
/************************************************************************/
// Logging functions
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

static void add_srv(
	Renderer* pRenderer, ID3D12Resource* pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pSrvDesc, D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
	*pHandle = consume_descriptor_handles(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], 1).mCpu;
	pRenderer->pDxDevice->CreateShaderResourceView(pResource, pSrvDesc, *pHandle);
}

static void add_uav(
	Renderer* pRenderer, ID3D12Resource* pResource, ID3D12Resource* pCounterResource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* pUavDesc,
	D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
	*pHandle = consume_descriptor_handles(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], 1).mCpu;
	pRenderer->pDxDevice->CreateUnorderedAccessView(pResource, pCounterResource, pUavDesc, *pHandle);
}

static void add_cbv(Renderer* pRenderer, const D3D12_CONSTANT_BUFFER_VIEW_DESC* pCbvDesc, D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
	*pHandle = consume_descriptor_handles(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], 1).mCpu;
	pRenderer->pDxDevice->CreateConstantBufferView(pCbvDesc, *pHandle);
}

static void add_rtv(
	Renderer* pRenderer, ID3D12Resource* pResource, DXGI_FORMAT format, uint32_t mipSlice, uint32_t arraySlice,
	D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
	*pHandle = consume_descriptor_handles(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV], 1).mCpu;
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	D3D12_RESOURCE_DESC           desc = pResource->GetDesc();
	D3D12_RESOURCE_DIMENSION      type = desc.Dimension;

	rtvDesc.Format = format;

	switch (type)
	{
		case D3D12_RESOURCE_DIMENSION_BUFFER: break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			if (desc.DepthOrArraySize > 1)
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
				rtvDesc.Texture1DArray.MipSlice = mipSlice;
				if (arraySlice != -1)
				{
					rtvDesc.Texture1DArray.ArraySize = 1;
					rtvDesc.Texture1DArray.FirstArraySlice = arraySlice;
				}
				else
				{
					rtvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
				}
			}
			else
			{
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
				rtvDesc.Texture1D.MipSlice = mipSlice;
			}
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			if (desc.SampleDesc.Count > 1)
			{
				if (desc.DepthOrArraySize > 1)
				{
					rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
					if (arraySlice != -1)
					{
						rtvDesc.Texture2DMSArray.ArraySize = 1;
						rtvDesc.Texture2DMSArray.FirstArraySlice = arraySlice;
					}
					else
					{
						rtvDesc.Texture2DMSArray.ArraySize = desc.DepthOrArraySize;
					}
				}
				else
				{
					rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				}
			}
			else
			{
				if (desc.DepthOrArraySize > 1)
				{
					rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
					rtvDesc.Texture2DArray.MipSlice = mipSlice;
					if (arraySlice != -1)
					{
						rtvDesc.Texture2DArray.ArraySize = 1;
						rtvDesc.Texture2DArray.FirstArraySlice = arraySlice;
					}
					else
					{
						rtvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
					}
				}
				else
				{
					rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
					rtvDesc.Texture2D.MipSlice = mipSlice;
				}
			}
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
			rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
			rtvDesc.Texture3D.MipSlice = mipSlice;
			if (arraySlice != -1)
			{
				rtvDesc.Texture3D.WSize = 1;
				rtvDesc.Texture3D.FirstWSlice = arraySlice;
			}
			else
			{
				rtvDesc.Texture3D.WSize = desc.DepthOrArraySize;
			}
			break;
		default: break;
	}

	pRenderer->pDxDevice->CreateRenderTargetView(pResource, &rtvDesc, *pHandle);
}

static void add_dsv(
	Renderer* pRenderer, ID3D12Resource* pResource, DXGI_FORMAT format, uint32_t mipSlice, uint32_t arraySlice,
	D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
	*pHandle = consume_descriptor_handles(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV], 1).mCpu;
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	D3D12_RESOURCE_DESC           desc = pResource->GetDesc();
	D3D12_RESOURCE_DIMENSION      type = desc.Dimension;

	dsvDesc.Format = format;

	switch (type)
	{
		case D3D12_RESOURCE_DIMENSION_BUFFER: break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
			if (desc.DepthOrArraySize > 1)
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
				dsvDesc.Texture1DArray.MipSlice = mipSlice;
				if (arraySlice != -1)
				{
					dsvDesc.Texture1DArray.ArraySize = 1;
					dsvDesc.Texture1DArray.FirstArraySlice = arraySlice;
				}
				else
				{
					dsvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
				}
			}
			else
			{
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
				dsvDesc.Texture1D.MipSlice = mipSlice;
			}
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
			if (desc.SampleDesc.Count > 1)
			{
				if (desc.DepthOrArraySize > 1)
				{
					dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
					if (arraySlice != -1)
					{
						dsvDesc.Texture2DMSArray.ArraySize = 1;
						dsvDesc.Texture2DMSArray.FirstArraySlice = arraySlice;
					}
					else
					{
						dsvDesc.Texture2DMSArray.ArraySize = desc.DepthOrArraySize;
					}
				}
				else
				{
					dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
				}
			}
			else
			{
				if (desc.DepthOrArraySize > 1)
				{
					dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
					dsvDesc.Texture2DArray.MipSlice = mipSlice;
					if (arraySlice != -1)
					{
						dsvDesc.Texture2DArray.ArraySize = 1;
						dsvDesc.Texture2DArray.FirstArraySlice = arraySlice;
					}
					else
					{
						dsvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
					}
				}
				else
				{
					dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
					dsvDesc.Texture2D.MipSlice = mipSlice;
				}
			}
			break;
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D: ASSERT(false && "Cannot create 3D Depth Stencil"); break;
		default: break;
	}

	pRenderer->pDxDevice->CreateDepthStencilView(pResource, &dsvDesc, *pHandle);
}

static void add_sampler(Renderer* pRenderer, const D3D12_SAMPLER_DESC* pSamplerDesc, D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
	*pHandle =
		consume_descriptor_handles(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE::D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER], 1).mCpu;
	pRenderer->pDxDevice->CreateSampler(pSamplerDesc, *pHandle);
}

static void create_default_resources(Renderer* pRenderer)
{
	// Create NULL descriptors in case user does not specify some descriptors we can bind null descriptor handles at those points
	D3D12_SAMPLER_DESC samplerDesc = {};
	samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	add_sampler(pRenderer, &samplerDesc, &pRenderer->mNullSampler);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R8_UINT;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R8_UINT;

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_1D]);
	add_uav(pRenderer, NULL, NULL, &uavDesc, &pRenderer->mNullTextureUAV[TEXTURE_DIM_1D]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_2D]);
	add_uav(pRenderer, NULL, NULL, &uavDesc, &pRenderer->mNullTextureUAV[TEXTURE_DIM_2D]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_2DMS]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_3D]);
	add_uav(pRenderer, NULL, NULL, &uavDesc, &pRenderer->mNullTextureUAV[TEXTURE_DIM_3D]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_1D_ARRAY]);
	add_uav(pRenderer, NULL, NULL, &uavDesc, &pRenderer->mNullTextureUAV[TEXTURE_DIM_1D_ARRAY]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_2D_ARRAY]);
	add_uav(pRenderer, NULL, NULL, &uavDesc, &pRenderer->mNullTextureUAV[TEXTURE_DIM_2D_ARRAY]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_2DMS_ARRAY]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_CUBE]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullTextureSRV[TEXTURE_DIM_CUBE_ARRAY]);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
	add_srv(pRenderer, NULL, &srvDesc, &pRenderer->mNullBufferSRV);
	add_uav(pRenderer, NULL, NULL, &uavDesc, &pRenderer->mNullBufferUAV);
	add_cbv(pRenderer, NULL, &pRenderer->mNullBufferCBV);

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
}

static void destroy_default_resources(Renderer* pRenderer)
{
	removeBlendState(pRenderer->pDefaultBlendState);
	removeDepthState(pRenderer->pDefaultDepthState);
	removeRasterizerState(pRenderer->pDefaultRasterizerState);
}

typedef enum GpuVendor
{
	GPU_VENDOR_NVIDIA,
	GPU_VENDOR_AMD,
	GPU_VENDOR_INTEL,
	GPU_VENDOR_UNKNOWN,
	GPU_VENDOR_COUNT,
} GpuVendor;

static uint32_t gRootSignatureDWORDS[GpuVendor::GPU_VENDOR_COUNT] =
{
	64U,
	13U,
	64U,
	64U,
};

#define VENDOR_ID_NVIDIA 0x10DE
#define VENDOR_ID_AMD 0x1002
#define VENDOR_ID_AMD_1 0x1022
#define VENDOR_ID_INTEL 0x163C
#define VENDOR_ID_INTEL_1 0x8086
#define VENDOR_ID_INTEL_2 0x8087

static GpuVendor util_to_internal_gpu_vendor(UINT vendorId)
{
	if (vendorId == VENDOR_ID_NVIDIA)
		return GPU_VENDOR_NVIDIA;
	else if (vendorId == VENDOR_ID_AMD || vendorId == VENDOR_ID_AMD_1)
		return GPU_VENDOR_AMD;
	else if (vendorId == VENDOR_ID_INTEL || vendorId == VENDOR_ID_INTEL_1 || vendorId == VENDOR_ID_INTEL_2)
		return GPU_VENDOR_INTEL;
	else
		return GPU_VENDOR_UNKNOWN;
}
/************************************************************************/
// Internal Root Signature Functions
/************************************************************************/
typedef struct UpdateFrequencyLayoutInfo
{
	eastl::vector<DescriptorInfo*>                  mCbvSrvUavTable;
	eastl::vector<DescriptorInfo*>                  mSamplerTable;
	eastl::vector<DescriptorInfo*>                  mRootDescriptorParams;
	eastl::vector<DescriptorInfo*>                  mRootConstants;
	eastl::unordered_map<DescriptorInfo*, uint32_t> mDescriptorIndexMap;
} UpdateFrequencyLayoutInfo;

/// Calculates the total size of the root signature (in DWORDS) from the input layouts
uint32_t calculate_root_signature_size(UpdateFrequencyLayoutInfo* pLayouts, uint32_t numLayouts)
{
	uint32_t size = 0;
	for (uint32_t i = 0; i < numLayouts; ++i)
	{
		if ((uint32_t)pLayouts[i].mCbvSrvUavTable.size())
			size += gDescriptorTableDWORDS;
		if ((uint32_t)pLayouts[i].mSamplerTable.size())
			size += gDescriptorTableDWORDS;

		for (uint32_t c = 0; c < (uint32_t)pLayouts[i].mRootDescriptorParams.size(); ++c)
		{
			size += gRootDescriptorDWORDS;
		}
		for (uint32_t c = 0; c < (uint32_t)pLayouts[i].mRootConstants.size(); ++c)
		{
			DescriptorInfo* pDesc = pLayouts[i].mRootConstants[c];
			size += pDesc->mDesc.size;
		}
	}

	return size;
}

/// Creates a root descriptor table parameter from the input table layout for root signature version 1_1
void create_descriptor_table(
	uint32_t numDescriptors, DescriptorInfo** tableRef, D3D12_DESCRIPTOR_RANGE1* pRange, D3D12_ROOT_PARAMETER1* pRootParam)
{
	pRootParam->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	ShaderStage stageCount = SHADER_STAGE_NONE;
	for (uint32_t i = 0; i < numDescriptors; ++i)
	{
		const DescriptorInfo* pDesc = tableRef[i];
		pRange[i].BaseShaderRegister = pDesc->mDesc.reg;
		pRange[i].RegisterSpace = pDesc->mDesc.set;
		pRange[i].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
		pRange[i].NumDescriptors = pDesc->mDesc.size;
		pRange[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		pRange[i].RangeType = util_to_dx_descriptor_range(pDesc->mDesc.type);
		stageCount |= pDesc->mDesc.used_stages;
	}
	pRootParam->ShaderVisibility = util_to_dx_shader_visibility(stageCount);
	pRootParam->DescriptorTable.NumDescriptorRanges = numDescriptors;
	pRootParam->DescriptorTable.pDescriptorRanges = pRange;
}

/// Creates a root descriptor table parameter from the input table layout for root signature version 1_0
void create_descriptor_table_1_0(
	uint32_t numDescriptors, DescriptorInfo** tableRef, D3D12_DESCRIPTOR_RANGE* pRange, D3D12_ROOT_PARAMETER* pRootParam)
{
	pRootParam->ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	ShaderStage stageCount = SHADER_STAGE_NONE;

	for (uint32_t i = 0; i < numDescriptors; ++i)
	{
		const DescriptorInfo* pDesc = tableRef[i];
		pRange[i].BaseShaderRegister = pDesc->mDesc.reg;
		pRange[i].RegisterSpace = pDesc->mDesc.set;
		pRange[i].NumDescriptors = pDesc->mDesc.size;
		pRange[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		pRange[i].RangeType = util_to_dx_descriptor_range(pDesc->mDesc.type);
		stageCount |= pDesc->mDesc.used_stages;
	}

	pRootParam->ShaderVisibility = util_to_dx_shader_visibility(stageCount);
	pRootParam->DescriptorTable.NumDescriptorRanges = numDescriptors;
	pRootParam->DescriptorTable.pDescriptorRanges = pRange;
}

/// Creates a root descriptor / root constant parameter for root signature version 1_1
void create_root_descriptor(const DescriptorInfo* pDesc, D3D12_ROOT_PARAMETER1* pRootParam)
{
	pRootParam->ShaderVisibility = util_to_dx_shader_visibility(pDesc->mDesc.used_stages);
	pRootParam->ParameterType = pDesc->mDxType;
	pRootParam->Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC;
	pRootParam->Descriptor.ShaderRegister = pDesc->mDesc.reg;
	pRootParam->Descriptor.RegisterSpace = pDesc->mDesc.set;
}

/// Creates a root descriptor / root constant parameter for root signature version 1_0
void create_root_descriptor_1_0(const DescriptorInfo* pDesc, D3D12_ROOT_PARAMETER* pRootParam)
{
	pRootParam->ShaderVisibility = util_to_dx_shader_visibility(pDesc->mDesc.used_stages);
	pRootParam->ParameterType = pDesc->mDxType;
	pRootParam->Descriptor.ShaderRegister = pDesc->mDesc.reg;
	pRootParam->Descriptor.RegisterSpace = pDesc->mDesc.set;
}

void create_root_constant(const DescriptorInfo* pDesc, D3D12_ROOT_PARAMETER1* pRootParam)
{
	pRootParam->ShaderVisibility = util_to_dx_shader_visibility(pDesc->mDesc.used_stages);
	pRootParam->ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	pRootParam->Constants.Num32BitValues = pDesc->mDesc.size;
	pRootParam->Constants.ShaderRegister = pDesc->mDesc.reg;
	pRootParam->Constants.RegisterSpace = pDesc->mDesc.set;
}

void create_root_constant_1_0(const DescriptorInfo* pDesc, D3D12_ROOT_PARAMETER* pRootParam)
{
	pRootParam->ShaderVisibility = util_to_dx_shader_visibility(pDesc->mDesc.used_stages);
	pRootParam->ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	pRootParam->Constants.Num32BitValues = pDesc->mDesc.size;
	pRootParam->Constants.ShaderRegister = pDesc->mDesc.reg;
	pRootParam->Constants.RegisterSpace = pDesc->mDesc.set;
}
/************************************************************************/
// Internal utility functions
/************************************************************************/
D3D12_FILTER util_to_dx_filter(FilterType minFilter, FilterType magFilter, MipMapMode mipMapMode, bool aniso, bool comparisonFilterEnabled)
{
	if (aniso)
		return (comparisonFilterEnabled ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC);

	// control bit : minFilter  magFilter   mipMapMode
	//   point   :   00	  00	   00
	//   linear  :   01	  01	   01
	// ex : trilinear == 010101
	int filter = (minFilter << 4) | (magFilter << 2) | mipMapMode;
	int baseFilter = comparisonFilterEnabled ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_POINT;
	return (D3D12_FILTER)(baseFilter + filter);
}

D3D12_TEXTURE_ADDRESS_MODE util_to_dx_texture_address_mode(AddressMode addressMode)
{
	switch (addressMode)
	{
		case ADDRESS_MODE_MIRROR: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case ADDRESS_MODE_REPEAT: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case ADDRESS_MODE_CLAMP_TO_EDGE: return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case ADDRESS_MODE_CLAMP_TO_BORDER: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
		default: return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE util_to_dx_primitive_topology_type(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PRIMITIVE_TOPO_POINT_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
		case PRIMITIVE_TOPO_LINE_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PRIMITIVE_TOPO_LINE_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
		case PRIMITIVE_TOPO_TRI_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case PRIMITIVE_TOPO_TRI_STRIP: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		case PRIMITIVE_TOPO_PATCH_LIST: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	}
	return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
}

uint64_t util_dx_determine_storage_counter_offset(uint64_t buffer_size)
{
	uint64_t alignment = D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT;
	uint64_t result = (buffer_size + (alignment - 1)) & ~(alignment - 1);
	return result;
}

DXGI_FORMAT util_to_dx_uav_format(DXGI_FORMAT defaultFormat)
{
	switch (defaultFormat)
	{
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;

		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
		case DXGI_FORMAT_B8G8R8A8_UNORM:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;

		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
		case DXGI_FORMAT_B8G8R8X8_UNORM:
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;

		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;

#ifdef _DEBUG
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
		case DXGI_FORMAT_D16_UNORM: LOGF( LogLevel::eERROR, "Requested a UAV format for a depth stencil format");
#endif

		default: return defaultFormat;
	}
}

DXGI_FORMAT util_to_dx_dsv_format(DXGI_FORMAT defaultFormat)
{
	switch (defaultFormat)
	{
			// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

			// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_D32_FLOAT;

			// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_D24_UNORM_S8_UINT;

			// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_D16_UNORM;

		default: return defaultFormat;
	}
}

DXGI_FORMAT util_to_dx_srv_format(DXGI_FORMAT defaultFormat)
{
	switch (defaultFormat)
	{
			// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;

			// No Stencil
		case DXGI_FORMAT_R32_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT:
		case DXGI_FORMAT_R32_FLOAT:
			return DXGI_FORMAT_R32_FLOAT;

			// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
			return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

			// 16-bit Z w/o Stencil
		case DXGI_FORMAT_R16_TYPELESS:
		case DXGI_FORMAT_D16_UNORM:
		case DXGI_FORMAT_R16_UNORM: return DXGI_FORMAT_R16_UNORM;

		case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;

		default: return defaultFormat;
	}
}

DXGI_FORMAT util_to_dx_stencil_format(DXGI_FORMAT defaultFormat)
{
	switch (defaultFormat)
	{
			// 32-bit Z w/ Stencil
		case DXGI_FORMAT_R32G8X24_TYPELESS:
		case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
		case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
		case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
			return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;

			// 24-bit Z
		case DXGI_FORMAT_R24G8_TYPELESS:
		case DXGI_FORMAT_D24_UNORM_S8_UINT:
		case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
		case DXGI_FORMAT_X24_TYPELESS_G8_UINT: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;

		default: return DXGI_FORMAT_UNKNOWN;
	}
}

DXGI_FORMAT util_to_dx_swapchain_format(TinyImageFormat const format)
{
	DXGI_FORMAT result = DXGI_FORMAT_UNKNOWN;

	// FLIP_DISCARD and FLIP_SEQEUNTIAL swapchain buffers only support these formats
	switch (format)
	{
		case TinyImageFormat_R16G16B16A16_SFLOAT: result = DXGI_FORMAT_R16G16B16A16_FLOAT;
		case TinyImageFormat_B8G8R8A8_UNORM: result = DXGI_FORMAT_B8G8R8A8_UNORM; break;
		case TinyImageFormat_R8G8B8A8_UNORM: result = DXGI_FORMAT_R8G8B8A8_UNORM; break;
		case TinyImageFormat_B8G8R8A8_SRGB: result = DXGI_FORMAT_B8G8R8A8_UNORM; break;
		case TinyImageFormat_R8G8B8A8_SRGB: result = DXGI_FORMAT_R8G8B8A8_UNORM; break;
		case TinyImageFormat_R10G10B10A2_UNORM: result = DXGI_FORMAT_R10G10B10A2_UNORM; break;
		default: break;
	}

	if (result == DXGI_FORMAT_UNKNOWN)
	{
		LOGF(LogLevel::eERROR, "Image Format (%u) not supported for creating swapchain buffer", (uint32_t)format);
	}

	return result;
}


D3D12_SHADER_VISIBILITY util_to_dx_shader_visibility(ShaderStage stages)
{
	D3D12_SHADER_VISIBILITY res = D3D12_SHADER_VISIBILITY_ALL;
	uint32_t                stageCount = 0;

	if (stages == SHADER_STAGE_COMP)
	{
		return D3D12_SHADER_VISIBILITY_ALL;
	}
	if (stages & SHADER_STAGE_VERT)
	{
		res = D3D12_SHADER_VISIBILITY_VERTEX;
		++stageCount;
	}
	if (stages & SHADER_STAGE_GEOM)
	{
		res = D3D12_SHADER_VISIBILITY_GEOMETRY;
		++stageCount;
	}
	if (stages & SHADER_STAGE_HULL)
	{
		res = D3D12_SHADER_VISIBILITY_HULL;
		++stageCount;
	}
	if (stages & SHADER_STAGE_DOMN)
	{
		res = D3D12_SHADER_VISIBILITY_DOMAIN;
		++stageCount;
	}
	if (stages & SHADER_STAGE_FRAG)
	{
		res = D3D12_SHADER_VISIBILITY_PIXEL;
		++stageCount;
	}
#ifdef ENABLE_RAYTRACING
	if (stages == SHADER_STAGE_RAYTRACING)
	{
		return D3D12_SHADER_VISIBILITY_ALL;
	}
#endif
	ASSERT(stageCount > 0);
	return stageCount > 1 ? D3D12_SHADER_VISIBILITY_ALL : res;
}

D3D12_DESCRIPTOR_RANGE_TYPE util_to_dx_descriptor_range(DescriptorType type)
{
	switch (type)
	{
		case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case DESCRIPTOR_TYPE_ROOT_CONSTANT: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		case DESCRIPTOR_TYPE_TEXTURE:
		case DESCRIPTOR_TYPE_BUFFER: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		case DESCRIPTOR_TYPE_RW_BUFFER:
		case DESCRIPTOR_TYPE_RW_TEXTURE: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		case DESCRIPTOR_TYPE_SAMPLER: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
#ifdef ENABLE_RAYTRACING
		case DESCRIPTOR_TYPE_RAY_TRACING: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
#endif
		default: ASSERT("Invalid DescriptorInfo Type"); return (D3D12_DESCRIPTOR_RANGE_TYPE)-1;
	}
}

D3D12_RESOURCE_STATES util_to_dx_resource_state(ResourceState state)
{
	D3D12_RESOURCE_STATES ret = (D3D12_RESOURCE_STATES)state;

	// These states cannot be combined with other states so we just do an == check
	if (state == RESOURCE_STATE_GENERIC_READ)
		return D3D12_RESOURCE_STATE_GENERIC_READ;
	if (state == RESOURCE_STATE_COMMON)
		return D3D12_RESOURCE_STATE_COMMON;
	if (state == RESOURCE_STATE_PRESENT)
		return D3D12_RESOURCE_STATE_PRESENT;

	if (state & RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)
		ret |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	if (state & RESOURCE_STATE_INDEX_BUFFER)
		ret |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
	if (state & RESOURCE_STATE_RENDER_TARGET)
		ret |= D3D12_RESOURCE_STATE_RENDER_TARGET;
	if (state & RESOURCE_STATE_UNORDERED_ACCESS)
		ret |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	if (state & RESOURCE_STATE_DEPTH_WRITE)
		ret |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
	if (state & RESOURCE_STATE_DEPTH_READ)
		ret |= D3D12_RESOURCE_STATE_DEPTH_READ;
	if (state & RESOURCE_STATE_STREAM_OUT)
		ret |= D3D12_RESOURCE_STATE_STREAM_OUT;
	if (state & RESOURCE_STATE_INDIRECT_ARGUMENT)
		ret |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
	if (state & RESOURCE_STATE_COPY_DEST)
		ret |= D3D12_RESOURCE_STATE_COPY_DEST;
	if (state & RESOURCE_STATE_COPY_SOURCE)
		ret |= D3D12_RESOURCE_STATE_COPY_SOURCE;

	if (state == RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		ret |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	else if (state & RESOURCE_STATE_SHADER_RESOURCE)
		ret |= (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	return ret;
}

D3D12_QUERY_HEAP_TYPE util_to_dx_query_heap_type(QueryType type)
{
	switch (type)
	{
		case QUERY_TYPE_TIMESTAMP: return D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
		case QUERY_TYPE_PIPELINE_STATISTICS: return D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
		case QUERY_TYPE_OCCLUSION: return D3D12_QUERY_HEAP_TYPE_OCCLUSION;
		default: ASSERT(false && "Invalid query heap type"); return D3D12_QUERY_HEAP_TYPE(-1);
	}
}

D3D12_QUERY_TYPE util_to_dx_query_type(QueryType type)
{
	switch (type)
	{
		case QUERY_TYPE_TIMESTAMP: return D3D12_QUERY_TYPE_TIMESTAMP;
		case QUERY_TYPE_PIPELINE_STATISTICS: return D3D12_QUERY_TYPE_PIPELINE_STATISTICS;
		case QUERY_TYPE_OCCLUSION: return D3D12_QUERY_TYPE_OCCLUSION;
		default: ASSERT(false && "Invalid query heap type"); return D3D12_QUERY_TYPE(-1);
	}
}

/************************************************************************/
// Internal init functions
/************************************************************************/
#ifndef _DURANGO
dxc::DxcDllSupport gDxcDllHelper;

// Note that Windows 10 Creator Update SDK is required for enabling Shader Model 6 feature.
static HRESULT EnableExperimentalShaderModels()
{
	static const GUID D3D12ExperimentalShaderModelsID = { /* 76f5573e-f13a-40f5-b297-81ce9e18933f */
														  0x76f5573e,
														  0xf13a,
														  0x40f5,
														  { 0xb2, 0x97, 0x81, 0xce, 0x9e, 0x18, 0x93, 0x3f }
	};

	return D3D12EnableExperimentalFeatures(1, &D3D12ExperimentalShaderModelsID, NULL, NULL);
}
#endif

#ifdef _DURANGO
static UINT HANGBEGINCALLBACK(UINT64 Flags)
{
	LOGF(LogLevel::eINFO, "( %d )", Flags);
	return (UINT)Flags;
}

static void HANGPRINTCALLBACK(const CHAR* strLine)
{
	LOGF(LogLevel::eINFO, "( %s )", strLine);
	return;
}

static void HANGDUMPCALLBACK(const WCHAR* strFileName) { return; }
#endif

static void AddDevice(Renderer* pRenderer)
{
#if defined(_DEBUG) || defined(PROFILE)
	//add debug layer if in debug mode
	if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(pRenderer->pDXDebug), (void**)&(pRenderer->pDXDebug))))
	{
		if (fnHookEnableDebugLayer != NULL)
			fnHookEnableDebugLayer(pRenderer);
	}
#endif

	D3D_FEATURE_LEVEL feature_levels[4] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};

#ifdef _DURANGO
	// Create the DX12 API device object.
	HRESULT hres = create_device(NULL, &pRenderer->pDxDevice);
	ASSERT(SUCCEEDED(hres));

#if defined(_DEBUG) || defined(PROFILE)
	//Sets the callback functions to invoke when the GPU hangs
	//pRenderer->pDxDevice->SetHangCallbacksX(HANGBEGINCALLBACK, HANGPRINTCALLBACK, NULL);
#endif

	// First, retrieve the underlying DXGI device from the D3D device.
	IDXGIDevice1* dxgiDevice;
	hres = pRenderer->pDxDevice->QueryInterface(IID_ARGS(&dxgiDevice));
	ASSERT(SUCCEEDED(hres));

	// Identify the physical adapter (GPU or card) this device is running on.
	IDXGIAdapter* dxgiAdapter;
	hres = dxgiDevice->GetAdapter(&dxgiAdapter);
	ASSERT(SUCCEEDED(hres));

	// And obtain the factory object that created it.
	hres = dxgiAdapter->GetParent(IID_ARGS(&pRenderer->pDXGIFactory));
	ASSERT(SUCCEEDED(hres));

	typedef struct GpuDesc
	{
		IDXGIAdapter*     pGpu = NULL;
		D3D_FEATURE_LEVEL mMaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0;
	} GpuDesc;

	GpuDesc gpuDesc[MAX_GPUS] = {};
	dxgiAdapter->QueryInterface(IID_ARGS(&gpuDesc->pGpu));
	gpuDesc->mMaxSupportedFeatureLevel = feature_levels[0];
	pRenderer->mNumOfGPUs = 1;

	dxgiAdapter->Release();

	auto isDeviceBetter = [](uint32_t, uint32_t) -> bool { return false; };
#else
	UINT flags = 0;
#if defined(_DEBUG)
	flags = DXGI_CREATE_FACTORY_DEBUG;
#endif
	HRESULT hres = CreateDXGIFactory2(flags, __uuidof(pRenderer->pDXGIFactory), (void**)&(pRenderer->pDXGIFactory));
	ASSERT(SUCCEEDED(hres));

	typedef struct GpuDesc
	{
		Renderer*                         pRenderer = NULL;
		IDXGIAdapter3*                    pGpu = NULL;
		D3D_FEATURE_LEVEL                 mMaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0;
		D3D12_FEATURE_DATA_D3D12_OPTIONS  mFeatureDataOptions;
		D3D12_FEATURE_DATA_D3D12_OPTIONS1 mFeatureDataOptions1;
		SIZE_T                            mDedicatedVideoMemory = 0;
		char                              mVendorId[MAX_GPU_VENDOR_STRING_LENGTH];
		char                              mDeviceId[MAX_GPU_VENDOR_STRING_LENGTH];
		char                              mRevisionId[MAX_GPU_VENDOR_STRING_LENGTH];
		char                              mName[MAX_GPU_VENDOR_STRING_LENGTH];
		GPUPresetLevel                    mPreset;
	} GpuDesc;

	GpuDesc gpuDesc[MAX_GPUS] = {};

	IDXGIAdapter3* adapter = NULL;
	for (UINT i = 0; DXGI_ERROR_NOT_FOUND != pRenderer->pDXGIFactory->EnumAdapters1(i, (IDXGIAdapter1**)&adapter); ++i)
	{
		DECLARE_ZERO(DXGI_ADAPTER_DESC1, desc);
		adapter->GetDesc1(&desc);

		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
		{
			for (uint32_t level = 0; level < sizeof(feature_levels) / sizeof(feature_levels[0]); ++level)
			{
				// Make sure the adapter can support a D3D12 device
				if (SUCCEEDED(D3D12CreateDevice(adapter, feature_levels[level], __uuidof(ID3D12Device), NULL)))
				{
					hres = adapter->QueryInterface(IID_ARGS(&gpuDesc[pRenderer->mNumOfGPUs].pGpu));
					if (SUCCEEDED(hres))
					{
						D3D12CreateDevice(adapter, feature_levels[level], IID_PPV_ARGS(&pRenderer->pDxDevice));

						// Query the level of support of Shader Model.
						D3D12_FEATURE_DATA_D3D12_OPTIONS  featureData = {};
						D3D12_FEATURE_DATA_D3D12_OPTIONS1 featureData1 = {};
						// Query the level of support of Wave Intrinsics.
						pRenderer->pDxDevice->CheckFeatureSupport(
							(D3D12_FEATURE)D3D12_FEATURE_D3D12_OPTIONS, &featureData, sizeof(featureData));
						pRenderer->pDxDevice->CheckFeatureSupport(
							(D3D12_FEATURE)D3D12_FEATURE_D3D12_OPTIONS1, &featureData1, sizeof(featureData1));

						gpuDesc[pRenderer->mNumOfGPUs].mMaxSupportedFeatureLevel = feature_levels[level];
						gpuDesc[pRenderer->mNumOfGPUs].mDedicatedVideoMemory = desc.DedicatedVideoMemory;
						gpuDesc[pRenderer->mNumOfGPUs].mFeatureDataOptions = featureData;
						gpuDesc[pRenderer->mNumOfGPUs].mFeatureDataOptions1 = featureData1;
						gpuDesc[pRenderer->mNumOfGPUs].pRenderer = pRenderer;

						//save vendor and model Id as string
						//char hexChar[10];
						//convert deviceId and assign it
						sprintf(gpuDesc[pRenderer->mNumOfGPUs].mDeviceId, "%#x\0", desc.DeviceId);
						//convert modelId and assign it
						sprintf(gpuDesc[pRenderer->mNumOfGPUs].mVendorId, "%#x\0", desc.VendorId);
						//convert Revision Id
						sprintf(gpuDesc[pRenderer->mNumOfGPUs].mRevisionId, "%#x\0", desc.Revision);

						//get preset for current gpu description
						gpuDesc[pRenderer->mNumOfGPUs].mPreset = getGPUPresetLevel(
							gpuDesc[pRenderer->mNumOfGPUs].mVendorId, gpuDesc[pRenderer->mNumOfGPUs].mDeviceId,
							gpuDesc[pRenderer->mNumOfGPUs].mRevisionId);

						//save gpu name (Some situtations this can show description instead of name)
						//char sName[MAX_PATH];
						wcstombs(gpuDesc[pRenderer->mNumOfGPUs].mName, desc.Description, MAX_PATH);
						++pRenderer->mNumOfGPUs;
						SAFE_RELEASE(pRenderer->pDxDevice);
						break;
					}
				}
			}
		}

		adapter->Release();
	}

	ASSERT(pRenderer->mNumOfGPUs > 0);

	auto isDeviceBetter = [gpuDesc](uint32_t testIndex, uint32_t refIndex) -> bool {
		const GpuDesc& gpu1 = gpuDesc[testIndex];
		const GpuDesc& gpu2 = gpuDesc[refIndex];

// force to an Intel, useful sometimes for debugging
//		if(stricmp(gpu1.mVendorId, "0x8086") == 0 )
//			return true;

		// If shader model 6.0 or higher is requested, prefer the GPU which supports it
		if (gpu1.pRenderer->mSettings.mShaderTarget >= shader_target_6_0)
		{
			if (gpu1.mFeatureDataOptions1.WaveOps != gpu2.mFeatureDataOptions1.WaveOps)
				return gpu1.mFeatureDataOptions1.WaveOps;
		}

		// Check feature level first, sort the greatest feature level gpu to the front
		if ((int)gpu1.mPreset != (int)gpu2.mPreset)
		{
			return gpu1.mPreset > gpu2.mPreset;
		}

		if ((int)gpu1.mMaxSupportedFeatureLevel != (int)gpu2.mMaxSupportedFeatureLevel)
		{
			return gpu2.mMaxSupportedFeatureLevel < gpu1.mMaxSupportedFeatureLevel;
		}

		return gpu1.mDedicatedVideoMemory > gpu2.mDedicatedVideoMemory;
	};

#endif

	uint32_t gpuIndex = UINT32_MAX;
	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		pRenderer->pDxGPUs[i] = gpuDesc[i].pGpu;
		pRenderer->mGpuSettings[i].mUniformBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
		pRenderer->mGpuSettings[i].mUploadBufferTextureAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
#if defined(_DURANGO)
		pRenderer->mGpuSettings[i].mUploadBufferTextureRowAlignment = D3D12XBOX_TEXTURE_DATA_PITCH_ALIGNMENT;
#else
		pRenderer->mGpuSettings[i].mUploadBufferTextureRowAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
#endif
		pRenderer->mGpuSettings[i].mMultiDrawIndirect = true;
		pRenderer->mGpuSettings[i].mMaxVertexInputBindings = 32U;
#ifndef _DURANGO
		//assign device ID
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mModelId, gpuDesc[i].mDeviceId, MAX_GPU_VENDOR_STRING_LENGTH);
		//assign vendor ID
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mVendorId, gpuDesc[i].mVendorId, MAX_GPU_VENDOR_STRING_LENGTH);
		//assign Revision ID
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mRevisionId, gpuDesc[i].mRevisionId, MAX_GPU_VENDOR_STRING_LENGTH);
		//get name from api
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mGpuName, gpuDesc[i].mName, MAX_GPU_VENDOR_STRING_LENGTH);
		//get preset
		pRenderer->mGpuSettings[i].mGpuVendorPreset.mPresetLevel = gpuDesc[i].mPreset;
		//get wave lane count
		pRenderer->mGpuSettings[i].mWaveLaneCount = gpuDesc[i].mFeatureDataOptions1.WaveLaneCountMin;
		pRenderer->mGpuSettings[i].mROVsSupported = gpuDesc[i].mFeatureDataOptions.ROVsSupported ? true : false;
#else
		//Default XBox values
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mModelId, "XboxOne", MAX_GPU_VENDOR_STRING_LENGTH);
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mVendorId, "XboxOne", MAX_GPU_VENDOR_STRING_LENGTH);
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mRevisionId, "0x00", MAX_GPU_VENDOR_STRING_LENGTH);
		strncpy(pRenderer->mGpuSettings[i].mGpuVendorPreset.mGpuName, "XboxOne", MAX_GPU_VENDOR_STRING_LENGTH);

		pRenderer->mGpuSettings[i].mGpuVendorPreset.mPresetLevel = GPUPresetLevel::GPU_PRESET_HIGH;
#endif
		// Determine root signature size for this gpu driver
		DXGI_ADAPTER_DESC adapterDesc;
		pRenderer->pDxGPUs[i]->GetDesc(&adapterDesc);
		pRenderer->mGpuSettings[i].mMaxRootSignatureDWORDS = gRootSignatureDWORDS[util_to_internal_gpu_vendor(adapterDesc.VendorId)];
		LOGF(
			LogLevel::eINFO, "GPU[%i] detected. Vendor ID: %x, Revision ID: %x, GPU Name: %S", i, adapterDesc.VendorId,
			adapterDesc.Revision, adapterDesc.Description);

		// Check that gpu supports at least graphics
		if (gpuIndex == UINT32_MAX || isDeviceBetter(i, gpuIndex))
		{
			gpuIndex = i;
		}
	}

#if defined(ACTIVE_TESTING_GPU) && !defined(_DURANGO) && defined(AUTOMATED_TESTING)
	//Read active GPU if AUTOMATED_TESTING and ACTIVE_TESTING_GPU are defined
	GPUVendorPreset activeTestingPreset;
	bool            activeTestingGpu = getActiveGpuConfig(activeTestingPreset);
	if (activeTestingGpu)
	{
		for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; i++)
		{
			if (strcmp(pRenderer->mGpuSettings[i].mGpuVendorPreset.mVendorId, activeTestingPreset.mVendorId) == 0 &&
				strcmp(pRenderer->mGpuSettings[i].mGpuVendorPreset.mModelId, activeTestingPreset.mModelId) == 0)
			{
				//if revision ID is valid then use it to select active GPU
				if (strcmp(pRenderer->mGpuSettings[i].mGpuVendorPreset.mRevisionId, "0x00") != 0 &&
					strcmp(pRenderer->mGpuSettings[i].mGpuVendorPreset.mRevisionId != activeTestingPreset.mRevisionId) != 0)
					continue;
				gpuIndex = i;
				break;
			}
		}
	}
#endif
	// Get the latest and greatest feature level gpu
	pRenderer->pDxActiveGPU = pRenderer->pDxGPUs[gpuIndex];
	ASSERT(pRenderer->pDxActiveGPU != NULL);
	pRenderer->pActiveGpuSettings = &pRenderer->mGpuSettings[gpuIndex];

	//print selected GPU information
	LOGF(LogLevel::eINFO, "GPU[%d] is selected as default GPU", gpuIndex);
	LOGF(LogLevel::eINFO, "Name of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mGpuName);
	LOGF(LogLevel::eINFO, "Vendor id of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mVendorId);
	LOGF(LogLevel::eINFO, "Model id of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mModelId);
	LOGF(LogLevel::eINFO, "Revision id of selected gpu: %s", pRenderer->pActiveGpuSettings->mGpuVendorPreset.mRevisionId);

	// Load functions
	{
#ifdef _DURANGO
		HMODULE module = get_d3d12_module_handle();
#else
		HMODULE module = ::GetModuleHandle(TEXT("d3d12.dll"));
#endif
		fnD3D12CreateRootSignatureDeserializer =
			(PFN_D3D12_CREATE_ROOT_SIGNATURE_DESERIALIZER)GetProcAddress(module, "D3D12SerializeVersionedRootSignature");

		fnD3D12SerializeVersionedRootSignature =
			(PFN_D3D12_SERIALIZE_VERSIONED_ROOT_SIGNATURE)GetProcAddress(module, "D3D12SerializeVersionedRootSignature");

		fnD3D12CreateVersionedRootSignatureDeserializer =
			(PFN_D3D12_CREATE_VERSIONED_ROOT_SIGNATURE_DESERIALIZER)GetProcAddress(module, "D3D12CreateVersionedRootSignatureDeserializer");
	}

#ifndef _DURANGO
	hres = D3D12CreateDevice(pRenderer->pDxActiveGPU, gpuDesc[gpuIndex].mMaxSupportedFeatureLevel, IID_ARGS(&pRenderer->pDxDevice));
	ASSERT(SUCCEEDED(hres));
	// #TODO - Let user specify these through RendererSettings
	//ID3D12InfoQueue* pd3dInfoQueue = NULL;
	//HRESULT hr = pRenderer->pDxDevice->QueryInterface(IID_ARGS(&pd3dInfoQueue));
	//if (SUCCEEDED(hr))
	//{
	//	pd3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
	//	pd3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
	//	pd3dInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
	//}
#endif

	//pRenderer->mSettings.mDxFeatureLevel = target_feature_level;  // this is not used anywhere?
}

static void RemoveDevice(Renderer* pRenderer)
{
	SAFE_RELEASE(pRenderer->pDXGIFactory);

	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		SAFE_RELEASE(pRenderer->pDxGPUs[i]);
	}

#if defined(_DURANGO)
	SAFE_RELEASE(pRenderer->pDxDevice);
#elif defined(_DEBUG) || defined(PROFILE)
	ID3D12DebugDevice* pDebugDevice = NULL;
	pRenderer->pDxDevice->QueryInterface(&pDebugDevice);

	SAFE_RELEASE(pRenderer->pDXDebug);
	SAFE_RELEASE(pRenderer->pDxDevice);

	pDebugDevice->ReportLiveDeviceObjects(D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
	pDebugDevice->Release();
#else
	SAFE_RELEASE(pRenderer->pDxDevice);
#endif
}

/************************************************************************/
// Renderer Init Remove
/************************************************************************/
void initRenderer(const char* appName, const RendererDesc* settings, Renderer** ppRenderer)
{
	initHooks();

	Renderer* pRenderer = (Renderer*)conf_calloc(1, sizeof(*pRenderer));
	ASSERT(pRenderer);

	pRenderer->pName = (char*)conf_calloc(strlen(appName) + 1, sizeof(char));
	memcpy(pRenderer->pName, appName, strlen(appName));

	// Copy settings
	memcpy(&(pRenderer->mSettings), settings, sizeof(*settings));
#if defined(_DURANGO)
	pRenderer->mSettings.mApi = RENDERER_API_XBOX_D3D12;
#else
	pRenderer->mSettings.mApi = RENDERER_API_D3D12;
#endif

	// Initialize the D3D12 bits
	{
		AddDevice(pRenderer);

#ifndef _DURANGO
		//anything below LOW preset is not supported and we will exit
		if (pRenderer->pActiveGpuSettings->mGpuVendorPreset.mPresetLevel < GPU_PRESET_LOW)
		{
			//have the condition in the assert as well so its cleared when the assert message box appears

			ASSERT(pRenderer->pActiveGpuSettings->mGpuVendorPreset.mPresetLevel >= GPU_PRESET_LOW);

			SAFE_FREE(pRenderer->pName);

			//remove device and any memory we allocated in just above as this is the first function called
			//when initializing the forge
			RemoveDevice(pRenderer);
			SAFE_FREE(pRenderer);
			LOGF(LogLevel::eERROR, "Selected GPU has an Office Preset in gpu.cfg.");
			LOGF(LogLevel::eERROR, "Office preset is not supported by The Forge.");

			//return NULL pRenderer so that client can gracefully handle exit
			//This is better than exiting from here in case client has allocated memory or has fallbacks
			ppRenderer = NULL;
			return;
		}

		utils_caps_builder(pRenderer);

		if (pRenderer->mSettings.mShaderTarget >= shader_target_6_0)
		{
			// Query the level of support of Shader Model.
			D3D12_FEATURE_DATA_SHADER_MODEL   shaderModelSupport = { D3D_SHADER_MODEL_6_0 };
			D3D12_FEATURE_DATA_D3D12_OPTIONS1 m_WaveIntrinsicsSupport = {};
			if (!SUCCEEDED(pRenderer->pDxDevice->CheckFeatureSupport(
					(D3D12_FEATURE)D3D12_FEATURE_SHADER_MODEL, &shaderModelSupport, sizeof(shaderModelSupport))))
			{
				return;
			}
			// Query the level of support of Wave Intrinsics.
			if (!SUCCEEDED(pRenderer->pDxDevice->CheckFeatureSupport(
					(D3D12_FEATURE)D3D12_FEATURE_D3D12_OPTIONS1, &m_WaveIntrinsicsSupport, sizeof(m_WaveIntrinsicsSupport))))
			{
				return;
			}

			// If the device doesn't support SM6 or Wave Intrinsics, try enabling the experimental feature for Shader Model 6 and creating the device again.
			if (shaderModelSupport.HighestShaderModel != D3D_SHADER_MODEL_6_0 || m_WaveIntrinsicsSupport.WaveOps != TRUE)
			{
				RENDERDOC_API_1_1_2* rdoc_api = NULL;
				// At init, on windows
				if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
				{
					pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)GetProcAddress(mod, "RENDERDOC_GetAPI");
					RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void**)&rdoc_api);
				}

				// If RenderDoc is connected shader model 6 is not detected but it still works
				if (!rdoc_api || !rdoc_api->IsTargetControlConnected())
				{
					// If the device still doesn't support SM6 or Wave Intrinsics after enabling the experimental feature, you could set up your application to use the highest supported shader model.
					// For simplicity we just exit the application here.
					if (shaderModelSupport.HighestShaderModel != D3D_SHADER_MODEL_6_0 ||
						m_WaveIntrinsicsSupport.WaveOps != TRUE && !SUCCEEDED(EnableExperimentalShaderModels()))
					{
						RemoveDevice(pRenderer);
						LOGF(LogLevel::eERROR, "Hardware does not support Shader Model 6.0");
						return;
					}
				}
				else
				{
					LOGF( LogLevel::eWARNING,
						"\nRenderDoc does not support SM 6.0 or higher. Application might work but you won't be able to debug the SM 6.0+ "
						"shaders or view their bytecode.");
				}
			}
		}
#endif

		/************************************************************************/
		/************************************************************************/
		for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Flags = gCpuDescriptorHeapProperties[i].mFlags;
			desc.NodeMask = 0; // CPU Descriptor Heap - Node mask is irrelevant
			desc.NumDescriptors = gCpuDescriptorHeapProperties[i].mMaxDescriptors;
			desc.Type = (D3D12_DESCRIPTOR_HEAP_TYPE)i;
			add_descriptor_heap(pRenderer->pDxDevice, &desc, &pRenderer->pCPUDescriptorHeaps[i]);
		}
		/************************************************************************/
		// Multi GPU - SLI Node Count
		/************************************************************************/
		uint32_t gpuCount = pRenderer->pDxDevice->GetNodeCount();
		pRenderer->mLinkedNodeCount = gpuCount;
		if (pRenderer->mLinkedNodeCount < 2)
			pRenderer->mSettings.mGpuMode = GPU_MODE_SINGLE;

		AllocatorCreateInfo info = { 0 };
		info.pRenderer = pRenderer;
		info.device = pRenderer->pDxDevice;
		info.physicalDevice = pRenderer->pDxActiveGPU;
		createAllocator(&info, &pRenderer->pResourceAllocator);
	}

	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = util_calculate_node_mask(pRenderer, i);

		desc.NumDescriptors = 1 << 16;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		add_descriptor_heap(pRenderer->pDxDevice, &desc, &pRenderer->pCbvSrvUavHeaps[i]);

		desc.NumDescriptors = 1 << 11;
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		add_descriptor_heap(pRenderer->pDxDevice, &desc, &pRenderer->pSamplerHeaps[i]);
	}

	create_default_resources(pRenderer);
	/************************************************************************/
	/************************************************************************/
	if (fnHookPostInitRenderer != NULL)
		fnHookPostInitRenderer(pRenderer);

#ifndef _DURANGO
	if (pRenderer->mSettings.mShaderTarget >= shader_target_6_0)
	{
		HRESULT dxrSuccess = gDxcDllHelper.Initialize();
		if (!SUCCEEDED(dxrSuccess))
		{
			pRenderer = NULL;
			return;
		}
	}
#endif

	// Set shader macro based on runtime information
	ShaderMacro rendererShaderDefines[] =
	{
		// Descriptor set indices
		{ "UPDATE_FREQ_NONE",      "space0" },
		{ "UPDATE_FREQ_PER_FRAME", "space1" },
		{ "UPDATE_FREQ_PER_BATCH", "space2" },
		{ "UPDATE_FREQ_PER_DRAW",  "space3" },
	};
	pRenderer->mBuiltinShaderDefinesCount = sizeof(rendererShaderDefines) / sizeof(rendererShaderDefines[0]);
	pRenderer->pBuiltinShaderDefines = (ShaderMacro*)conf_calloc(pRenderer->mBuiltinShaderDefinesCount, sizeof(ShaderMacro));
	for (uint32_t i = 0; i < pRenderer->mBuiltinShaderDefinesCount; ++i)
	{
		conf_placement_new<ShaderMacro>(&pRenderer->pBuiltinShaderDefines[i]);
		pRenderer->pBuiltinShaderDefines[i] = rendererShaderDefines[i];
	}

	// Renderer is good! Assign it to result!
	*(ppRenderer) = pRenderer;
}

void removeRenderer(Renderer* pRenderer)
{
	ASSERT(pRenderer);

	for (uint32_t i = 0; i < pRenderer->mBuiltinShaderDefinesCount; ++i)
		pRenderer->pBuiltinShaderDefines[i].~ShaderMacro();
	SAFE_FREE(pRenderer->pBuiltinShaderDefines);

#ifndef _DURANGO
	if (gDxcDllHelper.IsEnabled())
	{
		gDxcDllHelper.Cleanup();
	}
#endif

	SAFE_FREE(pRenderer->pName);

	destroy_default_resources(pRenderer);

	// Destroy the Direct3D12 bits
	for (uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		remove_descriptor_heap(pRenderer->pCPUDescriptorHeaps[i]);
	}

	for (uint32_t i = 0; i < pRenderer->mNumOfGPUs; ++i)
	{
		remove_descriptor_heap(pRenderer->pCbvSrvUavHeaps[i]);
		remove_descriptor_heap(pRenderer->pSamplerHeaps[i]);
	}

	destroyAllocator(pRenderer->pResourceAllocator);
	RemoveDevice(pRenderer);

	if (fnHookPostRemoveRenderer != NULL)
		fnHookPostRemoveRenderer(pRenderer);

	// Free all the renderer components
	SAFE_FREE(pRenderer);
}
/************************************************************************/
// Resource Creation Functions
/************************************************************************/
void addFence(Renderer* pRenderer, Fence** ppFence)
{
	//ASSERT that renderer is valid
	ASSERT(pRenderer);

	//create a Fence and ASSERT that it is valid
	Fence* pFence = (Fence*)conf_calloc(1, sizeof(*pFence));
	ASSERT(pFence);

	HRESULT hres = pRenderer->pDxDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ARGS(&pFence->pDxFence));
	pFence->mFenceValue = 1;
	ASSERT(SUCCEEDED(hres));

	pFence->pDxWaitIdleFenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	//set given pointer to new fence
	*ppFence = pFence;
}

void removeFence(Renderer* pRenderer, Fence* pFence)
{
	//ASSERT that renderer is valid
	ASSERT(pRenderer);
	//ASSERT that given fence to remove is valid
	ASSERT(pFence);

	SAFE_RELEASE(pFence->pDxFence);
	CloseHandle(pFence->pDxWaitIdleFenceEvent);

	//delete memory
	SAFE_FREE(pFence);
}

void addSemaphore(Renderer* pRenderer, Semaphore** ppSemaphore)
{
	//ASSERT that renderer is valid
	ASSERT(pRenderer);

	//create a semaphore and ASSERT that it is valid
	Semaphore* pSemaphore = (Semaphore*)conf_calloc(1, sizeof(*pSemaphore));
	ASSERT(pSemaphore);

	::addFence(pRenderer, &pSemaphore->pFence);

	//save newly created semaphore in given pointer
	*ppSemaphore = pSemaphore;
}

void removeSemaphore(Renderer* pRenderer, Semaphore* pSemaphore)
{
	//ASSERT that renderer and given semaphore are valid
	ASSERT(pRenderer);
	ASSERT(pSemaphore);

	::removeFence(pRenderer, pSemaphore->pFence);

	//safe delete that check for valid pointer
	SAFE_FREE(pSemaphore);
}

void addQueue(Renderer* pRenderer, QueueDesc* pQDesc, Queue** ppQueue)
{
	Queue* pQueue = (Queue*)conf_calloc(1, sizeof(*pQueue));
	ASSERT(pQueue != NULL);
	if (pQDesc->mNodeIndex)
	{
		ASSERT(pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED && "Node Masking can only be used with Linked Multi GPU");
	}

	//provided description for queue creation
	pQueue->mQueueDesc = *pQDesc;

	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Flags = gDx12QueueFlagTranslator[pQueue->mQueueDesc.mFlag];
	queueDesc.Type = gDx12CmdTypeTranslator[pQueue->mQueueDesc.mType];
	queueDesc.Priority = gDx12QueuePriorityTranslator[pQueue->mQueueDesc.mPriority];
	queueDesc.NodeMask = util_calculate_node_mask(pRenderer, pQDesc->mNodeIndex);

#if defined(_DURANGO)
	HRESULT hr = create_command_queue(pRenderer, queueDesc, __uuidof(pQueue->pDxQueue), (void**)&(pQueue->pDxQueue));
	ASSERT(SUCCEEDED(hr));
#else
	HRESULT hr = pRenderer->pDxDevice->CreateCommandQueue(&queueDesc, __uuidof(pQueue->pDxQueue), (void**)&(pQueue->pDxQueue));
	ASSERT(SUCCEEDED(hr));
#endif

	eastl::string queueType;
	switch (queueDesc.Type)
	{
		case D3D12_COMMAND_LIST_TYPE_DIRECT: queueType = "GRAPHICS QUEUE"; break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE: queueType = "COMPUTE QUEUE"; break;
		case D3D12_COMMAND_LIST_TYPE_COPY: queueType = "COPY QUEUE"; break;
		default: break;
	}

	eastl::string queueName;
	queueName.sprintf("%s %u", queueType.c_str(), pQDesc->mNodeIndex);
	WCHAR finalName[MAX_PATH] = {};
	mbstowcs(finalName, queueName.c_str(), queueName.size());
	pQueue->pDxQueue->SetName(finalName);

	pQueue->pRenderer = pRenderer;
	pQueue->mUploadGranularity = { 1, 1, 1 };

	// Add queue fence. This fence will make sure we finish all GPU works before releasing the queue
	::addFence(pQueue->pRenderer, &pQueue->pQueueFence);

	*ppQueue = pQueue;
}

void removeQueue(Queue* pQueue)
{
	ASSERT(pQueue != NULL);

	// Make sure we finished all GPU works before we remove the queue
	waitQueueIdle(pQueue);

	::removeFence(pQueue->pRenderer, pQueue->pQueueFence);
	
	SAFE_RELEASE(pQueue->pDxQueue);
	SAFE_FREE(pQueue);
}

void addCmdPool(Renderer* pRenderer, Queue* pQueue, bool transient, CmdPool** ppCmdPool)
{
	UNREF_PARAM(transient);
	//ASSERT that renderer is valid
	ASSERT(pRenderer);

	//create one new CmdPool and add to renderer
	CmdPool* pCmdPool = (CmdPool*)conf_calloc(1, sizeof(*pCmdPool));
	ASSERT(pCmdPool);

	CmdPoolDesc defaultDesc = {};
	defaultDesc.mCmdPoolType = pQueue->mQueueDesc.mType;

	pCmdPool->pQueue = pQueue;
	pCmdPool->mCmdPoolDesc.mCmdPoolType = defaultDesc.mCmdPoolType;

	*ppCmdPool = pCmdPool;
}

void removeCmdPool(Renderer* pRenderer, CmdPool* pCmdPool)
{
	//check validity of given renderer and command pool
	ASSERT(pRenderer);
	ASSERT(pCmdPool);
	SAFE_FREE(pCmdPool);
}

void addCmd(CmdPool* pCmdPool, bool secondary, Cmd** ppCmd)
{

	UNREF_PARAM(secondary);
	//verify that given pool is valid
	ASSERT(pCmdPool);

	//allocate new command
	Cmd* pCmd = (Cmd*)conf_calloc(1, sizeof(*pCmd));
	ASSERT(pCmd);

	//set command pool of new command
	pCmd->pRenderer = pCmdPool->pQueue->pRenderer;
	pCmd->pCmdPool = pCmdPool;
	pCmd->mNodeIndex = pCmdPool->pQueue->mQueueDesc.mNodeIndex;

	//add command to pool
	//ASSERT(pCmdPool->pDxCmdAlloc);
	ASSERT(pCmdPool->pQueue->pRenderer);
	ASSERT(pCmdPool->mCmdPoolDesc.mCmdPoolType < CmdPoolType::MAX_CMD_TYPE);

	ASSERT(pCmd->pRenderer->pDxDevice);
	ASSERT(pCmdPool->mCmdPoolDesc.mCmdPoolType < CmdPoolType::MAX_CMD_TYPE);
	HRESULT hres = pCmd->pRenderer->pDxDevice->CreateCommandAllocator(
		gDx12CmdTypeTranslator[pCmdPool->mCmdPoolDesc.mCmdPoolType], __uuidof(pCmd->pDxCmdAlloc), (void**)&(pCmd->pDxCmdAlloc));
	ASSERT(SUCCEEDED(hres));

	ID3D12PipelineState* initialState = NULL;
	hres = pCmd->pRenderer->pDxDevice->CreateCommandList(
		pCmdPool->pQueue->pDxQueue->GetDesc().NodeMask, gDx12CmdTypeTranslator[pCmdPool->mCmdPoolDesc.mCmdPoolType], pCmd->pDxCmdAlloc,
		initialState, __uuidof(pCmd->pDxCmdList), (void**)&(pCmd->pDxCmdList));
	ASSERT(SUCCEEDED(hres));

	// Command lists are addd in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	hres = pCmd->pDxCmdList->Close();
	ASSERT(SUCCEEDED(hres));

	//set new command
	*ppCmd = pCmd;
}

#ifdef _DURANGO
void addCmd(CmdPool* pCmdPool, bool secondary, DmaCmd** ppCmd)
{
	UNREF_PARAM(secondary);
	//verify that given pool is valid
	ASSERT(pCmdPool);

	//allocate new command
	DmaCmd* pCmd = (DmaCmd*)conf_calloc(1, sizeof(*pCmd));
	ASSERT(pCmd);

	//set command pool of new command
	pCmd->pRenderer = pCmdPool->pQueue->pRenderer;
	pCmd->pCmdPool = pCmdPool;

	//add command to pool
	//ASSERT(pCmdPool->pDxCmdAlloc);
	ASSERT(pCmdPool->pQueue->pRenderer);
	ASSERT(pCmdPool->mCmdPoolDesc.mCmdPoolType < CmdPoolType::MAX_CMD_TYPE);

	ASSERT(pCmd->pRenderer->pDxDevice);
	ASSERT(pCmdPool->mCmdPoolDesc.mCmdPoolType < CmdPoolType::MAX_CMD_TYPE);
	HRESULT hres = pCmd->pRenderer->pDxDevice->CreateCommandAllocator(
		gDx12CmdTypeTranslator[pCmdPool->mCmdPoolDesc.mCmdPoolType], __uuidof(pCmd->pDxCmdAlloc), (void**)&(pCmd->pDxCmdAlloc));
	ASSERT(SUCCEEDED(hres));

	ID3D12PipelineState* initialState = NULL;

	hres = create_command_list(
		pCmdPool->pQueue->pDxQueue->GetDesc().NodeMask, gDx12CmdTypeTranslator[pCmdPool->mCmdPoolDesc.mCmdPoolType],
		&initialState, pCmd);
	ASSERT(SUCCEEDED(hres));

	// Command lists are addd in the recording state, but there is nothing
	// to record yet. The main loop expects it to be closed, so close it now.
	hres = pCmd->pDxCmdList->Close();
	ASSERT(SUCCEEDED(hres));

	//set new command
	*ppCmd = pCmd;
}
#endif

void removeCmd(CmdPool* pCmdPool, Cmd* pCmd)
{
	//verify that given command and pool are valid
	ASSERT(pCmdPool);
	ASSERT(pCmd);

	//remove command from pool
	SAFE_RELEASE(pCmd->pDxCmdAlloc);
	SAFE_RELEASE(pCmd->pDxCmdList);

	//delete command
	SAFE_FREE(pCmd);
}

#ifdef _DURANGO
void removeCmd(CmdPool* pCmdPool, DmaCmd* pCmd)
{
	//verify that given command and pool are valid
	ASSERT(pCmdPool);
	ASSERT(pCmd);

	//remove command from pool
	SAFE_RELEASE(pCmd->pDxCmdAlloc);
	SAFE_RELEASE(pCmd->pDxCmdList);

	//delete command
	SAFE_FREE(pCmd);
}
#endif

void addCmd_n(CmdPool* pCmdPool, bool secondary, uint32_t cmdCount, Cmd*** pppCmd)
{
	//verify that ***cmd is valid
	ASSERT(pppCmd);

	//create new n command depending on cmdCount
	Cmd** ppCmd = (Cmd**)conf_calloc(cmdCount, sizeof(*ppCmd));
	ASSERT(ppCmd);

	//add n new cmds to given pool
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		::addCmd(pCmdPool, secondary, &(ppCmd[i]));
	}
	//return new list of cmds
	*pppCmd = ppCmd;
}

#ifdef _DURANGO
void addCmd_n(CmdPool* pCmdPool, bool secondary, uint32_t cmdCount, DmaCmd*** pppCmd)
{
	//verify that ***cmd is valid
	ASSERT(pppCmd);

	//create new n command depending on cmdCount
	DmaCmd** ppCmd = (DmaCmd**)conf_calloc(cmdCount, sizeof(*ppCmd));
	ASSERT(ppCmd);

	//add n new cmds to given pool
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		::addCmd(pCmdPool, secondary, &(ppCmd[i]));
	}
	//return new list of cmds
	*pppCmd = ppCmd;
}
#endif

void removeCmd_n(CmdPool* pCmdPool, uint32_t cmdCount, Cmd** ppCmd)
{
	//verify that given command list is valid
	ASSERT(ppCmd);

	//remove every given cmd in array
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		::removeCmd(pCmdPool, ppCmd[i]);
	}

	SAFE_FREE(ppCmd);
}

#ifdef _DURANGO
void removeCmd_n(CmdPool* pCmdPool, uint32_t cmdCount, DmaCmd** ppCmd)
{
	//verify that given command list is valid
	ASSERT(ppCmd);

	//remove every given cmd in array
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		::removeCmd(pCmdPool, ppCmd[i]);
	}

	SAFE_FREE(ppCmd);
}
#endif

void toggleVSync(Renderer* pRenderer, SwapChain** ppSwapChain)
{
	ASSERT(*ppSwapChain);

	//set descriptor vsync boolean
	(*ppSwapChain)->mDesc.mEnableVsync = !(*ppSwapChain)->mDesc.mEnableVsync;
#ifndef _DURANGO
	if (!(*ppSwapChain)->mDesc.mEnableVsync)
	{
		(*ppSwapChain)->mFlags |= DXGI_PRESENT_ALLOW_TEARING;
	}
	else
	{
		(*ppSwapChain)->mFlags &= ~DXGI_PRESENT_ALLOW_TEARING;
	}
#endif

	//toggle vsync present flag (this can go up to 4 but we don't need to refresh on nth vertical sync)
	(*ppSwapChain)->mDxSyncInterval = ((*ppSwapChain)->mDxSyncInterval + 1) % 2;
}

void addSwapChain(Renderer* pRenderer, const SwapChainDesc* pDesc, SwapChain** ppSwapChain)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(ppSwapChain);

	SwapChain* pSwapChain = (SwapChain*)conf_calloc(1, sizeof(*pSwapChain));
	pSwapChain->mDesc = *pDesc;
	pSwapChain->mDxSyncInterval = pSwapChain->mDesc.mEnableVsync ? 1 : 0;

	if (pSwapChain->mDesc.mSampleCount > SAMPLE_COUNT_1)
	{
		LOGF(LogLevel::eWARNING, "DirectX12 does not support multi-sample swapchains. Falling back to single sample swapchain");
		pSwapChain->mDesc.mSampleCount = SAMPLE_COUNT_1;
	}

	DXGI_SWAP_CHAIN_DESC1 desc = {};
	desc.Width = pSwapChain->mDesc.mWidth;
	desc.Height = pSwapChain->mDesc.mHeight;
	desc.Format = util_to_dx_swapchain_format(pSwapChain->mDesc.mColorFormat);
	desc.Stereo = false;
	desc.SampleDesc.Count = 1;    // If multisampling is needed, we'll resolve it later
	desc.SampleDesc.Quality = pSwapChain->mDesc.mSampleQuality;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
	desc.BufferCount = pSwapChain->mDesc.mImageCount;
	desc.Scaling = DXGI_SCALING_STRETCH;
#ifdef _DURANGO
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
#else
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
#endif
	desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

#ifdef _DURANGO
	desc.Flags = desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM
					 ? DXGIX_SWAP_CHAIN_FLAG_COLORIMETRY_RGB_BT2020_ST2084 | DXGIX_SWAP_CHAIN_FLAG_AUTOMATIC_GAMEDVR_TONEMAP
					 : 0;
#else
	desc.Flags = 0;
#endif

#if !defined(_DURANGO)
	BOOL allowTearing = FALSE;
	pRenderer->pDXGIFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
	desc.Flags |= allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	pSwapChain->mFlags |= (!pDesc->mEnableVsync && allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
#endif

	if (fnHookModifySwapChainDesc)
		fnHookModifySwapChainDesc(&desc);

	IDXGISwapChain1* swapchain;

#ifdef _DURANGO
	HRESULT hres = create_swap_chain(pRenderer, pSwapChain, &desc, &swapchain);
	ASSERT(SUCCEEDED(hres));
#else
	HWND hwnd = (HWND)pSwapChain->mDesc.mWindowHandle.window;

	HRESULT hres =
		pRenderer->pDXGIFactory->CreateSwapChainForHwnd(pDesc->ppPresentQueues[0]->pDxQueue, hwnd, &desc, NULL, NULL, &swapchain);
	ASSERT(SUCCEEDED(hres));

	hres = pRenderer->pDXGIFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
	ASSERT(SUCCEEDED(hres));
#endif

	hres = swapchain->QueryInterface(__uuidof(pSwapChain->pDxSwapChain), (void**)&(pSwapChain->pDxSwapChain));
	ASSERT(SUCCEEDED(hres));
	swapchain->Release();

#ifndef _DURANGO
	// Allowing multiple command queues to present for applications like Alternate Frame Rendering
	if (pRenderer->mSettings.mGpuMode == GPU_MODE_LINKED && pDesc->mPresentQueueCount > 1)
	{
		ASSERT(pDesc->mPresentQueueCount == pDesc->mImageCount);

		IUnknown** ppQueues = (IUnknown**)alloca(pDesc->mPresentQueueCount * sizeof(IUnknown*));
		UINT*      pCreationMasks = (UINT*)alloca(pDesc->mPresentQueueCount * sizeof(UINT));
		for (uint32_t i = 0; i < pDesc->mPresentQueueCount; ++i)
		{
			ppQueues[i] = pDesc->ppPresentQueues[i]->pDxQueue;
			pCreationMasks[i] = (1 << pDesc->ppPresentQueues[i]->mQueueDesc.mNodeIndex);
		}

		if (pDesc->mPresentQueueCount)
		{
			pSwapChain->pDxSwapChain->ResizeBuffers1(
				desc.BufferCount, desc.Width, desc.Height, desc.Format, desc.Flags, pCreationMasks, ppQueues);
		}
	}
#endif

	// Create rendertargets from swapchain
	pSwapChain->ppDxSwapChainResources =
		(ID3D12Resource**)conf_calloc(pSwapChain->mDesc.mImageCount, sizeof(*pSwapChain->ppDxSwapChainResources));
	ASSERT(pSwapChain->ppDxSwapChainResources);
	for (uint32_t i = 0; i < pSwapChain->mDesc.mImageCount; ++i)
	{
		hres = pSwapChain->pDxSwapChain->GetBuffer(i, IID_ARGS(&pSwapChain->ppDxSwapChainResources[i]));
		ASSERT(SUCCEEDED(hres) && pSwapChain->ppDxSwapChainResources[i]);
	}

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

	for (uint32_t i = 0; i < pSwapChain->mDesc.mImageCount; ++i)
	{
		descColor.pNativeHandle = (void*)pSwapChain->ppDxSwapChainResources[i];
		::addRenderTarget(pRenderer, &descColor, &pSwapChain->ppSwapchainRenderTargets[i]);
	}

	*ppSwapChain = pSwapChain;
}

void removeSwapChain(Renderer* pRenderer, SwapChain* pSwapChain)
{
	for (unsigned i = 0; i < pSwapChain->mDesc.mImageCount; ++i)
	{
		::removeRenderTarget(pRenderer, pSwapChain->ppSwapchainRenderTargets[i]);
		SAFE_RELEASE(pSwapChain->ppDxSwapChainResources[i]);
	}

	SAFE_RELEASE(pSwapChain->pDxSwapChain);
	SAFE_FREE(pSwapChain->ppSwapchainRenderTargets);
	SAFE_FREE(pSwapChain->ppDxSwapChainResources);
	SAFE_FREE(pSwapChain);
}

void addBuffer(Renderer* pRenderer, const BufferDesc* pDesc, Buffer** pp_buffer)
{
	//verify renderer validity
	ASSERT(pRenderer);
	//verify adding at least 1 buffer
	ASSERT(pDesc);
	ASSERT(pDesc->mSize > 0);

	//allocate new buffer
	Buffer* pBuffer = (Buffer*)conf_calloc(1, sizeof(*pBuffer));
	ASSERT(pBuffer);

	//set properties
	pBuffer->mDesc = *pDesc;

	//add to renderer

	uint64_t allocationSize = pBuffer->mDesc.mSize;
	// Align the buffer size to multiples of 256
	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER))
	{
		allocationSize = round_up_64(allocationSize, pRenderer->pActiveGpuSettings->mUniformBufferAlignment);
	}

	DECLARE_ZERO(D3D12_RESOURCE_DESC, desc);
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	//Alignment must be 64KB (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) or 0, which is effectively 64KB.
	//https://msdn.microsoft.com/en-us/library/windows/desktop/dn903813(v=vs.85).aspx
	desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	desc.Width = allocationSize;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;

	if (fnHookAddBuffer != NULL)
		fnHookAddBuffer(pBuffer, desc);

	if (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER)
	{
		desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}

	// Adjust for padding
	UINT64 padded_size = 0;
	pRenderer->pDxDevice->GetCopyableFootprints(&desc, 0, 1, 0, NULL, NULL, NULL, &padded_size);
	allocationSize = (uint64_t)padded_size;
	desc.Width = padded_size;

	if (pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_CPU_TO_GPU || pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_CPU_ONLY)
	{
		pBuffer->mDesc.mStartState = RESOURCE_STATE_GENERIC_READ;
	}

	D3D12_RESOURCE_STATES res_states = util_to_dx_resource_state(pBuffer->mDesc.mStartState);

	AllocatorMemoryRequirements mem_reqs = { 0 };
	mem_reqs.usage = (ResourceMemoryUsage)pBuffer->mDesc.mMemoryUsage;
	mem_reqs.flags = 0;
	if (pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_OWN_MEMORY_BIT)
		mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_OWN_MEMORY_BIT;
	if (pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_PERSISTENT_MAP_BIT)
		mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_PERSISTENT_MAP_BIT;
	if (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_INDIRECT_BUFFER)
		mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_ALLOW_INDIRECT_BUFFER;
	if (pBuffer->mDesc.mNodeIndex || pBuffer->mDesc.pSharedNodeIndices)
		mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_OWN_MEMORY_BIT;

	BufferCreateInfo alloc_info = { &desc, res_states, pBuffer->mDesc.pDebugName };
	HRESULT          hres = d3d12_createBuffer(pRenderer->pResourceAllocator, &alloc_info, &mem_reqs, pBuffer);
	ASSERT(SUCCEEDED(hres));

	// If buffer is a suballocation use offset in heap else use zero offset (placed resource / committed resource)
	if (pBuffer->pDxAllocation->GetResource())
		pBuffer->mPositionInHeap = pBuffer->pDxAllocation->GetOffset();
	else
		pBuffer->mPositionInHeap = 0;

	pBuffer->mCurrentState = pBuffer->mDesc.mStartState;
	pBuffer->mDxGpuAddress = pBuffer->pDxResource->GetGPUVirtualAddress() + pBuffer->mPositionInHeap;

	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) &&
		!(pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION))
	{
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
		cbvDesc.BufferLocation = pBuffer->mDxGpuAddress;
		cbvDesc.SizeInBytes = (UINT)allocationSize;
		add_cbv(pRenderer, &cbvDesc, &pBuffer->mDxCbvHandle);
	}

	if (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_INDEX_BUFFER)
	{
		//set type of index (16 bit, 32 bit) int
		pBuffer->mDxIndexFormat = (INDEX_TYPE_UINT16 == pBuffer->mDesc.mIndexType) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
	}

	if (pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_VERTEX_BUFFER)
	{
		if (pBuffer->mDesc.mVertexStride == 0)
		{
			LOGF(LogLevel::eERROR, "Vertex Stride must be a non zero value");
			ASSERT(false);
		}
	}

	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_BUFFER) &&
		!(pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION))
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Buffer.FirstElement = pBuffer->mDesc.mFirstElement;
		srvDesc.Buffer.NumElements = (UINT)(pBuffer->mDesc.mElementCount);
		srvDesc.Buffer.StructureByteStride = (UINT)(pBuffer->mDesc.mStructStride);
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDesc.Format = (DXGI_FORMAT) TinyImageFormat_ToDXGI_FORMAT(pDesc->mFormat);
		if (DESCRIPTOR_TYPE_BUFFER_RAW == (pDesc->mDescriptors & DESCRIPTOR_TYPE_BUFFER_RAW))
		{
			if (pDesc->mFormat != TinyImageFormat_UNDEFINED)
				LOGF(LogLevel::eWARNING, "Raw buffers use R32 typeless format. Format will be ignored");
			srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			srvDesc.Buffer.Flags |= D3D12_BUFFER_SRV_FLAG_RAW;
		}
		// Cannot create a typed StructuredBuffer
		if (srvDesc.Format != DXGI_FORMAT_UNKNOWN)
		{
			srvDesc.Buffer.StructureByteStride = 0;
		}

		add_srv(pRenderer, pBuffer->pDxResource, &srvDesc, &pBuffer->mDxSrvHandle);
	}

	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER) &&
		!(pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION))
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.FirstElement = pBuffer->mDesc.mFirstElement;
		uavDesc.Buffer.NumElements = (UINT)(pBuffer->mDesc.mElementCount);
		uavDesc.Buffer.StructureByteStride = (UINT)(pBuffer->mDesc.mStructStride);
		uavDesc.Buffer.CounterOffsetInBytes = 0;
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
		if (DESCRIPTOR_TYPE_RW_BUFFER_RAW == (pDesc->mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER_RAW))
		{
			if (pDesc->mFormat != TinyImageFormat_UNDEFINED)
				LOGF(LogLevel::eWARNING, "Raw buffers use R32 typeless format. Format will be ignored");
			uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			uavDesc.Buffer.Flags |= D3D12_BUFFER_UAV_FLAG_RAW;
		}
		else if (pDesc->mFormat != TinyImageFormat_UNDEFINED)
		{
			uavDesc.Format = (DXGI_FORMAT) TinyImageFormat_ToDXGI_FORMAT(pDesc->mFormat);
			D3D12_FEATURE_DATA_FORMAT_SUPPORT FormatSupport = { uavDesc.Format, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
			HRESULT hr = pRenderer->pDxDevice->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &FormatSupport, sizeof(FormatSupport));
			if (!SUCCEEDED(hr) || !(FormatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) ||
				!(FormatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE))
			{
				// Format does not support UAV Typed Load
				LOGF(LogLevel::eWARNING, "Cannot use Typed UAV for buffer format %u", (uint32_t)pDesc->mFormat);
				uavDesc.Format = DXGI_FORMAT_UNKNOWN;
			}
		}
		// Cannot create a typed RWStructuredBuffer
		if (uavDesc.Format != DXGI_FORMAT_UNKNOWN)
		{
			uavDesc.Buffer.StructureByteStride = 0;
		}

		ID3D12Resource* pCounterResource = pBuffer->mDesc.pCounterBuffer ? pBuffer->mDesc.pCounterBuffer->pDxResource : NULL;
		add_uav(pRenderer, pBuffer->pDxResource, pCounterResource, &uavDesc, &pBuffer->mDxUavHandle);
	}

	*pp_buffer = pBuffer;
}

void removeBuffer(Renderer* pRenderer, Buffer* pBuffer)
{
	UNREF_PARAM(pRenderer);
	ASSERT(pRenderer);
	ASSERT(pBuffer);

	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_UNIFORM_BUFFER) &&
		!(pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION))
	{
		return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], pBuffer->mDxCbvHandle);
	}
	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_BUFFER) &&
		!(pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION))
	{
		return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], pBuffer->mDxSrvHandle);
	}
	if ((pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER) &&
		!(pBuffer->mDesc.mFlags & BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION))
	{
		return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], pBuffer->mDxUavHandle);
	}


	d3d12_destroyBuffer(pRenderer->pResourceAllocator, pBuffer);

	SAFE_FREE(pBuffer);
}

void mapBuffer(Renderer* pRenderer, Buffer* pBuffer, ReadRange* pRange)
{
	UNREF_PARAM(pRenderer);
	ASSERT(pBuffer->mDesc.mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to map non-cpu accessible resource");

	D3D12_RANGE range = { pBuffer->mPositionInHeap, pBuffer->mPositionInHeap + pBuffer->mDesc.mSize };
	if (pRange)
	{
		range.Begin += pRange->mOffset;
		range.End = range.Begin + pRange->mSize;
	}

	void* pData = NULL;
	HRESULT hr = pBuffer->pDxResource->Map(0, &range, &pData);
	ASSERT(SUCCEEDED(hr) && pData);

	pBuffer->pCpuMappedAddress = (uint8_t*)pData + pBuffer->mPositionInHeap;
}

void unmapBuffer(Renderer* pRenderer, Buffer* pBuffer)
{
	UNREF_PARAM(pRenderer);
	ASSERT(pBuffer->mDesc.mMemoryUsage != RESOURCE_MEMORY_USAGE_GPU_ONLY && "Trying to unmap non-cpu accessible resource");

	pBuffer->pDxResource->Unmap(0, NULL);
	pBuffer->pCpuMappedAddress = NULL;
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

	//allocate new texture
	Texture* pTexture = (Texture*)conf_calloc(1, sizeof(*pTexture));
	ASSERT(pTexture);

	//set texture properties
	pTexture->mDesc = *pDesc;

	if (pDesc->pNativeHandle)
	{
		pTexture->mOwnsImage = false;
		pTexture->pDxResource = (ID3D12Resource*)pDesc->pNativeHandle;
	}
	else
	{
		pTexture->mOwnsImage = true;
	}


	//add to gpu
	D3D12_RESOURCE_DESC desc = {};

	DXGI_FORMAT dxFormat = (DXGI_FORMAT) TinyImageFormat_ToDXGI_FORMAT(pDesc->mFormat);

	DescriptorType      descriptors = pDesc->mDescriptors;

	ASSERT(DXGI_FORMAT_UNKNOWN != dxFormat);

	if (NULL == pTexture->pDxResource)
	{
		D3D12_RESOURCE_DIMENSION res_dim = D3D12_RESOURCE_DIMENSION_UNKNOWN;
		if (pDesc->mFlags & TEXTURE_CREATION_FLAG_FORCE_2D)
		{
			ASSERT(pDesc->mDepth == 1);
			res_dim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		}
		else if (pDesc->mFlags & TEXTURE_CREATION_FLAG_FORCE_3D)
		{
			res_dim = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		}
		else
		{
			if (pDesc->mDepth > 1)
				res_dim = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
			else if (pDesc->mHeight > 1)
				res_dim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			else
				res_dim = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
		}

		desc.Dimension = res_dim;
		//On PC, If Alignment is set to 0, the runtime will use 4MB for MSAA textures and 64KB for everything else.
		//On XBox, We have to explicitlly assign D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT if MSAA is used
		desc.Alignment = (UINT)pDesc->mSampleCount > 1 ? D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT : 0;
		desc.Width = pDesc->mWidth;
		desc.Height = pDesc->mHeight;
		desc.DepthOrArraySize = (UINT16)(pDesc->mArraySize != 1 ? pDesc->mArraySize : pDesc->mDepth);
		desc.MipLevels = (UINT16)pDesc->mMipLevels;
		desc.Format = (DXGI_FORMAT) TinyImageFormat_DXGI_FORMATToTypeless((TinyImageFormat_DXGI_FORMAT)dxFormat);
		desc.SampleDesc.Count = (UINT)pDesc->mSampleCount;
		desc.SampleDesc.Quality = (UINT)pDesc->mSampleQuality;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		desc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS data;
		data.Format = desc.Format;
		data.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
		data.SampleCount = desc.SampleDesc.Count;
		pRenderer->pDxDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &data, sizeof(data));
		while (data.NumQualityLevels == 0 && data.SampleCount > 0)
		{
			LOGF(
				LogLevel::eWARNING, "Sample Count (%u) not supported. Trying a lower sample count (%u)", data.SampleCount,
				data.SampleCount / 2);
			data.SampleCount = desc.SampleDesc.Count / 2;
			pRenderer->pDxDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &data, sizeof(data));
		}
		desc.SampleDesc.Count = data.SampleCount;
		pTexture->mDesc.mSampleCount = (SampleCount)desc.SampleDesc.Count;

		// Decide UAV flags
		if (descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		// Decide render target flags
		if (pDesc->mStartState & RESOURCE_STATE_RENDER_TARGET)
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		else if (pDesc->mStartState & RESOURCE_STATE_DEPTH_WRITE)
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

		// Decide sharing flags
		if (pDesc->mFlags & TEXTURE_CREATION_FLAG_EXPORT_ADAPTER_BIT)
		{
			desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
			desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		}

		if (fnHookResourceFlags != NULL)
			fnHookResourceFlags(desc.Flags, pDesc->mFlags);

		DECLARE_ZERO(D3D12_CLEAR_VALUE, clearValue);
		clearValue.Format = dxFormat;
		if (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)
		{
			clearValue.DepthStencil.Depth = pDesc->mClearValue.depth;
			clearValue.DepthStencil.Stencil = (UINT8)pDesc->mClearValue.stencil;
		}
		else
		{
			clearValue.Color[0] = pDesc->mClearValue.r;
			clearValue.Color[1] = pDesc->mClearValue.g;
			clearValue.Color[2] = pDesc->mClearValue.b;
			clearValue.Color[3] = pDesc->mClearValue.a;
		}

		D3D12_CLEAR_VALUE*    pClearValue = NULL;
		D3D12_RESOURCE_STATES res_states = util_to_dx_resource_state(pDesc->mStartState);

		if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) || (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL))
		{
			pClearValue = &clearValue;
		}

		AllocatorMemoryRequirements mem_reqs = { 0 };
		mem_reqs.usage = (ResourceMemoryUsage)RESOURCE_MEMORY_USAGE_GPU_ONLY;
		if (pDesc->mFlags & TEXTURE_CREATION_FLAG_OWN_MEMORY_BIT)
			mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_OWN_MEMORY_BIT;
		if (pDesc->mFlags & TEXTURE_CREATION_FLAG_EXPORT_BIT)
			mem_reqs.flags |= (RESOURCE_MEMORY_REQUIREMENT_SHARED_BIT | RESOURCE_MEMORY_REQUIREMENT_OWN_MEMORY_BIT);
		if (pDesc->mFlags & TEXTURE_CREATION_FLAG_EXPORT_ADAPTER_BIT)
			mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_SHARED_ADAPTER_BIT;
		if (pDesc->mNodeIndex > 0 || pDesc->pSharedNodeIndices)
			mem_reqs.flags |= RESOURCE_MEMORY_REQUIREMENT_OWN_MEMORY_BIT;

		TextureCreateInfo alloc_info = { pDesc, &desc, pClearValue, res_states, pDesc->pDebugName };
		HRESULT           hr = d3d12_createTexture(pRenderer->pResourceAllocator, &alloc_info, &mem_reqs, pTexture);
		ASSERT(SUCCEEDED(hr));

		pTexture->mCurrentState = pDesc->mStartState;
	}
	else
	{
		desc = pTexture->pDxResource->GetDesc();
		dxFormat = desc.Format;
	}

	// Compute texture size
	UINT64 buffer_size = 0;
	pRenderer->pDxDevice->GetCopyableFootprints(
		&desc, 0, desc.MipLevels * (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : desc.DepthOrArraySize), 0, NULL, NULL, NULL,
		&buffer_size);
	pTexture->mTextureSize = buffer_size;

	D3D12_SHADER_RESOURCE_VIEW_DESC  srvDesc = {};
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	switch (desc.Dimension)
	{
		case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
		{
			if (desc.DepthOrArraySize > 1)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
				uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
				// SRV
				srvDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
				srvDesc.Texture1DArray.FirstArraySlice = 0;
				srvDesc.Texture1DArray.MipLevels = desc.MipLevels;
				srvDesc.Texture1DArray.MostDetailedMip = 0;
				// UAV
				uavDesc.Texture1DArray.ArraySize = desc.DepthOrArraySize;
				uavDesc.Texture1DArray.FirstArraySlice = 0;
				uavDesc.Texture1DArray.MipSlice = 0;
			}
			else
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
				uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
				// SRV
				srvDesc.Texture1D.MipLevels = desc.MipLevels;
				srvDesc.Texture1D.MostDetailedMip = 0;
				// UAV
				uavDesc.Texture1D.MipSlice = 0;
			}
			break;
		}
		case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
		{
			if (DESCRIPTOR_TYPE_TEXTURE_CUBE == (descriptors & DESCRIPTOR_TYPE_TEXTURE_CUBE))
			{
				ASSERT(desc.DepthOrArraySize % 6 == 0);

				if (desc.DepthOrArraySize > 6)
				{
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
					// SRV
					srvDesc.TextureCubeArray.First2DArrayFace = 0;
					srvDesc.TextureCubeArray.MipLevels = desc.MipLevels;
					srvDesc.TextureCubeArray.MostDetailedMip = 0;
					srvDesc.TextureCubeArray.NumCubes = desc.DepthOrArraySize / 6;
				}
				else
				{
					srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
					// SRV
					srvDesc.TextureCube.MipLevels = desc.MipLevels;
					srvDesc.TextureCube.MostDetailedMip = 0;
				}

				// UAV
				uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
				uavDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
				uavDesc.Texture2DArray.FirstArraySlice = 0;
				uavDesc.Texture2DArray.MipSlice = 0;
				uavDesc.Texture2DArray.PlaneSlice = 0;
			}
			else
			{
				if (desc.DepthOrArraySize > 1)
				{
					if (desc.SampleDesc.Count > SAMPLE_COUNT_1)
					{
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
						// Cannot create a multisampled uav
						// SRV
						srvDesc.Texture2DMSArray.ArraySize = desc.DepthOrArraySize;
						srvDesc.Texture2DMSArray.FirstArraySlice = 0;
						// No UAV
					}
					else
					{
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
						uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
						// SRV
						srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
						srvDesc.Texture2DArray.FirstArraySlice = 0;
						srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
						srvDesc.Texture2DArray.MostDetailedMip = 0;
						srvDesc.Texture2DArray.PlaneSlice = 0;
						// UAV
						uavDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
						uavDesc.Texture2DArray.FirstArraySlice = 0;
						uavDesc.Texture2DArray.MipSlice = 0;
						uavDesc.Texture2DArray.PlaneSlice = 0;
					}
				}
				else
				{
					if (desc.SampleDesc.Count > SAMPLE_COUNT_1)
					{
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
						// Cannot create a multisampled uav
					}
					else
					{
						srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
						uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
						// SRV
						srvDesc.Texture2D.MipLevels = desc.MipLevels;
						srvDesc.Texture2D.MostDetailedMip = 0;
						srvDesc.Texture2D.PlaneSlice = 0;
						// UAV
						uavDesc.Texture2D.MipSlice = 0;
						uavDesc.Texture2D.PlaneSlice = 0;
					}
				}
			}
			break;
		}
		case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
		{
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
			// SRV
			srvDesc.Texture3D.MipLevels = desc.MipLevels;
			srvDesc.Texture3D.MostDetailedMip = 0;
			// UAV
			uavDesc.Texture3D.MipSlice = 0;
			uavDesc.Texture3D.FirstWSlice = 0;
			uavDesc.Texture3D.WSize = desc.DepthOrArraySize;
			break;
		}
		default: break;
	}

	if (descriptors & DESCRIPTOR_TYPE_TEXTURE)
	{
		ASSERT(srvDesc.ViewDimension != D3D12_SRV_DIMENSION_UNKNOWN);

		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = util_to_dx_srv_format(dxFormat);
		add_srv(pRenderer, pTexture->pDxResource, &srvDesc, &pTexture->mDxSRVDescriptor);
	}

	if (descriptors & DESCRIPTOR_TYPE_RW_TEXTURE)
	{
		uavDesc.Format = util_to_dx_uav_format(dxFormat);
		pTexture->pDxUAVDescriptors = (D3D12_CPU_DESCRIPTOR_HANDLE*)conf_calloc(desc.MipLevels, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));
		for (uint32_t i = 0; i < pDesc->mMipLevels; ++i)
		{
			uavDesc.Texture1DArray.MipSlice = i;
			if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
				uavDesc.Texture3D.WSize = desc.DepthOrArraySize / (UINT)pow(2, i);
			add_uav(pRenderer, pTexture->pDxResource, NULL, &uavDesc, &pTexture->pDxUAVDescriptors[i]);
		}
	}

	if (pDesc->pDebugName)
	{
		pTexture->mDesc.pDebugName = (wchar_t*)conf_calloc(wcslen(pDesc->pDebugName) + 1, sizeof(wchar_t));
		wcscpy((wchar_t*)pTexture->mDesc.pDebugName, pDesc->pDebugName);
	}

	//save tetxure in given pointer
	*ppTexture = pTexture;

	// TODO: Handle host visible textures in a better way
	if (pDesc->mHostVisible)
	{
		internal_log(
			LOG_TYPE_WARN,
			"D3D12 does not support host visible textures, memory of resulting texture will not be mapped for CPU visibility",
			"addTexture");
	}
}

// Allocate memory for the virtual page
bool allocateVirtualPage(Renderer* pRenderer, Texture* pTexture, VirtualTexturePage &virtualPage)
{
	if (virtualPage.pIntermediateBuffer != NULL)
	{
		//already filled
		return false;
	};

	BufferDesc desc = {};
	desc.mDescriptors = DESCRIPTOR_TYPE_RW_BUFFER;
	desc.mMemoryUsage = RESOURCE_MEMORY_USAGE_CPU_TO_GPU;
	desc.mFlags = BUFFER_CREATION_FLAG_OWN_MEMORY_BIT | BUFFER_CREATION_FLAG_NO_DESCRIPTOR_VIEW_CREATION;
	//desc.mFormat = pTexture->mDesc.mFormat;
	desc.mStartState = RESOURCE_STATE_COPY_SOURCE;

	desc.mFirstElement = 0;
	desc.mElementCount = pTexture->mSparseVirtualTexturePageWidth * pTexture->mSparseVirtualTexturePageHeight;
	desc.mStructStride = sizeof(uint32_t);
	desc.mSize = desc.mElementCount * desc.mStructStride;
	addBuffer(pRenderer, &desc, &virtualPage.pIntermediateBuffer);
	return true;
}


// Release memory allocated for this page
void releaseVirtualPage(Renderer* pRenderer, VirtualTexturePage &virtualPage, bool removeMemoryBind)
{
	if (virtualPage.pIntermediateBuffer)
	{
		removeBuffer(pRenderer, virtualPage.pIntermediateBuffer);
		virtualPage.pIntermediateBuffer = NULL;
	}
}

void removeTexture(Renderer* pRenderer, Texture* pTexture)
{
	ASSERT(pRenderer);
	ASSERT(pTexture);

	//delete texture descriptors
	if (pTexture->mDxSRVDescriptor.ptr != D3D12_GPU_VIRTUAL_ADDRESS_NULL)
		return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], pTexture->mDxSRVDescriptor);

	if (pTexture->pDxUAVDescriptors)
	{
		for (uint32_t i = 0; i < pTexture->mDesc.mMipLevels; ++i)
		{
			return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV], pTexture->pDxUAVDescriptors[i]);
		}
	}

	if (pTexture->mOwnsImage)
	{
		d3d12_destroyTexture(pRenderer->pResourceAllocator, pTexture);
	}

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;

	if (pPageTable)
	{
		for (int i = 0; i < (int)pPageTable->size(); i++)
		{
			releaseVirtualPage(pRenderer, (*pPageTable)[i], true);
		}

		pPageTable->set_capacity(0);
		SAFE_FREE(pTexture->pPages);
	}

	eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>* pSparseCoordinates = (eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>*)pTexture->pSparseCoordinates;

	if (pSparseCoordinates)
	{
		pSparseCoordinates->set_capacity(0);
		SAFE_FREE(pTexture->pSparseCoordinates);
	}

	eastl::vector<uint32_t>* pHeapRangeStartOffsets = (eastl::vector<uint32_t>*)pTexture->pHeapRangeStartOffsets;

	if (pHeapRangeStartOffsets)
	{
		pHeapRangeStartOffsets->set_capacity(0);
		SAFE_FREE(pTexture->pHeapRangeStartOffsets);
	}

	if(pTexture->pSparseImageMemory)
	{
		pTexture->pSparseImageMemory->Release();
		pTexture->pDxResource->Release();
	}

	if (pTexture->mVisibility)
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

	if (pTexture->mVirtualImageData)
		conf_free(pTexture->mVirtualImageData);

	SAFE_FREE((wchar_t*)pTexture->mDesc.pDebugName);
	SAFE_FREE(pTexture->pDxUAVDescriptors);
	SAFE_FREE(pTexture);
}

void addRenderTarget(Renderer* pRenderer, const RenderTargetDesc* pDesc, RenderTarget** ppRenderTarget)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(ppRenderTarget);

	bool const isDepth = 	TinyImageFormat_IsDepthAndStencil(pDesc->mFormat) ||
												TinyImageFormat_IsDepthOnly(pDesc->mFormat);

	ASSERT(!((isDepth) && (pDesc->mDescriptors & DESCRIPTOR_TYPE_RW_TEXTURE)) && "Cannot use depth stencil as UAV");

	((RenderTargetDesc*)pDesc)->mMipLevels = max(1U, pDesc->mMipLevels);

	RenderTarget* pRenderTarget = (RenderTarget*)conf_calloc(1, sizeof(*pRenderTarget));
	pRenderTarget->mDesc = *pDesc;

	//add to gpu
	DXGI_FORMAT dxFormat = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(pRenderTarget->mDesc.mFormat);
	ASSERT(DXGI_FORMAT_UNKNOWN != dxFormat);

	TextureDesc textureDesc = {};
	textureDesc.mArraySize = pDesc->mArraySize;
	textureDesc.mClearValue = pDesc->mClearValue;
	textureDesc.mDepth = pDesc->mDepth;
	textureDesc.mFlags = pDesc->mFlags;
	textureDesc.mFormat = pDesc->mFormat;
	textureDesc.mHeight = pDesc->mHeight;
	textureDesc.mMipLevels = pDesc->mMipLevels;
	textureDesc.mSampleCount = pDesc->mSampleCount;
	textureDesc.mSampleQuality = pDesc->mSampleQuality;

	if (!isDepth)
		textureDesc.mStartState |= RESOURCE_STATE_RENDER_TARGET;
	else
		textureDesc.mStartState |= RESOURCE_STATE_DEPTH_WRITE;

	// Set this by default to be able to sample the rendertarget in shader
	textureDesc.mWidth = pDesc->mWidth;
	textureDesc.pNativeHandle = pDesc->pNativeHandle;
	textureDesc.pDebugName = pDesc->pDebugName;
	textureDesc.mNodeIndex = pDesc->mNodeIndex;
	textureDesc.pSharedNodeIndices = pDesc->pSharedNodeIndices;
	textureDesc.mSharedNodeIndexCount = pDesc->mSharedNodeIndexCount;
	textureDesc.mDescriptors = pDesc->mDescriptors;
	// Create SRV by default for a render target
	textureDesc.mDescriptors |= DESCRIPTOR_TYPE_TEXTURE;

	addTexture(pRenderer, &textureDesc, &pRenderTarget->pTexture);

	D3D12_RESOURCE_DESC desc = pRenderTarget->pTexture->pDxResource->GetDesc();

	uint32_t numRTVs = desc.MipLevels;
	if ((pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		numRTVs *= desc.DepthOrArraySize;

	pRenderTarget->pDxDescriptors = (D3D12_CPU_DESCRIPTOR_HANDLE*)conf_calloc(numRTVs + 1, sizeof(D3D12_CPU_DESCRIPTOR_HANDLE));

	if(isDepth)	{
		add_dsv(pRenderer, pRenderTarget->pTexture->pDxResource, dxFormat, 0, -1, &pRenderTarget->pDxDescriptors[0]);
	}	else {
		add_rtv(pRenderer, pRenderTarget->pTexture->pDxResource, dxFormat, 0, -1, &pRenderTarget->pDxDescriptors[0]);
	}

	for (uint32_t i = 0; i < desc.MipLevels; ++i)
	{
		if ((pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
			(pDesc->mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
		{
			for (uint32_t j = 0; j < desc.DepthOrArraySize; ++j)
			{
				if(isDepth) {
					add_dsv(
							pRenderer, pRenderTarget->pTexture->pDxResource, dxFormat, i, j,
							&pRenderTarget->pDxDescriptors[1 + i * desc.DepthOrArraySize + j]);
				} else {
					add_rtv(
							pRenderer, pRenderTarget->pTexture->pDxResource, dxFormat, i, j,
							&pRenderTarget->pDxDescriptors[1 + i * desc.DepthOrArraySize + j]);
				}
			}
		}
		else
		{
			if(isDepth) {
				add_dsv(pRenderer, pRenderTarget->pTexture->pDxResource, dxFormat, i, -1, &pRenderTarget->pDxDescriptors[1 + i]);
			} else {
				add_rtv(pRenderer, pRenderTarget->pTexture->pDxResource, dxFormat, i, -1, &pRenderTarget->pDxDescriptors[1 + i]);
			}
		}
	}

	*ppRenderTarget = pRenderTarget;
}

void removeRenderTarget(Renderer* pRenderer, RenderTarget* pRenderTarget)
{
	bool const isDepth = 	TinyImageFormat_IsDepthAndStencil(pRenderTarget->mDesc.mFormat) ||
												TinyImageFormat_IsDepthOnly(pRenderTarget->mDesc.mFormat);

	removeTexture(pRenderer, pRenderTarget->pTexture);

	!isDepth ?
		return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV], pRenderTarget->pDxDescriptors[0]) :
		return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV], pRenderTarget->pDxDescriptors[0]);

	const uint32_t depthOrArraySize = pRenderTarget->mDesc.mArraySize * pRenderTarget->mDesc.mDepth;
	if ((pRenderTarget->mDesc.mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_ARRAY_SLICES) ||
		(pRenderTarget->mDesc.mDescriptors & DESCRIPTOR_TYPE_RENDER_TARGET_DEPTH_SLICES))
	{
		for (uint32_t i = 0; i < pRenderTarget->mDesc.mMipLevels; ++i)
			for (uint32_t j = 0; j < depthOrArraySize; ++j)
				!isDepth ?
				return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV], pRenderTarget->pDxDescriptors[1 + i * depthOrArraySize + j]) :
				return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV], pRenderTarget->pDxDescriptors[1 + i * depthOrArraySize + j]);
	}
	else
	{
		for (uint32_t i = 0; i < pRenderTarget->mDesc.mMipLevels; ++i)
			!isDepth ?
			return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_RTV], pRenderTarget->pDxDescriptors[1 + i]) :
			return_cpu_descriptor_handle(pRenderer->pCPUDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_DSV], pRenderTarget->pDxDescriptors[1 + i]);
	}

	SAFE_FREE(pRenderTarget->pDxDescriptors);
	SAFE_FREE(pRenderTarget);
}

void addSampler(Renderer* pRenderer, const SamplerDesc* pDesc, Sampler** ppSampler)
{
	ASSERT(pRenderer);
	ASSERT(pRenderer->pDxDevice);
	ASSERT(pDesc->mCompareFunc < MAX_COMPARE_MODES);

	//allocate new sampler
	Sampler* pSampler = (Sampler*)conf_calloc(1, sizeof(*pSampler));
	ASSERT(pSampler);

	D3D12_SAMPLER_DESC desc = {};
	//add sampler to gpu
	desc.Filter = util_to_dx_filter(
		pDesc->mMinFilter, pDesc->mMagFilter, pDesc->mMipMapMode, pDesc->mMaxAnisotropy > 0.0f,
		(pDesc->mCompareFunc != CMP_NEVER ? true : false));
	desc.AddressU = util_to_dx_texture_address_mode(pDesc->mAddressU);
	desc.AddressV = util_to_dx_texture_address_mode(pDesc->mAddressV);
	desc.AddressW = util_to_dx_texture_address_mode(pDesc->mAddressW);
	desc.MipLODBias = pDesc->mMipLodBias;
	desc.MaxAnisotropy = max((UINT)pDesc->mMaxAnisotropy, 1U);
	desc.ComparisonFunc = gDx12ComparisonFuncTranslator[pDesc->mCompareFunc];
	desc.BorderColor[0] = 0.0f;
	desc.BorderColor[1] = 0.0f;
	desc.BorderColor[2] = 0.0f;
	desc.BorderColor[3] = 0.0f;
	desc.MinLOD = 0.0f;
	desc.MaxLOD = ((pDesc->mMipMapMode == MIPMAP_MODE_LINEAR) ? D3D12_FLOAT32_MAX : 0.0f);;
	pSampler->mDxDesc = desc;
	add_sampler(pRenderer, &pSampler->mDxDesc, &pSampler->mDxSamplerHandle);

	*ppSampler = pSampler;
}

void removeSampler(Renderer* pRenderer, Sampler* pSampler)
{
	ASSERT(pRenderer);
	ASSERT(pSampler);

	//remove_sampler(pRenderer, &pSampler->mDxSamplerHandle);

	// Nop op

	SAFE_FREE(pSampler);
}
/************************************************************************/
// Shader Functions
/************************************************************************/
template <typename BlobType>
static inline eastl::string convertBlobToString(BlobType* pBlob)
{
	eastl::vector<char> infoLog(pBlob->GetBufferSize() + 1);
	memcpy(infoLog.data(), pBlob->GetBufferPointer(), pBlob->GetBufferSize());
	infoLog[pBlob->GetBufferSize()] = 0;
	return eastl::string(infoLog.data());
}

void compileShader(
	Renderer* pRenderer, ShaderTarget shaderTarget, ShaderStage stage, const Path* filePath, uint32_t codeSize, const char* code,
	uint32_t macroCount, ShaderMacro* pMacros, void* (*allocator)(size_t a, const char *f, int l, const char *sf), uint32_t* pByteCodeSize, char** ppByteCode,
	const char* pEntryPoint)
{
	if (shaderTarget > pRenderer->mSettings.mShaderTarget)
	{
		LOGF( LogLevel::eERROR,
			"Requested shader target (%u) is higher than the shader target that the renderer supports (%u). Shader wont be compiled",
			(uint32_t)shaderTarget, (uint32_t)pRenderer->mSettings.mShaderTarget);
		return;
	}
#ifndef _DURANGO
	if (shaderTarget >= shader_target_6_0)
	{
#define d3d_call(x)      \
	if (!SUCCEEDED((x))) \
	{                    \
		ASSERT(false);   \
		return;          \
	}

		IDxcCompiler* pCompiler;
		IDxcLibrary*  pLibrary;
		d3d_call(gDxcDllHelper.CreateInstance(CLSID_DxcCompiler, &pCompiler));
		d3d_call(gDxcDllHelper.CreateInstance(CLSID_DxcLibrary, &pLibrary));

		/************************************************************************/
		// Determine shader target
		/************************************************************************/
		int major;
		int minor;
		switch (shaderTarget)
		{
			default:
			case shader_target_6_0:
			{
				major = 6;
				minor = 0;
				break;
			}
			case shader_target_6_1:
			{
				major = 6;
				minor = 1;
				break;
			}
			case shader_target_6_2:
			{
				major = 6;
				minor = 2;
				break;
			}
			case shader_target_6_3:
			{
				major = 6;
				minor = 3;
				break;
			}
		}
		eastl::string target;
		switch (stage)
		{
			case SHADER_STAGE_VERT: target.sprintf("vs_%d_%d", major, minor); break;
			case SHADER_STAGE_TESC: target.sprintf("hs_%d_%d", major, minor); break;
			case SHADER_STAGE_TESE: target.sprintf("ds_%d_%d", major, minor); break;
			case SHADER_STAGE_GEOM: target.sprintf("gs_%d_%d", major, minor); break;
			case SHADER_STAGE_FRAG: target.sprintf("ps_%d_%d", major, minor); break;
			case SHADER_STAGE_COMP: target.sprintf("cs_%d_%d", major, minor); break;
#ifdef ENABLE_RAYTRACING
			case SHADER_STAGE_RAYTRACING:
			{
				target.sprintf("lib_%d_%d", major, minor);
				ASSERT(shaderTarget >= shader_target_6_3);
				break;
			}
#else
			return;
#endif
			default: break;
		}
		WCHAR* wTarget = (WCHAR*)alloca((target.size() + 1) * sizeof(WCHAR));
		mbstowcs(wTarget, target.c_str(), target.size());
		wTarget[target.size()] = L'\0';
		/************************************************************************/
		// Collect macros
		/************************************************************************/
		uint32_t namePoolSize = 0;
		for (uint32_t i = 0; i < macroCount; ++i)
		{
			namePoolSize += (uint32_t)strlen(pMacros[i].definition) + 1;
			namePoolSize += (uint32_t)strlen(pMacros[i].value) + 1;
		}
		WCHAR* namePool = NULL;
		if (namePoolSize)
			namePool = (WCHAR*)alloca(namePoolSize * sizeof(WCHAR));

		// Extract shader macro definitions into D3D_SHADER_MACRO scruct
		// Allocate Size+2 structs: one for D3D12 1 definition and one for null termination
		DxcDefine* macros = (DxcDefine*)alloca((macroCount + 1) * sizeof(DxcDefine));
		macros[0] = { L"D3D12", L"1" };
		WCHAR* pCurrent = namePool;
		for (uint32_t j = 0; j < macroCount; ++j)
		{
			uint32_t len = (uint32_t)strlen(pMacros[j].definition);
			mbstowcs(pCurrent, pMacros[j].definition, len);
			pCurrent[len] = L'\0';
			macros[j + 1].Name = pCurrent;
			pCurrent += (len + 1);

			len = (uint32_t)strlen(pMacros[j].value);
			mbstowcs(pCurrent, pMacros[j].value, len);
			pCurrent[len] = L'\0';
			macros[j + 1].Value = pCurrent;
			pCurrent += (len + 1);
		}
		/************************************************************************/
		// Compiler args
		/************************************************************************/
		eastl::vector<const WCHAR*> compilerArgs;
		compilerArgs.push_back(L"-Zi");
		compilerArgs.push_back(L"-all_resources_bound");
#if defined(_DEBUG)
		compilerArgs.push_back(L"-Od");
#else
		compilerArgs.push_back(L"-O3");
#endif
		/************************************************************************/
		// Create blob from the string
		/************************************************************************/
		IDxcBlobEncoding* pTextBlob;
		d3d_call(pLibrary->CreateBlobWithEncodingFromPinned((LPBYTE)code, (UINT32)codeSize, 0, &pTextBlob));
		IDxcOperationResult* pResult;
		WCHAR                filename[MAX_PATH] = {};
		const char*			 pathStr = fsGetPathAsNativeString(filePath);
		mbstowcs(filename, pathStr, min(strlen(pathStr), MAX_PATH));
		IDxcIncludeHandler* pInclude = NULL;
		pLibrary->CreateIncludeHandler(&pInclude);

		WCHAR* entryName = L"main";
		if (pEntryPoint != NULL)
		{
			entryName = (WCHAR*)conf_calloc(strlen(pEntryPoint) + 1, sizeof(WCHAR));
			mbstowcs(entryName, pEntryPoint, strlen(pEntryPoint));
		}

		d3d_call(pCompiler->Compile(
			pTextBlob, filename, entryName, wTarget, compilerArgs.data(), (UINT32)compilerArgs.size(), macros, macroCount + 1, pInclude,
			&pResult));

		if (pEntryPoint != NULL)
		{
			conf_free(entryName);
			entryName = NULL;
		}

		pInclude->Release();
		pLibrary->Release();
		pCompiler->Release();
		/************************************************************************/
		// Verify the result
		/************************************************************************/
		HRESULT resultCode;
		d3d_call(pResult->GetStatus(&resultCode));
		if (FAILED(resultCode))
		{
			IDxcBlobEncoding* pError;
			d3d_call(pResult->GetErrorBuffer(&pError));
			eastl::string log = convertBlobToString(pError);
			LOGF( LogLevel::eERROR, log.c_str());
			pError->Release();
			return;
		}
		/************************************************************************/
		// Collect blob
		/************************************************************************/
		IDxcBlob* pBlob;
		d3d_call(pResult->GetResult(&pBlob));

		char* pByteCode = (char*)allocator(pBlob->GetBufferSize(), __FILE__, __LINE__, __FUNCTION__);
		memcpy(pByteCode, pBlob->GetBufferPointer(), pBlob->GetBufferSize());
		*pByteCodeSize = (uint32_t)pBlob->GetBufferSize();
		*ppByteCode = pByteCode;

		pBlob->Release();
		/************************************************************************/
		/************************************************************************/
	}
	else
#endif
	{
#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compile_flags = D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

		compile_flags |= (D3DCOMPILE_DEBUG | D3DCOMPILE_ALL_RESOURCES_BOUND | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES);

		int major;
		int minor;
		switch (shaderTarget)
		{
			default:
			case shader_target_5_1:
			{
				major = 5;
				minor = 1;
			}
			break;
			case shader_target_6_0:
			{
				major = 6;
				minor = 0;
			}
			break;
		}

		eastl::string target;
		switch (stage)
		{
			case SHADER_STAGE_VERT: target.sprintf("vs_%d_%d", major, minor); break;
			case SHADER_STAGE_TESC: target.sprintf("hs_%d_%d", major, minor); break;
			case SHADER_STAGE_TESE: target.sprintf("ds_%d_%d", major, minor); break;
			case SHADER_STAGE_GEOM: target.sprintf("gs_%d_%d", major, minor); break;
			case SHADER_STAGE_FRAG: target.sprintf("ps_%d_%d", major, minor); break;
			case SHADER_STAGE_COMP: target.sprintf("cs_%d_%d", major, minor); break;
			default: break;
		}

		// Extract shader macro definitions into D3D_SHADER_MACRO scruct
		// Allocate Size+2 structs: one for D3D12 1 definition and one for null termination
		D3D_SHADER_MACRO* macros = (D3D_SHADER_MACRO*)alloca((macroCount + 2) * sizeof(D3D_SHADER_MACRO));
		macros[0] = { "D3D12", "1" };
		for (uint32_t j = 0; j < macroCount; ++j)
		{
			macros[j + 1] = { pMacros[j].definition, pMacros[j].value };
		}
		macros[macroCount + 1] = { NULL, NULL };

		if (fnHookShaderCompileFlags != NULL)
			fnHookShaderCompileFlags(compile_flags);

		eastl::string entryPoint = "main";
		ID3DBlob*     compiled_code = NULL;
		ID3DBlob*     error_msgs = NULL;
		HRESULT       hres = D3DCompile2(
            code, (size_t)codeSize, fsGetPathAsNativeString(filePath), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint.c_str(), target.c_str(), compile_flags,
            0, 0, NULL, 0, &compiled_code, &error_msgs);
		if (FAILED(hres))
		{
			char* msg = (char*)conf_calloc(error_msgs->GetBufferSize() + 1, sizeof(*msg));
			ASSERT(msg);
			memcpy(msg, error_msgs->GetBufferPointer(), error_msgs->GetBufferSize());
			eastl::string error = eastl::string(fsGetPathAsNativeString(filePath)) + " " + msg;
			LOGF( LogLevel::eERROR, error.c_str());
			SAFE_FREE(msg);
		}
		ASSERT(SUCCEEDED(hres));

		char* pByteCode = (char*)allocator(compiled_code->GetBufferSize(), __FILE__, __LINE__, __FUNCTION__);
		memcpy(pByteCode, compiled_code->GetBufferPointer(), compiled_code->GetBufferSize());

		*pByteCodeSize = (uint32_t)compiled_code->GetBufferSize();
		*ppByteCode = pByteCode;
		SAFE_RELEASE(compiled_code);
	}
}

void addShaderBinary(Renderer* pRenderer, const BinaryShaderDesc* pDesc, Shader** ppShaderProgram)
{
	ASSERT(pRenderer);
	ASSERT(pDesc && pDesc->mStages);
	ASSERT(ppShaderProgram);

	Shader* pShaderProgram = (Shader*)conf_calloc(1, sizeof(*pShaderProgram));
	ASSERT(pShaderProgram);
	pShaderProgram->mStages = pDesc->mStages;

	uint32_t                 reflectionCount = 0;
	eastl::vector<ID3DBlob*> blobs(SHADER_STAGE_COUNT);
	eastl::vector<LPCWSTR>   entriesNames(SHADER_STAGE_COUNT);

	for (uint32_t i = 0; i < SHADER_STAGE_COUNT; ++i)
	{
		ShaderStage                  stage_mask = (ShaderStage)(1 << i);
		const BinaryShaderStageDesc* pStage = NULL;
		if (stage_mask == (pShaderProgram->mStages & stage_mask))
		{
			switch (stage_mask)
			{
				case SHADER_STAGE_VERT: { pStage = &pDesc->mVert;
				}
				break;
				case SHADER_STAGE_HULL: { pStage = &pDesc->mHull;
				}
				break;
				case SHADER_STAGE_DOMN: { pStage = &pDesc->mDomain;
				}
				break;
				case SHADER_STAGE_GEOM: { pStage = &pDesc->mGeom;
				}
				break;
				case SHADER_STAGE_FRAG: { pStage = &pDesc->mFrag;
				}
				break;
				case SHADER_STAGE_COMP: { pStage = &pDesc->mComp;
				}
				break;
#ifdef ENABLE_RAYTRACING
				case SHADER_STAGE_RAYTRACING: { pStage = &pDesc->mComp;
				}
				break;
#endif
			}

			D3DCreateBlob(pStage->mByteCodeSize, &blobs[reflectionCount]);
			memcpy(blobs[reflectionCount]->GetBufferPointer(), pStage->pByteCode, pStage->mByteCodeSize);

			d3d12_createShaderReflection(
				(uint8_t*)(blobs[reflectionCount]->GetBufferPointer()), (uint32_t)blobs[reflectionCount]->GetBufferSize(), stage_mask,
				&pShaderProgram->mReflection.mStageReflections[reflectionCount]);

			WCHAR* entryPointName = (WCHAR*)conf_calloc(strlen(pStage->pEntryPoint) + 1, sizeof(WCHAR));
			mbstowcs((WCHAR*)entryPointName, pStage->pEntryPoint, strlen(pStage->pEntryPoint));
			entriesNames[reflectionCount] = entryPointName;

			reflectionCount++;
		}
	}

	pShaderProgram->pShaderBlobs = (ID3DBlob**)conf_calloc(reflectionCount, sizeof(ID3DBlob*));
	for (uint32_t i = 0; i < reflectionCount; ++i)
	{
		blobs[i]->QueryInterface(__uuidof(ID3DBlob), (void**)&pShaderProgram->pShaderBlobs[i]);
		blobs[i]->Release();
	}

	pShaderProgram->pEntryNames = (LPCWSTR*)conf_calloc(reflectionCount, sizeof(LPCWSTR));
	memcpy(pShaderProgram->pEntryNames, entriesNames.data(), reflectionCount * sizeof(LPCWSTR));

	createPipelineReflection(pShaderProgram->mReflection.mStageReflections, reflectionCount, &pShaderProgram->mReflection);

	*ppShaderProgram = pShaderProgram;
}

void removeShader(Renderer* pRenderer, Shader* pShaderProgram)
{
	UNREF_PARAM(pRenderer);

	//remove given shader
	for (uint32_t i = 0; i < pShaderProgram->mReflection.mStageReflectionCount; ++i)
	{
		SAFE_RELEASE(pShaderProgram->pShaderBlobs[i]);
		SAFE_FREE((void*)pShaderProgram->pEntryNames[i]);
	}
	destroyPipelineReflection(&pShaderProgram->mReflection);

	SAFE_FREE(pShaderProgram->pShaderBlobs);
	SAFE_FREE(pShaderProgram->pEntryNames);
	SAFE_FREE(pShaderProgram);
}
/************************************************************************/
// Root Signature Functions
/************************************************************************/
void addRootSignature(Renderer* pRenderer, const RootSignatureDesc* pRootSignatureDesc, RootSignature** ppRootSignature)
{
	ASSERT(pRenderer->pActiveGpuSettings->mMaxRootSignatureDWORDS > 0);

	RootSignature* pRootSignature = (RootSignature*)conf_calloc(1, sizeof(*pRootSignature));
	ASSERT(pRootSignature);

	pRootSignature->pDescriptorNameToIndexMap = conf_new(DescriptorIndexMap);
	ASSERT(pRootSignature->pDescriptorNameToIndexMap);

	eastl::vector<UpdateFrequencyLayoutInfo>               layouts(DESCRIPTOR_UPDATE_FREQ_COUNT);
	eastl::vector<ShaderResource>                          shaderResources;
	eastl::vector<uint32_t>                                constantSizes;
	eastl::vector<eastl::pair<DescriptorInfo*, Sampler*> > staticSamplers;
	ShaderStage                                            shaderStages = SHADER_STAGE_NONE;
	bool                                                   useInputLayout = false;
	eastl::string_hash_map<Sampler*>                       staticSamplerMap;

	for (uint32_t i = 0; i < pRootSignatureDesc->mStaticSamplerCount; ++i)
	{
		ASSERT(pRootSignatureDesc->ppStaticSamplers[i]);
		staticSamplerMap.insert(pRootSignatureDesc->ppStaticSamplerNames[i], pRootSignatureDesc->ppStaticSamplers[i]);
	}

	//pRootSignature->pDescriptorNameToIndexMap;
	conf_placement_new<eastl::unordered_map<uint32_t, uint32_t> >(&pRootSignature->pDescriptorNameToIndexMap);

	// Collect all unique shader resources in the given shaders
	// Resources are parsed by name (two resources named "XYZ" in two shaders will be considered the same resource)
	for (uint32_t sh = 0; sh < pRootSignatureDesc->mShaderCount; ++sh)
	{
		PipelineReflection const* pReflection = &pRootSignatureDesc->ppShaders[sh]->mReflection;

		// Keep track of the used pipeline stages
		shaderStages |= pReflection->mShaderStages;

		if (pReflection->mShaderStages & SHADER_STAGE_COMP)
			pRootSignature->mPipelineType = PIPELINE_TYPE_COMPUTE;
#ifdef ENABLE_RAYTRACING
		// All raytracing shader bindings use the SetComputeXXX methods to bind descriptors
		else if (pReflection->mShaderStages == SHADER_STAGE_RAYTRACING)
			pRootSignature->mPipelineType = PIPELINE_TYPE_COMPUTE;
#endif
		else
			pRootSignature->mPipelineType = PIPELINE_TYPE_GRAPHICS;

		if (pReflection->mShaderStages & SHADER_STAGE_VERT)
		{
			if (pReflection->mStageReflections[pReflection->mVertexStageIndex].mVertexInputsCount)
			{
				useInputLayout = true;
			}
		}
		for (uint32_t i = 0; i < pReflection->mShaderResourceCount; ++i)
		{
			ShaderResource const* pRes = &pReflection->pShaderResources[i];
			uint32_t              setIndex = pRes->set;

			// If the size of the resource is zero, assume its a bindless resource
			// All bindless resources will go in the static descriptor table
			if (pRes->size == 0)
				setIndex = 0;

			// Find all unique resources
			decltype(pRootSignature->pDescriptorNameToIndexMap->mMap)::iterator it =
				pRootSignature->pDescriptorNameToIndexMap->mMap.find(pRes->name);
			if (it == pRootSignature->pDescriptorNameToIndexMap->mMap.end())
			{
				pRootSignature->pDescriptorNameToIndexMap->mMap.insert(pRes->name, (uint32_t)shaderResources.size());
				shaderResources.push_back(*pRes);

				uint32_t constantSize = 0;

				if (pRes->type == DESCRIPTOR_TYPE_UNIFORM_BUFFER)
				{
					for (uint32_t v = 0; v < pReflection->mVariableCount; ++v)
					{
						if (pReflection->pVariables[v].parent_index == i)
							constantSize += pReflection->pVariables[v].size;
					}
				}

				//shaderStages |= pRes->used_stages;
				constantSizes.push_back(constantSize);
			}
			// If the resource was already collected, just update the shader stage mask in case it is used in a different
			// shader stage in this case
			else
			{
				if (shaderResources[it->second].reg != pRes->reg)
				{
					LOGF( LogLevel::eERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching register. All shader resources "
						"shared by multiple shaders specified in addRootSignature "
						"have the same register and space",
						pRes->name);
					return;
				}
				if (shaderResources[it->second].set != pRes->set)
				{
					LOGF( LogLevel::eERROR,
						"\nFailed to create root signature\n"
						"Shared shader resource %s has mismatching space. All shader resources "
						"shared by multiple shaders specified in addRootSignature "
						"have the same register and space",
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
		DescriptorInfo* pDesc = &pRootSignature->pDescriptors[i];
		ShaderResource* pRes = &shaderResources[i];
		uint32_t        setIndex = pRes->set;
		if (pRes->size == 0 || setIndex >= DESCRIPTOR_UPDATE_FREQ_COUNT)
			setIndex = 0;

		DescriptorUpdateFrequency updateFreq = (DescriptorUpdateFrequency)setIndex;

		pDesc->mDesc.reg = pRes->reg;
		pDesc->mDesc.set = pRes->set;
		pDesc->mDesc.size = pRes->size;
		pDesc->mDesc.type = pRes->type;
		pDesc->mDesc.used_stages = pRes->used_stages;
		pDesc->mUpdateFrquency = updateFreq;
		pDesc->mDesc.dim = pRes->dim;

		pDesc->mDesc.name_size = pRes->name_size;
		pDesc->mDesc.name = (const char*)conf_calloc(pDesc->mDesc.name_size + 1, sizeof(char));
		memcpy((char*)pDesc->mDesc.name, pRes->name, pRes->name_size);

		if (pDesc->mDesc.size == 0 && pDesc->mDesc.type == DESCRIPTOR_TYPE_TEXTURE)
		{
			pDesc->mDesc.size = pRootSignatureDesc->mMaxBindlessTextures;
		}

		// Find the D3D12 type of the descriptors
		if (pDesc->mDesc.type == DESCRIPTOR_TYPE_SAMPLER)
		{
			// If the sampler is a static sampler, no need to put it in the descriptor table
			const decltype(staticSamplerMap)::iterator pNode = staticSamplerMap.find(pDesc->mDesc.name);

			if (pNode != staticSamplerMap.end())
			{
				LOGF(LogLevel::eINFO, "Descriptor (%s) : User specified Static Sampler", pDesc->mDesc.name);
				// Set the index to invalid value so we can use this later for error checking if user tries to update a static sampler
				pDesc->mIndexInParent = ~0u;
				staticSamplers.push_back({ pDesc, pNode->second });
			}
			else
			{
				// In D3D12, sampler descriptors cannot be placed in a table containing view descriptors
				layouts[setIndex].mSamplerTable.emplace_back(pDesc);
			}
		}
		// No support for arrays of constant buffers to be used as root descriptors as this might bloat the root signature size
		else if (pDesc->mDesc.type == DESCRIPTOR_TYPE_UNIFORM_BUFFER && pDesc->mDesc.size == 1)
		{
			// D3D12 has no special syntax to declare root constants like Vulkan
			// So we assume that all constant buffers with the word "rootconstant" (case insensitive) are root constants
			eastl::string name = pRes->name;
			name.make_lower();
			if (name.find("rootconstant", 0) != eastl::string::npos ||
				pDesc->mDesc.type == DESCRIPTOR_TYPE_ROOT_CONSTANT)
			{
				// Make the root param a 32 bit constant if the user explicitly specifies it in the shader
				pDesc->mDxType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
				pDesc->mDesc.type = DESCRIPTOR_TYPE_ROOT_CONSTANT;
				layouts[setIndex].mRootConstants.emplace_back(pDesc);

				pDesc->mDesc.size = constantSizes[i] / sizeof(uint32_t);
			}
			// If a user specified a uniform buffer to be used directly in the root signature change its type to D3D12_ROOT_PARAMETER_TYPE_CBV
			// Also log a message for debugging purpose
			else if (name.find("rootcbv", 0) != eastl::string::npos)
			{
				layouts[setIndex].mRootDescriptorParams.emplace_back(pDesc);
				pDesc->mDxType = D3D12_ROOT_PARAMETER_TYPE_CBV;

				LOGF(LogLevel::eINFO, "Descriptor (%s) : User specified D3D12_ROOT_PARAMETER_TYPE_CBV", pDesc->mDesc.name);
			}
			else
			{
				layouts[setIndex].mCbvSrvUavTable.emplace_back(pDesc);
			}
		}
		else
		{
			layouts[setIndex].mCbvSrvUavTable.emplace_back(pDesc);
		}

		layouts[setIndex].mDescriptorIndexMap[pDesc] = i;
	}

	// We should never reach inside this if statement. If we do, something got messed up
	if (pRenderer->pActiveGpuSettings->mMaxRootSignatureDWORDS < calculate_root_signature_size(layouts.data(), (uint32_t)layouts.size()))
	{
		LOGF(LogLevel::eWARNING, "Root Signature size greater than the specified max size");
		ASSERT(false);
	}

	// D3D12 currently has two versions of root signatures (1_0, 1_1)
	// So we fill the structs of both versions and in the end use the structs compatible with the supported version
	eastl::vector<eastl::vector<D3D12_DESCRIPTOR_RANGE1> > cbvSrvUavRange((uint32_t)layouts.size());
	eastl::vector<eastl::vector<D3D12_DESCRIPTOR_RANGE1> > samplerRange((uint32_t)layouts.size());
	eastl::vector<D3D12_ROOT_PARAMETER1>                   rootParams;

	eastl::vector<eastl::vector<D3D12_DESCRIPTOR_RANGE> > cbvSrvUavRange_1_0((uint32_t)layouts.size());
	eastl::vector<eastl::vector<D3D12_DESCRIPTOR_RANGE> > samplerRange_1_0((uint32_t)layouts.size());
	eastl::vector<D3D12_ROOT_PARAMETER>                   rootParams_1_0;

	eastl::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplerDescs(staticSamplers.size());
	for (uint32_t i = 0; i < (uint32_t)staticSamplers.size(); ++i)
	{
		D3D12_SAMPLER_DESC& desc = staticSamplers[i].second->mDxDesc;
		staticSamplerDescs[i].Filter = desc.Filter;
		staticSamplerDescs[i].AddressU = desc.AddressU;
		staticSamplerDescs[i].AddressV = desc.AddressV;
		staticSamplerDescs[i].AddressW = desc.AddressW;
		staticSamplerDescs[i].MipLODBias = desc.MipLODBias;
		staticSamplerDescs[i].MaxAnisotropy = desc.MaxAnisotropy;
		staticSamplerDescs[i].ComparisonFunc = desc.ComparisonFunc;
		staticSamplerDescs[i].MinLOD = desc.MinLOD;
		staticSamplerDescs[i].MaxLOD = desc.MaxLOD;
		staticSamplerDescs[i].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		staticSamplerDescs[i].RegisterSpace = staticSamplers[i].first->mDesc.set;
		staticSamplerDescs[i].ShaderRegister = staticSamplers[i].first->mDesc.reg;
		staticSamplerDescs[i].ShaderVisibility = util_to_dx_shader_visibility(staticSamplers[i].first->mDesc.used_stages);
	}

	for (uint32_t i = 0; i < (uint32_t)layouts.size(); ++i)
	{
		cbvSrvUavRange[i].resize(layouts[i].mCbvSrvUavTable.size());
		cbvSrvUavRange_1_0[i].resize(layouts[i].mCbvSrvUavTable.size());

		samplerRange[i].resize(layouts[i].mSamplerTable.size());
		samplerRange_1_0[i].resize(layouts[i].mSamplerTable.size());
	}

	pRootSignature->mDescriptorCount = (uint32_t)shaderResources.size();

	for (uint32_t i = 0; i < (uint32_t)layouts.size(); ++i)
	{
		pRootSignature->mDxRootConstantCount += (uint32_t)layouts[i].mRootConstants.size();
		pRootSignature->mDxRootDescriptorCounts[i] += (uint32_t)layouts[i].mRootDescriptorParams.size();

		if (pRootSignature->mDxRootDescriptorCounts[i])
			pRootSignature->pDxRootDescriptorRootIndices[i] =
			(uint32_t*)conf_calloc(pRootSignature->mDxRootDescriptorCounts[i], sizeof(*pRootSignature->pDxRootDescriptorRootIndices[i]));
	}
	if (pRootSignature->mDxRootConstantCount)
		pRootSignature->pDxRootConstantRootIndices =
			(uint32_t*)conf_calloc(pRootSignature->mDxRootConstantCount, sizeof(*pRootSignature->pDxRootConstantRootIndices));

	// Start collecting root parameters
	// Start with root descriptors since they will be the most frequently updated descriptors
	// This also makes sure that if we spill, the root descriptors in the front of the root signature will most likely still remain in the root
	// Collect all root descriptors
	// Put most frequently changed params first
	for (uint32_t i = (uint32_t)layouts.size(); i-- > 0U;)
	{
		UpdateFrequencyLayoutInfo& layout = layouts[i];
		if (layout.mRootDescriptorParams.size())
		{
			uint32_t rootDescriptorIndex = 0;

			for (uint32_t descIndex = 0; descIndex < (uint32_t)layout.mRootDescriptorParams.size(); ++descIndex)
			{
				DescriptorInfo* pDesc = layout.mRootDescriptorParams[descIndex];
				pDesc->mIndexInParent = rootDescriptorIndex;
				pRootSignature->pDxRootDescriptorRootIndices[i][pDesc->mIndexInParent] = (uint32_t)rootParams.size();

				D3D12_ROOT_PARAMETER1 rootParam;
				D3D12_ROOT_PARAMETER  rootParam_1_0;
				create_root_descriptor(pDesc, &rootParam);
				create_root_descriptor_1_0(pDesc, &rootParam_1_0);

				rootParams.push_back(rootParam);
				rootParams_1_0.push_back(rootParam_1_0);

				++rootDescriptorIndex;
			}
		}
	}

	uint32_t rootConstantIndex = 0;

	// Collect all root constants
	for (uint32_t setIndex = 0; setIndex < (uint32_t)layouts.size(); ++setIndex)
	{
		UpdateFrequencyLayoutInfo& layout = layouts[setIndex];

		if (!layout.mRootConstants.size())
			continue;

		for (uint32_t i = 0; i < (uint32_t)layouts[setIndex].mRootConstants.size(); ++i)
		{
			DescriptorInfo* pDesc = layout.mRootConstants[i];
			pDesc->mIndexInParent = rootConstantIndex;
			pRootSignature->pDxRootConstantRootIndices[pDesc->mIndexInParent] = (uint32_t)rootParams.size();

			D3D12_ROOT_PARAMETER1 rootParam;
			D3D12_ROOT_PARAMETER  rootParam_1_0;
			create_root_constant(pDesc, &rootParam);
			create_root_constant_1_0(pDesc, &rootParam_1_0);

			rootParams.push_back(rootParam);
			rootParams_1_0.push_back(rootParam_1_0);

			if (pDesc->mDesc.size > gMaxRootConstantsPerRootParam)
			{
				//64 DWORDS for NVIDIA, 16 for AMD but 3 are used by driver so we get 13 SGPR
				//DirectX12
				//Root descriptors - 2
				//Root constants - Number of 32 bit constants
				//Descriptor tables - 1
				//Static samplers - 0
				LOGF(
					LogLevel::eINFO,
					"Root constant (%s) has (%u) 32 bit values. It is recommended to have root constant number less or equal than 13",
					pDesc->mDesc.name, pDesc->mDesc.size);
			}

			++rootConstantIndex;
		}
	}

	// Collect descriptor table parameters
	// Put most frequently changed descriptor tables in the front of the root signature
	for (uint32_t i = (uint32_t)layouts.size(); i-- > 0U;)
	{
		UpdateFrequencyLayoutInfo& layout = layouts[i];

		// Fill the descriptor table layout for the view descriptor table of this update frequency
		if (layout.mCbvSrvUavTable.size())
		{
			// sort table by type (CBV/SRV/UAV) by register by space
			eastl::stable_sort(
				layout.mCbvSrvUavTable.begin(), layout.mCbvSrvUavTable.end(),
				[](DescriptorInfo* const lhs, DescriptorInfo* const rhs) { return lhs->mDesc.reg > rhs->mDesc.reg; });
			eastl::stable_sort(
				layout.mCbvSrvUavTable.begin(), layout.mCbvSrvUavTable.end(),
				[](DescriptorInfo* const& lhs, DescriptorInfo* const& rhs) { return lhs->mDesc.set > rhs->mDesc.set; });
			eastl::stable_sort(
				layout.mCbvSrvUavTable.begin(), layout.mCbvSrvUavTable.end(),
				[](DescriptorInfo* const& lhs, DescriptorInfo* const& rhs) { return lhs->mDesc.type > rhs->mDesc.type; });

			D3D12_ROOT_PARAMETER1 rootParam;
			create_descriptor_table(
				(uint32_t)layout.mCbvSrvUavTable.size(), layout.mCbvSrvUavTable.data(), cbvSrvUavRange[i].data(), &rootParam);

			D3D12_ROOT_PARAMETER rootParam_1_0;
			create_descriptor_table_1_0(
				(uint32_t)layout.mCbvSrvUavTable.size(), layout.mCbvSrvUavTable.data(), cbvSrvUavRange_1_0[i].data(), &rootParam_1_0);

			// Store some of the binding info which will be required later when binding the descriptor table
			// We need the root index when calling SetRootDescriptorTable
			pRootSignature->mDxViewDescriptorTableRootIndices[i] = (uint32_t)rootParams.size();
			pRootSignature->mDxViewDescriptorCounts[i] = (uint32_t)layout.mCbvSrvUavTable.size();
			pRootSignature->pDxViewDescriptorIndices[i] = (uint32_t*)conf_calloc(layout.mCbvSrvUavTable.size(), sizeof(uint32_t));

			for (uint32_t descIndex = 0; descIndex < (uint32_t)layout.mCbvSrvUavTable.size(); ++descIndex)
			{
				DescriptorInfo* pDesc = layout.mCbvSrvUavTable[descIndex];
				pDesc->mIndexInParent = descIndex;

				// Store the d3d12 related info in the descriptor to avoid constantly calling the util_to_dx mapping functions
				pDesc->mDxType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				pDesc->mHandleIndex = pRootSignature->mDxCumulativeViewDescriptorCounts[i];

				// Store the cumulative descriptor count so we can just fetch this value later when allocating descriptor handles
				// This avoids unnecessary loops in the future to find the unfolded number of descriptors (includes shader resource arrays) in the descriptor table
				pRootSignature->mDxCumulativeViewDescriptorCounts[i] += pDesc->mDesc.size;
				pRootSignature->pDxViewDescriptorIndices[i][descIndex] = layout.mDescriptorIndexMap[pDesc];
			}

			rootParams.push_back(rootParam);
			rootParams_1_0.push_back(rootParam_1_0);
		}

		// Fill the descriptor table layout for the sampler descriptor table of this update frequency
		if (layout.mSamplerTable.size())
		{
			D3D12_ROOT_PARAMETER1 rootParam;
			create_descriptor_table((uint32_t)layout.mSamplerTable.size(), layout.mSamplerTable.data(), samplerRange[i].data(), &rootParam);

			D3D12_ROOT_PARAMETER rootParam_1_0;
			create_descriptor_table_1_0(
				(uint32_t)layout.mSamplerTable.size(), layout.mSamplerTable.data(), samplerRange_1_0[i].data(), &rootParam_1_0);

			// Store some of the binding info which will be required later when binding the descriptor table
			// We need the root index when calling SetRootDescriptorTable
			pRootSignature->mDxSamplerDescriptorTableRootIndices[i] = (uint32_t)rootParams.size();
			pRootSignature->mDxSamplerDescriptorCounts[i] = (uint32_t)layout.mSamplerTable.size();
			pRootSignature->pDxSamplerDescriptorIndices[i] = (uint32_t*)conf_calloc(layout.mSamplerTable.size(), sizeof(uint32_t));
			//table.pDescriptorIndices = (uint32_t*)conf_calloc(table.mDescriptorCount, sizeof(uint32_t));

			for (uint32_t descIndex = 0; descIndex < (uint32_t)layout.mSamplerTable.size(); ++descIndex)
			{
				DescriptorInfo* pDesc = layout.mSamplerTable[descIndex];
				pDesc->mIndexInParent = descIndex;

				// Store the d3d12 related info in the descriptor to avoid constantly calling the util_to_dx mapping functions
				pDesc->mDxType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
				pDesc->mHandleIndex = pRootSignature->mDxCumulativeSamplerDescriptorCounts[i];

				// Store the cumulative descriptor count so we can just fetch this value later when allocating descriptor handles
				// This avoids unnecessary loops in the future to find the unfolded number of descriptors (includes shader resource arrays) in the descriptor table
				pRootSignature->mDxCumulativeSamplerDescriptorCounts[i] += pDesc->mDesc.size;
				pRootSignature->pDxSamplerDescriptorIndices[i][descIndex] = layout.mDescriptorIndexMap[pDesc];
			}

			rootParams.push_back(rootParam);
			rootParams_1_0.push_back(rootParam_1_0);
		}
	}

	DECLARE_ZERO(D3D12_FEATURE_DATA_ROOT_SIGNATURE, feature_data);
	feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
	HRESULT hres = pRenderer->pDxDevice->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data));

	if (FAILED(hres))
		feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

	// Specify the deny flags to avoid unnecessary shader stages being notified about descriptor modifications
	D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	if (useInputLayout)
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	if (!(shaderStages & SHADER_STAGE_VERT))
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS;
	if (!(shaderStages & SHADER_STAGE_HULL))
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS;
	if (!(shaderStages & SHADER_STAGE_DOMN))
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS;
	if (!(shaderStages & SHADER_STAGE_GEOM))
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
	if (!(shaderStages & SHADER_STAGE_FRAG))
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
#ifdef ENABLE_RAYTRACING
	if (pRootSignatureDesc->mFlags & ROOT_SIGNATURE_FLAG_LOCAL_BIT)
		rootSignatureFlags |= D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
#endif

	ID3DBlob* error_msgs = NULL;
#ifdef _DURANGO
	DECLARE_ZERO(D3D12_ROOT_SIGNATURE_DESC, desc);
	desc.NumParameters = (uint32_t)rootParams_1_0.size();
	desc.pParameters = rootParams_1_0.data();
	desc.NumStaticSamplers = (UINT)staticSamplerDescs.size();
	desc.pStaticSamplers = staticSamplerDescs.data();
	desc.Flags = rootSignatureFlags;
	hres = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &pRootSignature->pDxSerializedRootSignatureString, &error_msgs);
#else
	DECLARE_ZERO(D3D12_VERSIONED_ROOT_SIGNATURE_DESC, desc);

	if (D3D_ROOT_SIGNATURE_VERSION_1_1 == feature_data.HighestVersion)
	{
		desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		desc.Desc_1_1.NumParameters = (uint32_t)rootParams.size();
		desc.Desc_1_1.pParameters = rootParams.data();
		desc.Desc_1_1.NumStaticSamplers = (UINT)staticSamplerDescs.size();
		desc.Desc_1_1.pStaticSamplers = staticSamplerDescs.data();
		desc.Desc_1_1.Flags = rootSignatureFlags;
	}
	else
	{
		desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
		desc.Desc_1_0.NumParameters = (uint32_t)rootParams_1_0.size();
		desc.Desc_1_0.pParameters = rootParams_1_0.data();
		desc.Desc_1_0.NumStaticSamplers = (UINT)staticSamplerDescs.size();
		desc.Desc_1_0.pStaticSamplers = staticSamplerDescs.data();
		desc.Desc_1_0.Flags = rootSignatureFlags;
	}

	hres = D3D12SerializeVersionedRootSignature(&desc, &pRootSignature->pDxSerializedRootSignatureString, &error_msgs);
#endif

	if (!SUCCEEDED(hres))
	{
		char* pMsg = (char*)conf_calloc(error_msgs->GetBufferSize(), sizeof(char));
		memcpy(pMsg, error_msgs->GetBufferPointer(), error_msgs->GetBufferSize());
		LOGF(LogLevel::eERROR, "Failed to serialize root signature with error (%s)", pMsg);
		conf_free(pMsg);
	}

	// If running Linked Mode (SLI) create root signature for all nodes
	// #NOTE : In non SLI mode, mNodeCount will be 0 which sets nodeMask to default value
	hres = pRenderer->pDxDevice->CreateRootSignature(
		util_calculate_shared_node_mask(pRenderer), pRootSignature->pDxSerializedRootSignatureString->GetBufferPointer(),
		pRootSignature->pDxSerializedRootSignatureString->GetBufferSize(), IID_ARGS(&pRootSignature->pDxRootSignature));
	ASSERT(SUCCEEDED(hres));

	SAFE_RELEASE(error_msgs);

	*ppRootSignature = pRootSignature;
}

void removeRootSignature(Renderer* pRenderer, RootSignature* pRootSignature)
{
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		SAFE_FREE(pRootSignature->pDxViewDescriptorIndices[i]);
		SAFE_FREE(pRootSignature->pDxSamplerDescriptorIndices[i]);
		SAFE_FREE(pRootSignature->pDxRootDescriptorRootIndices[i]);
	}

	for (uint32_t i = 0; i < pRootSignature->mDescriptorCount; ++i)
	{
		SAFE_FREE((void*)pRootSignature->pDescriptors[i].mDesc.name);
	}

	conf_delete(pRootSignature->pDescriptorNameToIndexMap);

	SAFE_FREE(pRootSignature->pDescriptors);
	SAFE_FREE(pRootSignature->pDxRootConstantRootIndices);

	SAFE_RELEASE(pRootSignature->pDxRootSignature);
	SAFE_RELEASE(pRootSignature->pDxSerializedRootSignatureString);

	SAFE_FREE(pRootSignature);
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
	const uint32_t cbvSrvUavDescCount = pRootSignature->mDxCumulativeViewDescriptorCounts[updateFreq];
	const uint32_t samplerDescCount = pRootSignature->mDxCumulativeSamplerDescriptorCounts[updateFreq];

	pDescriptorSet->pRootSignature = pRootSignature;
	pDescriptorSet->mUpdateFrequency = updateFreq;
	pDescriptorSet->mNodeIndex = nodeIndex;
	pDescriptorSet->mMaxSets = pDesc->mMaxSets;
	pDescriptorSet->mCbvSrvUavRootIndex = pRootSignature->mDxViewDescriptorTableRootIndices[updateFreq];
	pDescriptorSet->mSamplerRootIndex = pRootSignature->mDxSamplerDescriptorTableRootIndices[updateFreq];
	pDescriptorSet->mRootAddressCount = pRootSignature->mDxRootDescriptorCounts[updateFreq];

	if (pDescriptorSet->mRootAddressCount)
	{
		pDescriptorSet->pRootAddresses = (D3D12_GPU_VIRTUAL_ADDRESS**)conf_calloc(pDescriptorSet->mMaxSets, sizeof(D3D12_GPU_VIRTUAL_ADDRESS*));
		for (uint32_t i = 0; i < pDescriptorSet->mMaxSets; ++i)
			pDescriptorSet->pRootAddresses[i] = (D3D12_GPU_VIRTUAL_ADDRESS*)conf_calloc(pDescriptorSet->mRootAddressCount, sizeof(D3D12_GPU_VIRTUAL_ADDRESS));
	}

	if (cbvSrvUavDescCount || samplerDescCount)
	{
		if (cbvSrvUavDescCount)
		{
			DescriptorHeap* pHeap = pRenderer->pCbvSrvUavHeaps[nodeIndex];
			pDescriptorSet->pCbvSrvUavHandles = (uint64_t*)conf_calloc(pDesc->mMaxSets, sizeof(uint64_t));
			DescriptorHeap::DescriptorHandle startHandle = consume_descriptor_handles(pHeap, cbvSrvUavDescCount * pDesc->mMaxSets);
			for (uint32_t i = 0; i < pDesc->mMaxSets; ++i)
				pDescriptorSet->pCbvSrvUavHandles[i] =
				(startHandle.mGpu.ptr + (i * cbvSrvUavDescCount * pHeap->mDescriptorSize)) -
				pHeap->mStartHandle.mGpu.ptr;

			for (uint32_t i = 0; i < pRootSignature->mDxViewDescriptorCounts[updateFreq]; ++i)
			{
				const DescriptorInfo* pDescInfo = &pRootSignature->pDescriptors[pRootSignature->pDxViewDescriptorIndices[updateFreq][i]];
				DescriptorType        type = pDescInfo->mDesc.type;
				D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = { D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN };
				switch (type)
				{
				case DESCRIPTOR_TYPE_TEXTURE:        srcHandle = pRenderer->mNullTextureSRV[pDescInfo->mDesc.dim]; break;
				case DESCRIPTOR_TYPE_BUFFER:         srcHandle = pRenderer->mNullBufferSRV; break;
				case DESCRIPTOR_TYPE_RW_TEXTURE:     srcHandle = pRenderer->mNullTextureUAV[pDescInfo->mDesc.dim]; break;
				case DESCRIPTOR_TYPE_RW_BUFFER:      srcHandle = pRenderer->mNullBufferUAV; break;
				case DESCRIPTOR_TYPE_UNIFORM_BUFFER: srcHandle = pRenderer->mNullBufferCBV; break;
				default: break;
				}

#ifdef ENABLE_RAYTRACING
				if (pDescInfo->mDesc.type != DESCRIPTOR_TYPE_RAY_TRACING)
#endif
				{
					ASSERT(srcHandle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

					for (uint32_t s = 0; s < pDesc->mMaxSets; ++s)
						for (uint32_t j = 0; j < pDescInfo->mDesc.size; ++j)
							copy_descriptor_handle(pHeap,
								srcHandle, pDescriptorSet->pCbvSrvUavHandles[s], pDescInfo->mHandleIndex + j);
				}
			}
		}
		if (samplerDescCount)
		{
			DescriptorHeap* pHeap = pRenderer->pSamplerHeaps[nodeIndex];
			pDescriptorSet->pSamplerHandles = (uint64_t*)conf_calloc(pDesc->mMaxSets, sizeof(uint64_t));
			DescriptorHeap::DescriptorHandle startHandle = consume_descriptor_handles(pHeap, samplerDescCount * pDesc->mMaxSets);
			for (uint32_t i = 0; i < pDesc->mMaxSets; ++i)
			{
				pDescriptorSet->pSamplerHandles[i] =
					(startHandle.mGpu.ptr + (i * samplerDescCount * pHeap->mDescriptorSize)) -
					pHeap->mStartHandle.mGpu.ptr;

				for (uint32_t j = 0; j < samplerDescCount; ++j)
					copy_descriptor_handle(pHeap, pRenderer->mNullSampler, pDescriptorSet->pSamplerHandles[i], j);
			}
		}
	}

	*ppDescriptorSet = pDescriptorSet;
}

void removeDescriptorSet(Renderer* pRenderer, DescriptorSet* pDescriptorSet)
{
	ASSERT(pRenderer);
	ASSERT(pDescriptorSet);

	if (pDescriptorSet->mRootAddressCount)
		for (uint32_t i = 0; i < pDescriptorSet->mMaxSets; ++i)
			SAFE_FREE(pDescriptorSet->pRootAddresses[i]);

	SAFE_FREE(pDescriptorSet->pRootAddresses);
	SAFE_FREE(pDescriptorSet->pCbvSrvUavHandles);
	SAFE_FREE(pDescriptorSet->pSamplerHandles);
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
	ASSERT(index < pDescriptorSet->mMaxSets);

	const RootSignature* pRootSignature = pDescriptorSet->pRootSignature;
	const DescriptorUpdateFrequency updateFreq = (DescriptorUpdateFrequency)pDescriptorSet->mUpdateFrequency;
	const uint32_t nodeIndex = pDescriptorSet->mNodeIndex;
	bool update = false;

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

		VALIDATE_DESCRIPTOR(pDesc->mUpdateFrquency == updateFreq,
			"Descriptor (%s) - Mismatching update frequency and register space", pDesc->mDesc.name);

		if (pDesc->mDxType == D3D12_ROOT_PARAMETER_TYPE_CBV)
		{
			VALIDATE_DESCRIPTOR(arrayCount == 1, "Descriptor (%s) : D3D12_ROOT_PARAMETER_TYPE_CBV does not support arrays", pDesc->mDesc.name);
			// We have this validation to stay consistent with Vulkan
			VALIDATE_DESCRIPTOR(pParam->pSizes, "Descriptor (%s) : Must provide pSizes for D3D12_ROOT_PARAMETER_TYPE_CBV", pDesc->mDesc.name);

			pDescriptorSet->pRootAddresses[index][pDesc->mIndexInParent] = pParam->ppBuffers[0]->mDxGpuAddress +
				(pParam->pOffsets ? (uint32_t)pParam->pOffsets[0] : 0);
		}
		else if (type == DESCRIPTOR_TYPE_SAMPLER)
		{
			// Index is invalid when descriptor is a static sampler
			VALIDATE_DESCRIPTOR(pDesc->mIndexInParent != -1,
				"Trying to update a static sampler (%s). All static samplers must be set in addRootSignature and cannot be updated later",
				pDesc->mDesc.name);

			VALIDATE_DESCRIPTOR(pParam->ppSamplers, "NULL Sampler (%s)", pDesc->mDesc.name);

			for (uint32_t arr = 0; arr < arrayCount; ++arr)
			{
				VALIDATE_DESCRIPTOR(pParam->ppSamplers[arr], "NULL Sampler (%s [%u] )", pDesc->mDesc.name, arr);

				copy_descriptor_handle(pRenderer->pSamplerHeaps[nodeIndex],
					pParam->ppSamplers[arr]->mDxSamplerHandle,
					pDescriptorSet->pSamplerHandles[index], pDesc->mHandleIndex + arr);
			}

			update = true;
		}
		else
		{
			switch (type)
			{
			case DESCRIPTOR_TYPE_TEXTURE:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL Texture (%s)", pDesc->mDesc.name);

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL Texture (%s [%u] )", pDesc->mDesc.name, arr);

					copy_descriptor_handle(pRenderer->pCbvSrvUavHeaps[nodeIndex],
						pParam->ppTextures[arr]->mDxSRVDescriptor,
						pDescriptorSet->pCbvSrvUavHandles[index], pDesc->mHandleIndex + arr);
				}
				break;
			}
			case DESCRIPTOR_TYPE_RW_TEXTURE:
			{
				VALIDATE_DESCRIPTOR(pParam->ppTextures, "NULL RW Texture (%s)", pDesc->mDesc.name);

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppTextures[arr], "NULL RW Texture (%s [%u] )", pDesc->mDesc.name, arr);

					copy_descriptor_handle(pRenderer->pCbvSrvUavHeaps[nodeIndex],
						pParam->ppTextures[arr]->pDxUAVDescriptors[pParam->mUAVMipSlice],
						pDescriptorSet->pCbvSrvUavHandles[index], pDesc->mHandleIndex + arr);
				}
				break;
			}
			case DESCRIPTOR_TYPE_BUFFER:
			case DESCRIPTOR_TYPE_BUFFER_RAW:
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Buffer (%s)", pDesc->mDesc.name);

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Buffer (%s [%u] )", pDesc->mDesc.name, arr);

					copy_descriptor_handle(pRenderer->pCbvSrvUavHeaps[nodeIndex],
						pParam->ppBuffers[arr]->mDxSrvHandle,
						pDescriptorSet->pCbvSrvUavHandles[index], pDesc->mHandleIndex + arr);
				}
				break;
			}
			case DESCRIPTOR_TYPE_RW_BUFFER:
			case DESCRIPTOR_TYPE_RW_BUFFER_RAW:
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL RW Buffer (%s)", pDesc->mDesc.name);

				for (uint32_t arr = 0; arr < arrayCount; ++arr)
				{
					VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL RW Buffer (%s [%u] )", pDesc->mDesc.name, arr);

					copy_descriptor_handle(pRenderer->pCbvSrvUavHeaps[nodeIndex],
						pParam->ppBuffers[arr]->mDxUavHandle,
						pDescriptorSet->pCbvSrvUavHandles[index], pDesc->mHandleIndex + arr);
				}
				break;
			}
			case DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			{
				VALIDATE_DESCRIPTOR(pParam->ppBuffers, "NULL Uniform Buffer (%s)", pDesc->mDesc.name);

				if (pParam->pOffsets)
				{
					VALIDATE_DESCRIPTOR(pParam->pSizes, "Descriptor (%s) - pSizes must be provided with pOffsets", pDesc->mDesc.name);

					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Uniform Buffer (%s [%u] )", pDesc->mDesc.name, arr);
						VALIDATE_DESCRIPTOR(pParam->pSizes[arr] > 0, "Descriptor (%s) - pSizes[%u] is zero", pDesc->mDesc.name, arr);
						VALIDATE_DESCRIPTOR(pParam->pSizes[arr] <= 65536, "Descriptor (%s) - pSizes[%u] is %ull which exceeds max size %u", pDesc->mDesc.name, arr,
							pParam->pSizes[arr], 65536U);

						D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
						cbvDesc.BufferLocation = pParam->ppBuffers[arr]->mDxGpuAddress + pParam->pOffsets[arr];
						cbvDesc.SizeInBytes = (UINT)pParam->pSizes[arr];
						pRenderer->pDxDevice->CreateConstantBufferView(&cbvDesc,
							{ pRenderer->pCbvSrvUavHeaps[nodeIndex]->mStartHandle.mCpu.ptr + pDescriptorSet->pCbvSrvUavHandles[index] });
					}
				}
				else
				{
					for (uint32_t arr = 0; arr < arrayCount; ++arr)
					{
						VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr], "NULL Uniform Buffer (%s [%u] )", pDesc->mDesc.name, arr);
						VALIDATE_DESCRIPTOR(pParam->ppBuffers[arr]->mDesc.mSize <= 65536, "Descriptor (%s) - pSizes[%u] is exceeds max size", pDesc->mDesc.name, arr);

						copy_descriptor_handle(pRenderer->pCbvSrvUavHeaps[nodeIndex],
							pParam->ppBuffers[arr]->mDxCbvHandle,
							pDescriptorSet->pCbvSrvUavHandles[index], pDesc->mHandleIndex + arr);
					}
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

					D3D12_CPU_DESCRIPTOR_HANDLE handle = { D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN };
					d3d12_fillRaytracingDescriptorHandle(pParam->ppAccelerationStructures[arr], &handle.ptr);

					VALIDATE_DESCRIPTOR(handle.ptr != D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN, "Invalid Acceleration Structure (%s [%u] )", pDesc->mDesc.name, arr);

					copy_descriptor_handle(pRenderer->pCbvSrvUavHeaps[nodeIndex],
						handle,
						pDescriptorSet->pCbvSrvUavHandles[index], pDesc->mHandleIndex + arr);
				}
				break;
			}
#endif
			default:
				break;
			}

			update = true;
		}
	}
}

bool reset_root_signature(Cmd* pCmd, const RootSignature* pRootSignature)
{
	// Set root signature if the current one differs from pRootSignature
	if (pCmd->pBoundRootSignature != pRootSignature)
	{
		pCmd->pBoundRootSignature = pRootSignature;
		if (pRootSignature->mPipelineType == PIPELINE_TYPE_GRAPHICS)
			pCmd->pDxCmdList->SetGraphicsRootSignature(pRootSignature->pDxRootSignature);
		else
			pCmd->pDxCmdList->SetComputeRootSignature(pRootSignature->pDxRootSignature);

		for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
		{
			pCmd->pBoundDescriptorSets[i] = NULL;
			pCmd->mBoundDescriptorSetIndices[i] = -1;
		}
	}

	return false;
}

void cmdBindDescriptorSet(Cmd* pCmd, uint32_t index, DescriptorSet* pDescriptorSet)
{
	ASSERT(pCmd);
	ASSERT(pDescriptorSet);
	ASSERT(index < pDescriptorSet->mMaxSets);

	const RootSignature* pRootSignature = pDescriptorSet->pRootSignature;
	const DescriptorUpdateFrequency updateFreq = (DescriptorUpdateFrequency)pDescriptorSet->mUpdateFrequency;

	// Set root signature if the current one differs from pRootSignature
	reset_root_signature(pCmd, pRootSignature);

	// Bind all required root descriptors
	for (uint32_t i = 0; i < pDescriptorSet->mRootAddressCount; ++i)
	{
		if (pRootSignature->mPipelineType == PIPELINE_TYPE_GRAPHICS)
			pCmd->pDxCmdList->SetGraphicsRootConstantBufferView(pRootSignature->pDxRootDescriptorRootIndices[updateFreq][i],
				pDescriptorSet->pRootAddresses[index][i]);
		else
			pCmd->pDxCmdList->SetComputeRootConstantBufferView(pRootSignature->pDxRootDescriptorRootIndices[updateFreq][i],
				pDescriptorSet->pRootAddresses[index][i]);
	}

	if (pCmd->mBoundDescriptorSetIndices[pDescriptorSet->mUpdateFrequency] != index || pCmd->pBoundDescriptorSets[pDescriptorSet->mUpdateFrequency] != pDescriptorSet)
	{
		pCmd->pBoundDescriptorSets[pDescriptorSet->mUpdateFrequency] = pDescriptorSet;
		pCmd->mBoundDescriptorSetIndices[pDescriptorSet->mUpdateFrequency] = index;

		// Bind the descriptor tables associated with this DescriptorSet
		if (pRootSignature->mPipelineType == PIPELINE_TYPE_GRAPHICS)
		{
			if (pDescriptorSet->pCbvSrvUavHandles)
				pCmd->pDxCmdList->SetGraphicsRootDescriptorTable(pDescriptorSet->mCbvSrvUavRootIndex,
					{ pCmd->pRenderer->pCbvSrvUavHeaps[pDescriptorSet->mNodeIndex]->mStartHandle.mGpu.ptr +
					pDescriptorSet->pCbvSrvUavHandles[index] });
			if (pDescriptorSet->pSamplerHandles)
				pCmd->pDxCmdList->SetGraphicsRootDescriptorTable(pDescriptorSet->mSamplerRootIndex,
					{ pCmd->pRenderer->pSamplerHeaps[pDescriptorSet->mNodeIndex]->mStartHandle.mGpu.ptr +
					pDescriptorSet->pSamplerHandles[index] });
		}
		else
		{
			if (pDescriptorSet->pCbvSrvUavHandles)
				pCmd->pDxCmdList->SetComputeRootDescriptorTable(pDescriptorSet->mCbvSrvUavRootIndex,
					{ pCmd->pRenderer->pCbvSrvUavHeaps[pDescriptorSet->mNodeIndex]->mStartHandle.mGpu.ptr +
					pDescriptorSet->pCbvSrvUavHandles[index] });
			if (pDescriptorSet->pSamplerHandles)
				pCmd->pDxCmdList->SetComputeRootDescriptorTable(pDescriptorSet->mSamplerRootIndex,
					{ pCmd->pRenderer->pSamplerHeaps[pDescriptorSet->mNodeIndex]->mStartHandle.mGpu.ptr +
					pDescriptorSet->pSamplerHandles[index] });
		}
	}
}

void cmdBindPushConstants(Cmd* pCmd, RootSignature* pRootSignature, const char* pName, const void* pConstants)
{
	ASSERT(pCmd);
	ASSERT(pConstants);
	ASSERT(pRootSignature);
	ASSERT(pName);
	
	// Set root signature if the current one differs from pRootSignature
	reset_root_signature(pCmd, pRootSignature);

	const DescriptorInfo* pDesc = get_descriptor(pRootSignature, pName);
	ASSERT(pDesc);
	ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == pDesc->mDesc.type);
	
	if (pRootSignature->mPipelineType == PIPELINE_TYPE_GRAPHICS)
		pCmd->pDxCmdList->SetGraphicsRoot32BitConstants(pRootSignature->pDxRootConstantRootIndices[pDesc->mIndexInParent], pDesc->mDesc.size, pConstants, 0);
	else
		pCmd->pDxCmdList->SetComputeRoot32BitConstants(pRootSignature->pDxRootConstantRootIndices[pDesc->mIndexInParent], pDesc->mDesc.size, pConstants, 0);
}

void cmdBindPushConstantsByIndex(Cmd* pCmd, RootSignature* pRootSignature, uint32_t paramIndex, const void* pConstants)
{
	ASSERT(pCmd);
	ASSERT(pConstants);
	ASSERT(pRootSignature);
	ASSERT(paramIndex >= 0 && paramIndex < pRootSignature->mDescriptorCount);
	
	// Set root signature if the current one differs from pRootSignature
	reset_root_signature(pCmd, pRootSignature);

	const DescriptorInfo* pDesc = pRootSignature->pDescriptors + paramIndex;
	ASSERT(pDesc);
	ASSERT(DESCRIPTOR_TYPE_ROOT_CONSTANT == pDesc->mDesc.type);

	if (pRootSignature->mPipelineType == PIPELINE_TYPE_GRAPHICS)
		pCmd->pDxCmdList->SetGraphicsRoot32BitConstants(pRootSignature->pDxRootConstantRootIndices[pDesc->mIndexInParent], pDesc->mDesc.size, pConstants, 0);
	else
		pCmd->pDxCmdList->SetComputeRoot32BitConstants(pRootSignature->pDxRootConstantRootIndices[pDesc->mIndexInParent], pDesc->mDesc.size, pConstants, 0);
}
/************************************************************************/
// Pipeline State Functions
/************************************************************************/
void addPipeline(Renderer* pRenderer, const GraphicsPipelineDesc* pDesc, Pipeline** ppPipeline)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(pDesc->pShaderProgram);
	ASSERT(pDesc->pRootSignature);

	//allocate new pipeline
	Pipeline* pPipeline = (Pipeline*)conf_calloc(1, sizeof(*pPipeline));
	ASSERT(pPipeline);

	const Shader*       pShaderProgram = pDesc->pShaderProgram;
	const VertexLayout* pVertexLayout = pDesc->pVertexLayout;

	//copy the given pipeline settings into new pipeline
	memcpy(&(pPipeline->mGraphics), pDesc, sizeof(*pDesc));
	pPipeline->mType = PIPELINE_TYPE_GRAPHICS;

	//add to gpu
	DECLARE_ZERO(D3D12_SHADER_BYTECODE, VS);
	DECLARE_ZERO(D3D12_SHADER_BYTECODE, PS);
	DECLARE_ZERO(D3D12_SHADER_BYTECODE, DS);
	DECLARE_ZERO(D3D12_SHADER_BYTECODE, HS);
	DECLARE_ZERO(D3D12_SHADER_BYTECODE, GS);
	if (pShaderProgram->mStages & SHADER_STAGE_VERT)
	{
		VS.BytecodeLength = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mVertexStageIndex]->GetBufferSize();
		VS.pShaderBytecode = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mVertexStageIndex]->GetBufferPointer();
	}
	if (pShaderProgram->mStages & SHADER_STAGE_FRAG)
	{
		PS.BytecodeLength = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mPixelStageIndex]->GetBufferSize();
		PS.pShaderBytecode = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mPixelStageIndex]->GetBufferPointer();
	}
	if (pShaderProgram->mStages & SHADER_STAGE_HULL)
	{
		HS.BytecodeLength = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mHullStageIndex]->GetBufferSize();
		HS.pShaderBytecode = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mHullStageIndex]->GetBufferPointer();
	}
	if (pShaderProgram->mStages & SHADER_STAGE_DOMN)
	{
		DS.BytecodeLength = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mDomainStageIndex]->GetBufferSize();
		DS.pShaderBytecode = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mDomainStageIndex]->GetBufferPointer();
	}
	if (pShaderProgram->mStages & SHADER_STAGE_GEOM)
	{
		GS.BytecodeLength = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mGeometryStageIndex]->GetBufferSize();
		GS.pShaderBytecode = pShaderProgram->pShaderBlobs[pShaderProgram->mReflection.mGeometryStageIndex]->GetBufferPointer();
	}

	DECLARE_ZERO(D3D12_STREAM_OUTPUT_DESC, stream_output_desc);
	stream_output_desc.pSODeclaration = NULL;
	stream_output_desc.NumEntries = 0;
	stream_output_desc.pBufferStrides = NULL;
	stream_output_desc.NumStrides = 0;
	stream_output_desc.RasterizedStream = 0;

	DECLARE_ZERO(D3D12_DEPTH_STENCILOP_DESC, depth_stencilop_desc);
	depth_stencilop_desc.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	depth_stencilop_desc.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	depth_stencilop_desc.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	depth_stencilop_desc.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	uint32_t input_elementCount = 0;
	DECLARE_ZERO(D3D12_INPUT_ELEMENT_DESC, input_elements[MAX_VERTEX_ATTRIBS]);

	DECLARE_ZERO(char, semantic_names[MAX_VERTEX_ATTRIBS][MAX_SEMANTIC_NAME_LENGTH]);
	// Make sure there's attributes
	if (pVertexLayout != NULL)
	{

		//uint32_t attrib_count = min(pVertexLayout->mAttribCount, MAX_VERTEX_ATTRIBS);  //Not used
		for (uint32_t attrib_index = 0; attrib_index < pVertexLayout->mAttribCount; ++attrib_index)
		{
			const VertexAttrib* attrib = &(pVertexLayout->mAttribs[attrib_index]);

			ASSERT(SEMANTIC_UNDEFINED != attrib->mSemantic);

			if (attrib->mSemanticNameLength > 0)
			{
				uint32_t name_length = min(MAX_SEMANTIC_NAME_LENGTH, attrib->mSemanticNameLength);
				strncpy_s(semantic_names[attrib_index], attrib->mSemanticName, name_length);
			}
			else
			{
				switch (attrib->mSemantic)
				{
					case SEMANTIC_POSITION: strcpy_s(semantic_names[attrib_index], "POSITION"); break;
					case SEMANTIC_NORMAL: strcpy_s(semantic_names[attrib_index], "NORMAL"); break;
					case SEMANTIC_COLOR: strcpy_s(semantic_names[attrib_index], "COLOR"); break;
					case SEMANTIC_TANGENT: strcpy_s(semantic_names[attrib_index], "TANGENT"); break;
					case SEMANTIC_BITANGENT: strcpy_s(semantic_names[attrib_index], "BINORMAL"); break;
					case SEMANTIC_TEXCOORD0:
					case SEMANTIC_TEXCOORD1:
					case SEMANTIC_TEXCOORD2:
					case SEMANTIC_TEXCOORD3:
					case SEMANTIC_TEXCOORD4:
					case SEMANTIC_TEXCOORD5:
					case SEMANTIC_TEXCOORD6:
					case SEMANTIC_TEXCOORD7:
					case SEMANTIC_TEXCOORD8:
					case SEMANTIC_TEXCOORD9: strcpy_s(semantic_names[attrib_index], "TEXCOORD"); break;
					default: ASSERT(false); break;
				}
			}

			UINT semantic_index = 0;
			switch (attrib->mSemantic)
			{
				case SEMANTIC_TEXCOORD0: semantic_index = 0; break;
				case SEMANTIC_TEXCOORD1: semantic_index = 1; break;
				case SEMANTIC_TEXCOORD2: semantic_index = 2; break;
				case SEMANTIC_TEXCOORD3: semantic_index = 3; break;
				case SEMANTIC_TEXCOORD4: semantic_index = 4; break;
				case SEMANTIC_TEXCOORD5: semantic_index = 5; break;
				case SEMANTIC_TEXCOORD6: semantic_index = 6; break;
				case SEMANTIC_TEXCOORD7: semantic_index = 7; break;
				case SEMANTIC_TEXCOORD8: semantic_index = 8; break;
				case SEMANTIC_TEXCOORD9: semantic_index = 9; break;
				default: break;
			}

			input_elements[input_elementCount].SemanticName = semantic_names[attrib_index];
			input_elements[input_elementCount].SemanticIndex = semantic_index;

			input_elements[input_elementCount].Format = (DXGI_FORMAT) TinyImageFormat_ToDXGI_FORMAT(attrib->mFormat);
			input_elements[input_elementCount].InputSlot = attrib->mBinding;
			input_elements[input_elementCount].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
			if (attrib->mRate == VERTEX_ATTRIB_RATE_INSTANCE)
			{
				input_elements[input_elementCount].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
				input_elements[input_elementCount].InstanceDataStepRate = 1;
			}
			else
			{
				input_elements[input_elementCount].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
				input_elements[input_elementCount].InstanceDataStepRate = 0;
			}
			++input_elementCount;
		}
	}

	DECLARE_ZERO(D3D12_INPUT_LAYOUT_DESC, input_layout_desc);
	input_layout_desc.pInputElementDescs = input_elementCount ? input_elements : NULL;
	input_layout_desc.NumElements = input_elementCount;

	uint32_t render_target_count = min(pDesc->mRenderTargetCount, MAX_RENDER_TARGET_ATTACHMENTS);
	render_target_count = min(render_target_count, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);

	DECLARE_ZERO(DXGI_SAMPLE_DESC, sample_desc);
	sample_desc.Count = (UINT)(pDesc->mSampleCount);
	sample_desc.Quality = (UINT)(pDesc->mSampleQuality);

	DECLARE_ZERO(D3D12_CACHED_PIPELINE_STATE, cached_pso_desc);
	cached_pso_desc.pCachedBlob = NULL;
	cached_pso_desc.CachedBlobSizeInBytes = 0;

	DECLARE_ZERO(D3D12_GRAPHICS_PIPELINE_STATE_DESC, pipeline_state_desc);
	pipeline_state_desc.pRootSignature = pDesc->pRootSignature->pDxRootSignature;
	pipeline_state_desc.VS = VS;
	pipeline_state_desc.PS = PS;
	pipeline_state_desc.DS = DS;
	pipeline_state_desc.HS = HS;
	pipeline_state_desc.GS = GS;
	pipeline_state_desc.StreamOutput = stream_output_desc;
	pipeline_state_desc.BlendState =
		pDesc->pBlendState != NULL ? pDesc->pBlendState->mDxBlendDesc : pRenderer->pDefaultBlendState->mDxBlendDesc;

	pipeline_state_desc.SampleMask = UINT_MAX;

	pipeline_state_desc.RasterizerState = pDesc->pRasterizerState != NULL ? pDesc->pRasterizerState->mDxRasterizerDesc
																		  : pRenderer->pDefaultRasterizerState->mDxRasterizerDesc;

	pipeline_state_desc.DepthStencilState =
		pDesc->pDepthState != NULL ? pDesc->pDepthState->mDxDepthStencilDesc : pRenderer->pDefaultDepthState->mDxDepthStencilDesc;

	pipeline_state_desc.InputLayout = input_layout_desc;
	pipeline_state_desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
	pipeline_state_desc.PrimitiveTopologyType = util_to_dx_primitive_topology_type(pDesc->mPrimitiveTopo);
	pipeline_state_desc.NumRenderTargets = render_target_count;
	pipeline_state_desc.DSVFormat = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(pDesc->mDepthStencilFormat);

	pipeline_state_desc.SampleDesc = sample_desc;
	pipeline_state_desc.CachedPSO = cached_pso_desc;
	pipeline_state_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	for (uint32_t attrib_index = 0; attrib_index < render_target_count; ++attrib_index)
	{
		pipeline_state_desc.RTVFormats[attrib_index] = (DXGI_FORMAT)TinyImageFormat_ToDXGI_FORMAT(
				pDesc->pColorFormats[attrib_index]);
	}

	// If running Linked Mode (SLI) create pipeline for all nodes
	// #NOTE : In non SLI mode, mNodeCount will be 0 which sets nodeMask to default value
	pipeline_state_desc.NodeMask = util_calculate_shared_node_mask(pRenderer);

	HRESULT hres = pRenderer->pDxDevice->CreateGraphicsPipelineState(
		&pipeline_state_desc, __uuidof(pPipeline->pDxPipelineState), (void**)&(pPipeline->pDxPipelineState));
	ASSERT(SUCCEEDED(hres));

	D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	switch (pPipeline->mGraphics.mPrimitiveTopo)
	{
		case PRIMITIVE_TOPO_POINT_LIST: topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST; break;
		case PRIMITIVE_TOPO_LINE_LIST: topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; break;
		case PRIMITIVE_TOPO_LINE_STRIP: topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; break;
		case PRIMITIVE_TOPO_TRI_LIST: topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
		case PRIMITIVE_TOPO_TRI_STRIP: topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
		case PRIMITIVE_TOPO_PATCH_LIST:
		{
			const PipelineReflection* pReflection = &pPipeline->mGraphics.pShaderProgram->mReflection;
			uint32_t                  controlPoint = pReflection->mStageReflections[pReflection->mHullStageIndex].mNumControlPoint;
			topology = (D3D_PRIMITIVE_TOPOLOGY)(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + (controlPoint - 1));
		}
		break;

		default: break;
	}
	ASSERT(D3D_PRIMITIVE_TOPOLOGY_UNDEFINED != topology);
	pPipeline->mDxPrimitiveTopology = topology;

	//save new pipeline in given pointer
	*ppPipeline = pPipeline;
}

void addComputePipeline(Renderer* pRenderer, const ComputePipelineDesc* pDesc, Pipeline** ppPipeline)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(pDesc->pShaderProgram);
	ASSERT(pDesc->pRootSignature);
	ASSERT(pDesc->pShaderProgram->pShaderBlobs[0]);

	//allocate new pipeline
	Pipeline* pPipeline = (Pipeline*)conf_calloc(1, sizeof(*pPipeline));
	ASSERT(pPipeline);

	//copy pipeline settings
	memcpy(&(pPipeline->mCompute), pDesc, sizeof(*pDesc));
	pPipeline->mType = PIPELINE_TYPE_COMPUTE;

	//add pipeline specifying its for compute purposes
	DECLARE_ZERO(D3D12_SHADER_BYTECODE, CS);
	CS.BytecodeLength = pDesc->pShaderProgram->pShaderBlobs[0]->GetBufferSize();
	CS.pShaderBytecode = pDesc->pShaderProgram->pShaderBlobs[0]->GetBufferPointer();

	DECLARE_ZERO(D3D12_CACHED_PIPELINE_STATE, cached_pso_desc);
	cached_pso_desc.pCachedBlob = NULL;
	cached_pso_desc.CachedBlobSizeInBytes = 0;

	DECLARE_ZERO(D3D12_COMPUTE_PIPELINE_STATE_DESC, pipeline_state_desc);
	pipeline_state_desc.pRootSignature = pDesc->pRootSignature->pDxRootSignature;
	pipeline_state_desc.CS = CS;
	pipeline_state_desc.CachedPSO = cached_pso_desc;
#ifndef _DURANGO
	pipeline_state_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
#endif

	// If running Linked Mode (SLI) create pipeline for all nodes
	// #NOTE : In non SLI mode, mNodeCount will be 0 which sets nodeMask to default value
	pipeline_state_desc.NodeMask = util_calculate_shared_node_mask(pRenderer);

	HRESULT hres = pRenderer->pDxDevice->CreateComputePipelineState(
		&pipeline_state_desc, __uuidof(pPipeline->pDxPipelineState), (void**)&(pPipeline->pDxPipelineState));
	ASSERT(SUCCEEDED(hres));

	*ppPipeline = pPipeline;
}

void addPipeline(Renderer* pRenderer, const PipelineDesc* p_pipeline_settings, Pipeline** pp_pipeline)
{
	switch (p_pipeline_settings->mType)
	{
		case (PIPELINE_TYPE_COMPUTE):
		{
			addComputePipeline(pRenderer, &p_pipeline_settings->mComputeDesc, pp_pipeline);
			break;
		}
		case (PIPELINE_TYPE_GRAPHICS):
		{
			addPipeline(pRenderer, &p_pipeline_settings->mGraphicsDesc, pp_pipeline);
			break;
		}
#ifdef ENABLE_RAYTRACING
		case (PIPELINE_TYPE_RAYTRACING):
		{
			d3d12_addRaytracingPipeline(&p_pipeline_settings->mRaytracingDesc, pp_pipeline);
			break;
		}
#endif
		default:
		{
			ASSERT(false);
			pp_pipeline = NULL;
			break;
		}
	}
}

void removePipeline(Renderer* pRenderer, Pipeline* pPipeline)
{
	ASSERT(pRenderer);
	ASSERT(pPipeline);

	//delete pipeline from device
	SAFE_RELEASE(pPipeline->pDxPipelineState);
#ifdef ENABLE_RAYTRACING
	SAFE_RELEASE(pPipeline->pDxrPipeline);
#endif

	SAFE_FREE(pPipeline);
}

void addBlendState(Renderer* pRenderer, const BlendStateDesc* pDesc, BlendState** ppBlendState)
{
	UNREF_PARAM(pRenderer);

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

	BlendState* pBlendState = (BlendState*)conf_calloc(1, sizeof(*pBlendState));

	pBlendState->mDxBlendDesc.AlphaToCoverageEnable = (BOOL)pDesc->mAlphaToCoverage;
	pBlendState->mDxBlendDesc.IndependentBlendEnable = TRUE;
	for (int i = 0; i < MAX_RENDER_TARGET_ATTACHMENTS; i++)
	{
		if (pDesc->mRenderTargetMask & (1 << i))
		{
			BOOL blendEnable =
				(gDx12BlendConstantTranslator[pDesc->mSrcFactors[blendDescIndex]] != D3D12_BLEND_ONE ||
				 gDx12BlendConstantTranslator[pDesc->mDstFactors[blendDescIndex]] != D3D12_BLEND_ZERO ||
				 gDx12BlendConstantTranslator[pDesc->mSrcAlphaFactors[blendDescIndex]] != D3D12_BLEND_ONE ||
				 gDx12BlendConstantTranslator[pDesc->mDstAlphaFactors[blendDescIndex]] != D3D12_BLEND_ZERO);

			pBlendState->mDxBlendDesc.RenderTarget[i].BlendEnable = blendEnable;
			pBlendState->mDxBlendDesc.RenderTarget[i].RenderTargetWriteMask = (UINT8)pDesc->mMasks[blendDescIndex];
			pBlendState->mDxBlendDesc.RenderTarget[i].BlendOp = gDx12BlendOpTranslator[pDesc->mBlendModes[blendDescIndex]];
			pBlendState->mDxBlendDesc.RenderTarget[i].SrcBlend = gDx12BlendConstantTranslator[pDesc->mSrcFactors[blendDescIndex]];
			pBlendState->mDxBlendDesc.RenderTarget[i].DestBlend = gDx12BlendConstantTranslator[pDesc->mDstFactors[blendDescIndex]];
			pBlendState->mDxBlendDesc.RenderTarget[i].BlendOpAlpha = gDx12BlendOpTranslator[pDesc->mBlendAlphaModes[blendDescIndex]];
			pBlendState->mDxBlendDesc.RenderTarget[i].SrcBlendAlpha = gDx12BlendConstantTranslator[pDesc->mSrcAlphaFactors[blendDescIndex]];
			pBlendState->mDxBlendDesc.RenderTarget[i].DestBlendAlpha =
				gDx12BlendConstantTranslator[pDesc->mDstAlphaFactors[blendDescIndex]];
		}

		if (pDesc->mIndependentBlend)
			++blendDescIndex;
	}

	*ppBlendState = pBlendState;
}

void removeBlendState(BlendState* pBlendState) { SAFE_FREE(pBlendState); }

void addDepthState(Renderer* pRenderer, const DepthStateDesc* pDesc, DepthState** ppDepthState)
{
	UNREF_PARAM(pRenderer);

	ASSERT(pDesc->mDepthFunc < CompareMode::MAX_COMPARE_MODES);
	ASSERT(pDesc->mStencilFrontFunc < CompareMode::MAX_COMPARE_MODES);
	ASSERT(pDesc->mStencilFrontFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mDepthFrontFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mStencilFrontPass < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mStencilBackFunc < CompareMode::MAX_COMPARE_MODES);
	ASSERT(pDesc->mStencilBackFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mDepthBackFail < StencilOp::MAX_STENCIL_OPS);
	ASSERT(pDesc->mStencilBackPass < StencilOp::MAX_STENCIL_OPS);

	DepthState* pDepthState = (DepthState*)conf_calloc(1, sizeof(*pDepthState));

	pDepthState->mDxDepthStencilDesc.DepthEnable = (BOOL)pDesc->mDepthTest;
	pDepthState->mDxDepthStencilDesc.DepthWriteMask = pDesc->mDepthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	pDepthState->mDxDepthStencilDesc.DepthFunc = gDx12ComparisonFuncTranslator[pDesc->mDepthFunc];
	pDepthState->mDxDepthStencilDesc.StencilEnable = (BOOL)pDesc->mStencilTest;
	pDepthState->mDxDepthStencilDesc.StencilReadMask = pDesc->mStencilReadMask;
	pDepthState->mDxDepthStencilDesc.StencilWriteMask = pDesc->mStencilWriteMask;
	pDepthState->mDxDepthStencilDesc.BackFace.StencilFunc = gDx12ComparisonFuncTranslator[pDesc->mStencilBackFunc];
	pDepthState->mDxDepthStencilDesc.FrontFace.StencilFunc = gDx12ComparisonFuncTranslator[pDesc->mStencilFrontFunc];
	pDepthState->mDxDepthStencilDesc.BackFace.StencilDepthFailOp = gDx12StencilOpTranslator[pDesc->mDepthBackFail];
	pDepthState->mDxDepthStencilDesc.FrontFace.StencilDepthFailOp = gDx12StencilOpTranslator[pDesc->mDepthFrontFail];
	pDepthState->mDxDepthStencilDesc.BackFace.StencilFailOp = gDx12StencilOpTranslator[pDesc->mStencilBackFail];
	pDepthState->mDxDepthStencilDesc.FrontFace.StencilFailOp = gDx12StencilOpTranslator[pDesc->mStencilFrontFail];
	pDepthState->mDxDepthStencilDesc.BackFace.StencilPassOp = gDx12StencilOpTranslator[pDesc->mStencilBackPass];
	pDepthState->mDxDepthStencilDesc.FrontFace.StencilPassOp = gDx12StencilOpTranslator[pDesc->mStencilFrontPass];

	*ppDepthState = pDepthState;
}

void removeDepthState(DepthState* pDepthState) { SAFE_FREE(pDepthState); }

void addRasterizerState(Renderer* pRenderer, const RasterizerStateDesc* pDesc, RasterizerState** ppRasterizerState)
{
	UNREF_PARAM(pRenderer);

	ASSERT(pDesc->mFillMode < FillMode::MAX_FILL_MODES);
	ASSERT(pDesc->mCullMode < CullMode::MAX_CULL_MODES);
	ASSERT(pDesc->mFrontFace == FRONT_FACE_CCW || pDesc->mFrontFace == FRONT_FACE_CW);

	RasterizerState* pRasterizerState = (RasterizerState*)conf_calloc(1, sizeof(*pRasterizerState));

	pRasterizerState->mDxRasterizerDesc.FillMode = gDx12FillModeTranslator[pDesc->mFillMode];
	pRasterizerState->mDxRasterizerDesc.CullMode = gDx12CullModeTranslator[pDesc->mCullMode];
	pRasterizerState->mDxRasterizerDesc.FrontCounterClockwise = pDesc->mFrontFace == FRONT_FACE_CCW;
	pRasterizerState->mDxRasterizerDesc.DepthBias = pDesc->mDepthBias;
	pRasterizerState->mDxRasterizerDesc.DepthBiasClamp = 0.0f;
	pRasterizerState->mDxRasterizerDesc.SlopeScaledDepthBias = pDesc->mSlopeScaledDepthBias;
	pRasterizerState->mDxRasterizerDesc.DepthClipEnable = TRUE;
	pRasterizerState->mDxRasterizerDesc.MultisampleEnable = pDesc->mMultiSample ? TRUE : FALSE;
	pRasterizerState->mDxRasterizerDesc.AntialiasedLineEnable = FALSE;
	pRasterizerState->mDxRasterizerDesc.ForcedSampleCount = 0;
	pRasterizerState->mDxRasterizerDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

	*ppRasterizerState = pRasterizerState;
}

void removeRasterizerState(RasterizerState* pRasterizerState) { SAFE_FREE(pRasterizerState); }
/************************************************************************/
// Command buffer Functions
/************************************************************************/
void beginCmd(Cmd* pCmd)
{
	ASSERT(pCmd);
	ASSERT(pCmd->pDxCmdList);
	ASSERT(pCmd->pDxCmdAlloc);

	HRESULT hres = pCmd->pDxCmdAlloc->Reset();
	ASSERT(SUCCEEDED(hres));

	hres = pCmd->pDxCmdList->Reset(pCmd->pDxCmdAlloc, NULL);
	ASSERT(SUCCEEDED(hres));

	if (pCmd->pCmdPool->mCmdPoolDesc.mCmdPoolType != CMD_POOL_COPY)
	{
		ID3D12DescriptorHeap* pHeaps[] =
		{
			pCmd->pRenderer->pCbvSrvUavHeaps[pCmd->mNodeIndex]->pCurrentHeap,
			pCmd->pRenderer->pSamplerHeaps[pCmd->mNodeIndex]->pCurrentHeap,
		};
		pCmd->pDxCmdList->SetDescriptorHeaps(2, pHeaps);
	}

	// Reset CPU side data
	pCmd->pBoundRootSignature = NULL;
	for (uint32_t i = 0; i < DESCRIPTOR_UPDATE_FREQ_COUNT; ++i)
	{
		pCmd->pBoundDescriptorSets[i] = NULL;
		pCmd->mBoundDescriptorSetIndices[i] = -1;
	}
}

#ifdef _DURANGO
void beginCmd(DmaCmd* pCmd)
{
	ASSERT(pCmd);
	ASSERT(pCmd->pDxCmdList);
	ASSERT(pCmd->pDxCmdAlloc);

	HRESULT hres = pCmd->pDxCmdAlloc->Reset();
	ASSERT(SUCCEEDED(hres));

	hres = pCmd->pDxCmdList->Reset(pCmd->pDxCmdAlloc, NULL);
	ASSERT(SUCCEEDED(hres));
}
#endif

void endCmd(Cmd* pCmd)
{
	ASSERT(pCmd);
	ASSERT(pCmd->pDxCmdList);

	HRESULT hres = pCmd->pDxCmdList->Close();
	ASSERT(SUCCEEDED(hres));
}

#ifdef _DURANGO
void endCmd(DmaCmd* pCmd)
{
	ASSERT(pCmd);
	ASSERT(pCmd->pDxCmdList);

	HRESULT hres = pCmd->pDxCmdList->Close();
	ASSERT(SUCCEEDED(hres));
}
#endif

void cmdBindRenderTargets(
	Cmd* pCmd, uint32_t renderTargetCount, RenderTarget** ppRenderTargets, RenderTarget* pDepthStencil,
	const LoadActionsDesc* pLoadActions /* = NULL*/, uint32_t* pColorArraySlices, uint32_t* pColorMipSlices, uint32_t depthArraySlice,
	uint32_t depthMipSlice)
{
	ASSERT(pCmd);
	ASSERT(pCmd->pDxCmdList);

	if (!renderTargetCount && !pDepthStencil)
		return;

	D3D12_CPU_DESCRIPTOR_HANDLE* p_dsv_handle = NULL;
	D3D12_CPU_DESCRIPTOR_HANDLE* p_rtv_handles =
		renderTargetCount ? (D3D12_CPU_DESCRIPTOR_HANDLE*)alloca(renderTargetCount * sizeof(D3D12_CPU_DESCRIPTOR_HANDLE)) : NULL;
	for (uint32_t i = 0; i < renderTargetCount; ++i)
	{
		uint32_t handle = 0;
		if (pColorMipSlices)
		{
			if (pColorArraySlices)
				handle = 1 + pColorMipSlices[i] * ppRenderTargets[i]->mDesc.mArraySize + pColorArraySlices[i];
			else
				handle = 1 + pColorMipSlices[i];
		}
		else if (pColorArraySlices)
		{
			handle = 1 + pColorArraySlices[i];
		}

		p_rtv_handles[i] = ppRenderTargets[i]->pDxDescriptors[handle];
	}

	if (pDepthStencil)
	{
		uint32_t handle = 0;
		if (depthMipSlice != -1)
		{
			if (depthArraySlice != -1)
				handle = 1 + depthMipSlice * pDepthStencil->mDesc.mArraySize + depthArraySlice;
			else
				handle = 1 + depthMipSlice;
		}
		else if (depthArraySlice != -1)
		{
			handle = 1 + depthArraySlice;
		}

		p_dsv_handle = &pDepthStencil->pDxDescriptors[handle];
	}

	pCmd->pDxCmdList->OMSetRenderTargets(renderTargetCount, p_rtv_handles, FALSE, p_dsv_handle);

	//process clear actions (clear color/depth)
	if (pLoadActions)
	{
		for (uint32_t i = 0; i < renderTargetCount; ++i)
		{
			if (pLoadActions->mLoadActionsColor[i] == LOAD_ACTION_CLEAR)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE handle = p_rtv_handles[i];

				DECLARE_ZERO(FLOAT, color_rgba[4]);
				color_rgba[0] = pLoadActions->mClearColorValues[i].r;
				color_rgba[1] = pLoadActions->mClearColorValues[i].g;
				color_rgba[2] = pLoadActions->mClearColorValues[i].b;
				color_rgba[3] = pLoadActions->mClearColorValues[i].a;

				pCmd->pDxCmdList->ClearRenderTargetView(handle, color_rgba, 0, NULL);
			}
		}
		if (pLoadActions->mLoadActionDepth == LOAD_ACTION_CLEAR || pLoadActions->mLoadActionStencil == LOAD_ACTION_CLEAR)
		{
			D3D12_CLEAR_FLAGS flags = (D3D12_CLEAR_FLAGS)0;
			if (pLoadActions->mLoadActionDepth == LOAD_ACTION_CLEAR)
				flags |= D3D12_CLEAR_FLAG_DEPTH;
			if (pLoadActions->mLoadActionStencil == LOAD_ACTION_CLEAR)
				flags |= D3D12_CLEAR_FLAG_STENCIL;
			ASSERT(flags);
			pCmd->pDxCmdList->ClearDepthStencilView(
				*p_dsv_handle, flags, pLoadActions->mClearDepth.depth, (UINT8)pLoadActions->mClearDepth.stencil, 0, NULL);
		}
	}
}

void cmdSetViewport(Cmd* pCmd, float x, float y, float width, float height, float minDepth, float maxDepth)
{
	ASSERT(pCmd);

	//set new viewport
	ASSERT(pCmd->pDxCmdList);

	D3D12_VIEWPORT viewport;
	viewport.TopLeftX = x;
	viewport.TopLeftY = y;
	viewport.Width = width;
	viewport.Height = height;
	viewport.MinDepth = minDepth;
	viewport.MaxDepth = maxDepth;

	pCmd->pDxCmdList->RSSetViewports(1, &viewport);
}

void cmdSetScissor(Cmd* pCmd, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	ASSERT(pCmd);

	//set new scissor values
	ASSERT(pCmd->pDxCmdList);

	D3D12_RECT scissor;
	scissor.left = x;
	scissor.top = y;
	scissor.right = x + width;
	scissor.bottom = y + height;

	pCmd->pDxCmdList->RSSetScissorRects(1, &scissor);
}

void cmdBindPipeline(Cmd* pCmd, Pipeline* pPipeline)
{
	ASSERT(pCmd);
	ASSERT(pPipeline);

	//bind given pipeline
	ASSERT(pCmd->pDxCmdList);


	if (pPipeline->mType == PIPELINE_TYPE_GRAPHICS)
	{
		reset_root_signature(pCmd, pPipeline->mGraphics.pRootSignature);
		pCmd->pDxCmdList->IASetPrimitiveTopology(pPipeline->mDxPrimitiveTopology);
	}
#ifdef ENABLE_RAYTRACING
	if (pPipeline->mType == PIPELINE_TYPE_RAYTRACING)
	{
		reset_root_signature(pCmd, pPipeline->mCompute.pRootSignature);
		d3d12_cmdBindRaytracingPipeline(pCmd, pPipeline);
	}
	else
#endif
	{
		ASSERT(pPipeline->pDxPipelineState);
		reset_root_signature(pCmd, pPipeline->mCompute.pRootSignature);
		pCmd->pDxCmdList->SetPipelineState(pPipeline->pDxPipelineState);
	}
}

void cmdBindIndexBuffer(Cmd* pCmd, Buffer* pBuffer, uint64_t offset)
{
	ASSERT(pCmd);
	ASSERT(pBuffer);
	ASSERT(pCmd->pDxCmdList);
	ASSERT(D3D12_GPU_VIRTUAL_ADDRESS_NULL != pBuffer->mDxGpuAddress);

	D3D12_INDEX_BUFFER_VIEW ibView = {};
	ibView.BufferLocation = pBuffer->mDxGpuAddress + offset;
	ibView.Format = pBuffer->mDxIndexFormat;
	ibView.SizeInBytes = (UINT)(pBuffer->mDesc.mSize - offset);

	//bind given index buffer
	pCmd->pDxCmdList->IASetIndexBuffer(&ibView);
}

void cmdBindVertexBuffer(Cmd* pCmd, uint32_t bufferCount, Buffer** ppBuffers, uint64_t* pOffsets)
{
	ASSERT(pCmd);
	ASSERT(0 != bufferCount);
	ASSERT(ppBuffers);
	ASSERT(pCmd->pDxCmdList);
	//bind given vertex buffer

	DECLARE_ZERO(D3D12_VERTEX_BUFFER_VIEW, views[MAX_VERTEX_ATTRIBS]);
	for (uint32_t i = 0; i < bufferCount; ++i)
	{
		ASSERT(D3D12_GPU_VIRTUAL_ADDRESS_NULL != ppBuffers[i]->mDxGpuAddress);

		views[i].BufferLocation = (ppBuffers[i]->mDxGpuAddress + (pOffsets ? pOffsets[i] : 0));
		views[i].SizeInBytes = (UINT)(ppBuffers[i]->mDesc.mSize - (pOffsets ? pOffsets[i] : 0));
		views[i].StrideInBytes = (UINT)ppBuffers[i]->mDesc.mVertexStride;
	}

	pCmd->pDxCmdList->IASetVertexBuffers(0, bufferCount, views);
}

void cmdDraw(Cmd* pCmd, uint32_t vertexCount, uint32_t firstVertex)
{
	ASSERT(pCmd);

	//draw given vertices
	ASSERT(pCmd->pDxCmdList);

	pCmd->pDxCmdList->DrawInstanced((UINT)vertexCount, (UINT)1, (UINT)firstVertex, (UINT)0);
}

void cmdDrawInstanced(Cmd* pCmd, uint32_t vertexCount, uint32_t firstVertex, uint32_t instanceCount, uint32_t firstInstance)
{
	ASSERT(pCmd);

	//draw given vertices
	ASSERT(pCmd->pDxCmdList);

	pCmd->pDxCmdList->DrawInstanced((UINT)vertexCount, (UINT)instanceCount, (UINT)firstVertex, (UINT)firstInstance);
}

void cmdDrawIndexed(Cmd* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t firstVertex)
{
	ASSERT(pCmd);

	//draw indexed mesh
	ASSERT(pCmd->pDxCmdList);

	pCmd->pDxCmdList->DrawIndexedInstanced((UINT)indexCount, (UINT)1, (UINT)firstIndex, (UINT)firstVertex, (UINT)0);
}

void cmdDrawIndexedInstanced(
	Cmd* pCmd, uint32_t indexCount, uint32_t firstIndex, uint32_t instanceCount, uint32_t firstInstance, uint32_t firstVertex)
{
	ASSERT(pCmd);

	//draw indexed mesh
	ASSERT(pCmd->pDxCmdList);

	pCmd->pDxCmdList->DrawIndexedInstanced((UINT)indexCount, (UINT)instanceCount, (UINT)firstIndex, (UINT)firstVertex, (UINT)firstInstance);
}

void cmdDispatch(Cmd* pCmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	ASSERT(pCmd);

	//dispatch given command
	ASSERT(pCmd->pDxCmdList != NULL);

	pCmd->pDxCmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void cmdResourceBarrier(Cmd* pCmd, uint32_t numBufferBarriers, BufferBarrier* pBufferBarriers, uint32_t numTextureBarriers, TextureBarrier* pTextureBarriers)
{
	D3D12_RESOURCE_BARRIER* barriers =
		(D3D12_RESOURCE_BARRIER*)alloca((numBufferBarriers + numTextureBarriers) * sizeof(D3D12_RESOURCE_BARRIER));
	uint32_t transitionCount = 0;

	for (uint32_t i = 0; i < numBufferBarriers; ++i)
	{
		BufferBarrier*          pTransBarrier = &pBufferBarriers[i];
		D3D12_RESOURCE_BARRIER* pBarrier = &barriers[transitionCount];
		Buffer*                 pBuffer = pTransBarrier->pBuffer;

		// Only transition GPU visible resources.
		// Note: General CPU_TO_GPU resources have to stay in generic read state. They are created in upload heap.
		// There is one corner case: CPU_TO_GPU resources with UAV usage can have state transition. And they are created in custom heap.
		if (pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_ONLY ||
			pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_TO_CPU ||
			(pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_CPU_TO_GPU && pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER))
		{
			//if (!(pBuffer->mCurrentState & pTransBarrier->mNewState) && pBuffer->mCurrentState != pTransBarrier->mNewState)
			if (pBuffer->mCurrentState != pTransBarrier->mNewState)
			{
				if (pTransBarrier->mSplit)
				{
					ResourceState currentState = pBuffer->mCurrentState;
					// Determine if the barrier is begin only or end only
					// If the previous state and new state are same, we know this is end only since the state was already set in begin only
					if (pBuffer->mPreviousState & pTransBarrier->mNewState)
					{
						pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
						pBuffer->mPreviousState = RESOURCE_STATE_UNDEFINED;
						pBuffer->mCurrentState = pTransBarrier->mNewState;
					}
					else
					{
						pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
						pBuffer->mPreviousState = pTransBarrier->mNewState;
					}

					pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					pBarrier->Transition.pResource = pBuffer->pDxResource;
					pBarrier->Transition.StateBefore = util_to_dx_resource_state(currentState);
					pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);

					++transitionCount;
				}
				else
				{
					pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					pBarrier->Transition.pResource = pBuffer->pDxResource;
					pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					pBarrier->Transition.StateBefore = util_to_dx_resource_state(pBuffer->mCurrentState);
					pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);

					pBuffer->mCurrentState = pTransBarrier->mNewState;

					++transitionCount;
				}
			}
			else if (pTransBarrier->mNewState == RESOURCE_STATE_UNORDERED_ACCESS)
			{
				pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				pBarrier->UAV.pResource = pBuffer->pDxResource;
				++transitionCount;
			}
		}
	}
	for (uint32_t i = 0; i < numTextureBarriers; ++i)
	{
		TextureBarrier*         pTransBarrier = &pTextureBarriers[i];
		D3D12_RESOURCE_BARRIER* pBarrier = &barriers[transitionCount];
		Texture*                pTexture = pTransBarrier->pTexture;

		if (pTexture->mCurrentState != pTransBarrier->mNewState)
		{
			if (pTransBarrier->mSplit)
			{
				ResourceState currentState = pTexture->mCurrentState;
				// Determine if the barrier is begin only or end only
				// If the previous state and new state are same, we know this is end only since the state was already set in begin only
				if (pTexture->mPreviousState & pTransBarrier->mNewState)
				{
					pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
					pTexture->mPreviousState = RESOURCE_STATE_UNDEFINED;
					pTexture->mCurrentState = pTransBarrier->mNewState;
				}
				else
				{
					pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
					pTexture->mPreviousState = pTransBarrier->mNewState;
				}

				pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				pBarrier->Transition.pResource = pTexture->pDxResource;
				pBarrier->Transition.StateBefore = util_to_dx_resource_state(currentState);
				pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);

				++transitionCount;
			}
			else
			{
				pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				pBarrier->Transition.pResource = pTexture->pDxResource;
				pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
				pBarrier->Transition.StateBefore = util_to_dx_resource_state(pTexture->mCurrentState);
				pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);
				pTexture->mCurrentState = pTransBarrier->mNewState;

				++transitionCount;
			}
		}
		else if (pTransBarrier->mNewState == RESOURCE_STATE_UNORDERED_ACCESS)
		{
			pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			pBarrier->UAV.pResource = pTexture->pDxResource;
			++transitionCount;
		}
	}

	if (transitionCount)
		pCmd->pDxCmdList->ResourceBarrier(transitionCount, barriers);
}

#ifdef _DURANGO
void cmdResourceBarrier(DmaCmd* pCmd, uint32_t numBufferBarriers, BufferBarrier* pBufferBarriers, uint32_t numTextureBarriers, TextureBarrier* pTextureBarriers)
{
	D3D12_RESOURCE_BARRIER* barriers =
		(D3D12_RESOURCE_BARRIER*)alloca((numBufferBarriers + numTextureBarriers) * sizeof(D3D12_RESOURCE_BARRIER));
	uint32_t transitionCount = 0;

	for (uint32_t i = 0; i < numBufferBarriers; ++i)
	{
		BufferBarrier*          pTransBarrier = &pBufferBarriers[i];
		D3D12_RESOURCE_BARRIER* pBarrier = &barriers[transitionCount];
		Buffer*                 pBuffer = pTransBarrier->pBuffer;

		// Only transition GPU visible resources.
		// Note: General CPU_TO_GPU resources have to stay in generic read state. They are created in upload heap.
		// There is one corner case: CPU_TO_GPU resources with UAV usage can have state transition. And they are created in custom heap.
		if (pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_ONLY ||
			pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_GPU_TO_CPU ||
			(pBuffer->mDesc.mMemoryUsage == RESOURCE_MEMORY_USAGE_CPU_TO_GPU && pBuffer->mDesc.mDescriptors & DESCRIPTOR_TYPE_RW_BUFFER))
		{
			//if (!(pBuffer->mCurrentState & pTransBarrier->mNewState) && pBuffer->mCurrentState != pTransBarrier->mNewState)
			if (pBuffer->mCurrentState != pTransBarrier->mNewState)
			{
				if (pTransBarrier->mSplit)
				{
					ResourceState currentState = pBuffer->mCurrentState;
					// Determine if the barrier is begin only or end only
					// If the previous state and new state are same, we know this is end only since the state was already set in begin only
					if (pBuffer->mPreviousState & pTransBarrier->mNewState)
					{
						pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
						pBuffer->mPreviousState = RESOURCE_STATE_UNDEFINED;
						pBuffer->mCurrentState = pTransBarrier->mNewState;
					}
					else
					{
						pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
						pBuffer->mPreviousState = pTransBarrier->mNewState;
					}

					pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					pBarrier->Transition.pResource = pBuffer->pDxResource;
					pBarrier->Transition.StateBefore = util_to_dx_resource_state(currentState);
					pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);

					++transitionCount;
				}
				else
				{
					pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					pBarrier->Transition.pResource = pBuffer->pDxResource;
					pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					pBarrier->Transition.StateBefore = util_to_dx_resource_state(pBuffer->mCurrentState);
					pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);

					pBuffer->mCurrentState = pTransBarrier->mNewState;

					++transitionCount;
				}
			}
			else if (pBuffer->mCurrentState == RESOURCE_STATE_UNORDERED_ACCESS)
			{
				pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				pBarrier->UAV.pResource = pBuffer->pDxResource;
				++transitionCount;
			}
		}
	}
	for (uint32_t i = 0; i < numTextureBarriers; ++i)
	{
		TextureBarrier*         pTransBarrier = &pTextureBarriers[i];
		D3D12_RESOURCE_BARRIER* pBarrier = &barriers[transitionCount];
		Texture*                pTexture = pTransBarrier->pTexture;
		{
			if (pTexture->mCurrentState != pTransBarrier->mNewState)
			{
				if (pTransBarrier->mSplit)
				{
					ResourceState currentState = pTexture->mCurrentState;
					// Determine if the barrier is begin only or end only
					// If the previous state and new state are same, we know this is end only since the state was already set in begin only
					if (pTexture->mPreviousState & pTransBarrier->mNewState)
					{
						pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
						pTexture->mPreviousState = RESOURCE_STATE_UNDEFINED;
						pTexture->mCurrentState = pTransBarrier->mNewState;
					}
					else
					{
						pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
						pTexture->mPreviousState = pTransBarrier->mNewState;
					}

					pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					pBarrier->Transition.pResource = pTexture->pDxResource;
					pBarrier->Transition.StateBefore = util_to_dx_resource_state(currentState);
					pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);

					++transitionCount;
				}
				else
				{
					pBarrier->Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
					pBarrier->Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
					pBarrier->Transition.pResource = pTexture->pDxResource;
					pBarrier->Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
					pBarrier->Transition.StateBefore = util_to_dx_resource_state(pTexture->mCurrentState);
					pBarrier->Transition.StateAfter = util_to_dx_resource_state(pTransBarrier->mNewState);
					pTexture->mCurrentState = pTransBarrier->mNewState;

					++transitionCount;
				}
			}
		}
	}

	if (transitionCount)
		pCmd->pDxCmdList->ResourceBarrier(transitionCount, barriers);
}
#endif

void cmdUpdateBuffer(Cmd* pCmd, Buffer* pBuffer, uint64_t dstOffset, Buffer* pSrcBuffer, uint64_t srcOffset, uint64_t size)
{
	ASSERT(pCmd);
	ASSERT(pSrcBuffer);
	ASSERT(pSrcBuffer->pDxResource);
	ASSERT(pBuffer);
	ASSERT(pBuffer->pDxResource);

	pCmd->pDxCmdList->CopyBufferRegion(
		pBuffer->pDxResource, pBuffer->mPositionInHeap + dstOffset, pSrcBuffer->pDxResource, pSrcBuffer->mPositionInHeap + srcOffset, size);
}

#ifdef _DURANGO
void cmdUpdateBuffer(DmaCmd* pCmd, Buffer* pBuffer, uint64_t dstOffset, Buffer* pSrcBuffer, uint64_t srcOffset, uint64_t size)
{
	ASSERT(pCmd);
	ASSERT(pSrcBuffer);
	ASSERT(pSrcBuffer->pDxResource);
	ASSERT(pBuffer);
	ASSERT(pBuffer->pDxResource);

	pCmd->pDxCmdList->CopyBufferRegion(
		pBuffer->pDxResource, pBuffer->mPositionInHeap + dstOffset, pSrcBuffer->pDxResource, pSrcBuffer->mPositionInHeap + srcOffset, size);
}
#endif

static uint32_t updateSubresourceDimension(DXGI_FORMAT format, uint32_t dim)
{
	switch (format)
	{
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB: return (dim + 3) & ~3u;
		default: return dim;
	}
}

void cmdUpdateSubresource(Cmd* pCmd, Texture* pTexture, Buffer* pSrcBuffer, SubresourceDataDesc* pSubresourceDesc)
{
	D3D12_RESOURCE_DESC         Desc = pTexture->pDxResource->GetDesc();
	D3D12_TEXTURE_COPY_LOCATION Dst = {};
	Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Dst.pResource = pTexture->pDxResource;
	Dst.SubresourceIndex = pSubresourceDesc->mMipLevel + pSubresourceDesc->mArrayLayer * Desc.MipLevels;

	D3D12_TEXTURE_COPY_LOCATION Src = {};
	Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Src.pResource = pSrcBuffer->pDxResource;
	Src.PlacedFootprint =
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT{ pSrcBuffer->mPositionInHeap + pSubresourceDesc->mBufferOffset,
											{ Desc.Format, updateSubresourceDimension(Desc.Format, pSubresourceDesc->mRegion.mWidth),
											  updateSubresourceDimension(Desc.Format, pSubresourceDesc->mRegion.mHeight),
											  pSubresourceDesc->mRegion.mDepth, pSubresourceDesc->mRowPitch } };

	pCmd->pDxCmdList->CopyTextureRegion(
		&Dst, pSubresourceDesc->mRegion.mXOffset, pSubresourceDesc->mRegion.mYOffset, pSubresourceDesc->mRegion.mZOffset, &Src, NULL);
}

#ifdef _DURANGO
void cmdUpdateSubresource(DmaCmd* pCmd, Texture* pTexture, Buffer* pSrcBuffer, SubresourceDataDesc* pSubresourceDesc)
{
	D3D12_RESOURCE_DESC         Desc = pTexture->pDxResource->GetDesc();
	D3D12_TEXTURE_COPY_LOCATION Dst = {};
	Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	Dst.pResource = pTexture->pDxResource;
	Dst.SubresourceIndex = pSubresourceDesc->mMipLevel + pSubresourceDesc->mArrayLayer * Desc.MipLevels;

	D3D12_TEXTURE_COPY_LOCATION Src = {};
	Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	Src.pResource = pSrcBuffer->pDxResource;
	Src.PlacedFootprint =
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT{ pSrcBuffer->mPositionInHeap + pSubresourceDesc->mBufferOffset,
											{ Desc.Format, updateSubresourceDimension(Desc.Format, pSubresourceDesc->mRegion.mWidth),
											  updateSubresourceDimension(Desc.Format, pSubresourceDesc->mRegion.mHeight),
											  pSubresourceDesc->mRegion.mDepth, pSubresourceDesc->mRowPitch } };

	pCmd->pDxCmdList->CopyTextureRegion(
		&Dst, pSubresourceDesc->mRegion.mXOffset, pSubresourceDesc->mRegion.mYOffset, pSubresourceDesc->mRegion.mZOffset, &Src, NULL);
}
#endif

/************************************************************************/
// Queue Fence Semaphore Functions
/************************************************************************/
void acquireNextImage(
	Renderer* pRenderer, SwapChain* pSwapChain, Semaphore* pSignalSemaphore, Fence* pFence, uint32_t* pSwapChainImageIndex)
{
	UNREF_PARAM(pSignalSemaphore);
	UNREF_PARAM(pFence);
	ASSERT(pRenderer);

	//get latest backbuffer image
	ASSERT(pSwapChain->pDxSwapChain);
	ASSERT(pSwapChainImageIndex);

	*pSwapChainImageIndex = fnHookGetSwapChainImageIndex(pSwapChain);
	pRenderer->mCurrentFrameIdx = (pRenderer->mCurrentFrameIdx + 1) % pSwapChain->mDesc.mImageCount;
}

void queueSubmit(
	Queue* pQueue, uint32_t cmdCount, Cmd** ppCmds, Fence* pFence, uint32_t waitSemaphoreCount, Semaphore** ppWaitSemaphores,
	uint32_t signalSemaphoreCount, Semaphore** ppSignalSemaphores)
{
	//ASSERT that given cmd list and given params are valid
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

	//execute given command list
	ASSERT(pQueue->pDxQueue);

	cmdCount = cmdCount > MAX_SUBMIT_CMDS ? MAX_SUBMIT_CMDS : cmdCount;
	ID3D12CommandList** cmds = (ID3D12CommandList**)alloca(cmdCount * sizeof(ID3D12CommandList*));
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		cmds[i] = ppCmds[i]->pDxCmdList;
	}

	for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
		pQueue->pDxQueue->Wait(ppWaitSemaphores[i]->pFence->pDxFence, ppWaitSemaphores[i]->pFence->mFenceValue - 1);

	pQueue->pDxQueue->ExecuteCommandLists(cmdCount, cmds);

	if (pFence)
		pQueue->pDxQueue->Signal(pFence->pDxFence, pFence->mFenceValue++);

	for (uint32_t i = 0; i < signalSemaphoreCount; ++i)
		pQueue->pDxQueue->Signal(ppSignalSemaphores[i]->pFence->pDxFence, ppSignalSemaphores[i]->pFence->mFenceValue++);
}
#ifdef _DURANGO
void queueSubmit(
	Queue* pQueue, uint32_t cmdCount, DmaCmd** ppCmds, Fence* pFence, uint32_t waitSemaphoreCount, Semaphore** ppWaitSemaphores,
	uint32_t signalSemaphoreCount, Semaphore** ppSignalSemaphores)
{
	//ASSERT that given cmd list and given params are valid
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

	//execute given command list
	ASSERT(pQueue->pDxQueue);

	cmdCount = cmdCount > MAX_SUBMIT_CMDS ? MAX_SUBMIT_CMDS : cmdCount;
	ID3D12CommandList** cmds = (ID3D12CommandList**)alloca(cmdCount * sizeof(ID3D12CommandList*));
	for (uint32_t i = 0; i < cmdCount; ++i)
	{
		cmds[i] = ppCmds[i]->pDxCmdList;
	}

	for (uint32_t i = 0; i < waitSemaphoreCount; ++i)
		pQueue->pDxQueue->Wait(ppWaitSemaphores[i]->pFence->pDxFence, ppWaitSemaphores[i]->pFence->mFenceValue - 1);

	pQueue->pDxQueue->ExecuteCommandLists(cmdCount, cmds);

	if (pFence)
		pQueue->pDxQueue->Signal(pFence->pDxFence, pFence->mFenceValue++);

	for (uint32_t i = 0; i < signalSemaphoreCount; ++i)
		pQueue->pDxQueue->Signal(ppSignalSemaphores[i]->pFence->pDxFence, ppSignalSemaphores[i]->pFence->mFenceValue++);
}
#endif

void queuePresent(
	Queue* pQueue, SwapChain* pSwapChain, uint32_t swapChainImageIndex, uint32_t waitSemaphoreCount, Semaphore** ppWaitSemaphores)
{
	UNREF_PARAM(swapChainImageIndex);
	ASSERT(pQueue);
	ASSERT(pSwapChain->pDxSwapChain);

	if (waitSemaphoreCount > 0)
	{
		ASSERT(ppWaitSemaphores);
	}

#if defined(_DURANGO)

	if (pSwapChain->mDesc.mColorFormat == TinyImageFormat_R10G10B10A2_UNORM)
	{
		RECT presentRect;

		presentRect.top = 0;
		presentRect.left = 0;
		presentRect.bottom = pSwapChain->mDesc.mHeight;
		presentRect.right = pSwapChain->mDesc.mWidth;

		DXGIX_PRESENTARRAY_PARAMETERS presentParameterSets[1] = {};
		presentParameterSets[0].SourceRect = presentRect;
		presentParameterSets[0].ScaleFactorHorz = 1.0f;
		presentParameterSets[0].ScaleFactorVert = 1.0f;

		HRESULT hr = DXGIXPresentArray(
			pSwapChain->mDxSyncInterval, 0, pSwapChain->mFlags, _countof(presentParameterSets), &pSwapChain->pDxSwapChain,
			presentParameterSets);

		if (FAILED(hr))
		{
			hr = pQueue->pRenderer->pDxDevice->GetDeviceRemovedReason();
			if (FAILED(hr))
				ASSERT(false);    //TODO: let's do something with the error
		}
	}
	else
	{
		HRESULT hr = pSwapChain->pDxSwapChain->Present(pSwapChain->mDxSyncInterval, pSwapChain->mFlags);
		if (FAILED(hr))
		{
			hr = pQueue->pRenderer->pDxDevice->GetDeviceRemovedReason();
			if (FAILED(hr))
				ASSERT(false);    //TODO: let's do something with the error
		}
	}
#else
	HRESULT hr = pSwapChain->pDxSwapChain->Present(pSwapChain->mDxSyncInterval, pSwapChain->mFlags);
	if (FAILED(hr))
	{
		HRESULT removeHr = pQueue->pRenderer->pDxDevice->GetDeviceRemovedReason();
		if (FAILED(removeHr))
			ASSERT(false);    //TODO: let's do something with the error
	}
#endif
}

bool queueSignal(Queue* pQueue, Fence* fence, uint64_t value)
{
	ASSERT(pQueue);
	ASSERT(fence);

	HRESULT hres = pQueue->pDxQueue->Signal(fence->pDxFence, value);

	return SUCCEEDED(hres);
}

void waitForFences(Renderer* pRenderer, uint32_t fenceCount, Fence** ppFences)
{
	// Wait for fence completion
	for (uint32_t i = 0; i < fenceCount; ++i)
	{
		FenceStatus fenceStatus;
		::getFenceStatus(pRenderer, ppFences[i], &fenceStatus);
		uint64_t fenceValue = ppFences[i]->mFenceValue - 1;
		//if (completedValue < fenceValue)
		if (fenceStatus == FENCE_STATUS_INCOMPLETE)
		{
			ppFences[i]->pDxFence->SetEventOnCompletion(fenceValue, ppFences[i]->pDxWaitIdleFenceEvent);
			WaitForSingleObject(ppFences[i]->pDxWaitIdleFenceEvent, INFINITE);
		}
	}
}

void waitQueueIdle(Queue* pQueue)
{
	FenceStatus fenceStatus;
	pQueue->pDxQueue->Signal(pQueue->pQueueFence->pDxFence, pQueue->pQueueFence->mFenceValue++);
	::getFenceStatus(pQueue->pRenderer, pQueue->pQueueFence, &fenceStatus);
	uint64_t fenceValue = pQueue->pQueueFence->mFenceValue - 1;
	if (fenceStatus == FENCE_STATUS_INCOMPLETE)
	{
		pQueue->pQueueFence->pDxFence->SetEventOnCompletion(fenceValue, pQueue->pQueueFence->pDxWaitIdleFenceEvent);
		WaitForSingleObject(pQueue->pQueueFence->pDxWaitIdleFenceEvent, INFINITE);
	}
}

void getFenceStatus(Renderer* pRenderer, Fence* pFence, FenceStatus* pFenceStatus)
{
	UNREF_PARAM(pRenderer);

	if (pFence->pDxFence->GetCompletedValue() < pFence->mFenceValue - 1)
		*pFenceStatus = FENCE_STATUS_INCOMPLETE;
	else
		*pFenceStatus = FENCE_STATUS_COMPLETE;
}

bool fenceSetEventOnCompletion(Fence* fence, uint64_t value, HANDLE fenceEvent)
{
	ASSERT(fence);
	HRESULT hres = fence->pDxFence->SetEventOnCompletion(value, fenceEvent);
	return SUCCEEDED(hres);
}

/************************************************************************/
// Utility functions
/************************************************************************/
TinyImageFormat getRecommendedSwapchainFormat(bool hintHDR)
{
	if (fnHookGetRecommendedSwapChainFormat)
		return fnHookGetRecommendedSwapChainFormat(hintHDR);
	else
		return TinyImageFormat_B8G8R8A8_UNORM;
}

/************************************************************************/
// Execute Indirect Implementation
/************************************************************************/
D3D12_INDIRECT_ARGUMENT_TYPE util_to_dx_indirect_argument_type(IndirectArgumentType argType)
{
	D3D12_INDIRECT_ARGUMENT_TYPE res = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	switch (argType)
	{
		case INDIRECT_DRAW: res = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW; break;
		case INDIRECT_DRAW_INDEX: res = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED; break;
		case INDIRECT_DISPATCH: res = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH; break;
		case INDIRECT_VERTEX_BUFFER: res = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW; break;
		case INDIRECT_INDEX_BUFFER: res = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW; break;
		case INDIRECT_CONSTANT: res = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT; break;
		case INDIRECT_CONSTANT_BUFFER_VIEW: res = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW; break;
		case INDIRECT_SHADER_RESOURCE_VIEW: res = D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW; break;
		case INDIRECT_UNORDERED_ACCESS_VIEW: res = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW; break;
		case INDIRECT_DESCRIPTOR_TABLE: LOGF(LogLevel::eERROR, "Dx12 Doesn't support DescriptorTable in Indirect Command"); break;
		case INDIRECT_PIPELINE: LOGF(LogLevel::eERROR, "Dx12 Doesn't support the Pipeline in Indirect Command"); break;
	}
	return res;
}

void addIndirectCommandSignature(Renderer* pRenderer, const CommandSignatureDesc* pDesc, CommandSignature** ppCommandSignature)
{
	ASSERT(pRenderer);
	ASSERT(pDesc);
	ASSERT(pDesc->pArgDescs);

	CommandSignature* pCommandSignature = (CommandSignature*)conf_calloc(1, sizeof(*pCommandSignature));
	pCommandSignature->mDesc = *pDesc;

	bool change = false;
	// calculate size through arguement types
	uint32_t commandStride = 0;

	// temporary use
	D3D12_INDIRECT_ARGUMENT_DESC* argumentDescs =
		(D3D12_INDIRECT_ARGUMENT_DESC*)alloca(pDesc->mIndirectArgCount * sizeof(D3D12_INDIRECT_ARGUMENT_DESC));

	for (uint32_t i = 0; i < pDesc->mIndirectArgCount; ++i)
	{
		if (pDesc->pArgDescs[i].mType == INDIRECT_DESCRIPTOR_TABLE || pDesc->pArgDescs[i].mType == INDIRECT_PIPELINE)
		{
			LOGF(LogLevel::eERROR, "Dx12 Doesn't support DescriptorTable or Pipeline in Indirect Command");
		}

		argumentDescs[i].Type = util_to_dx_indirect_argument_type(pDesc->pArgDescs[i].mType);
		uint32_t rootParameterIndex = 0;
		rootParameterIndex = (pDesc->pArgDescs[i].pName) ?
			get_descriptor(pDesc->pRootSignature, pDesc->pArgDescs[i].pName)->mIndexInParent :
			pDesc->pArgDescs[i].mIndex;

		switch (argumentDescs[i].Type)
		{
			case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
				argumentDescs[i].Constant.RootParameterIndex = rootParameterIndex;
				argumentDescs[i].Constant.DestOffsetIn32BitValues = 0;
				argumentDescs[i].Constant.Num32BitValuesToSet = pDesc->pArgDescs[i].mCount;
				commandStride += sizeof(UINT) * argumentDescs[i].Constant.Num32BitValuesToSet;
				change = true;
				break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
				argumentDescs[i].UnorderedAccessView.RootParameterIndex = rootParameterIndex;
				commandStride += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
				change = true;
				break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
				argumentDescs[i].ShaderResourceView.RootParameterIndex = rootParameterIndex;
				commandStride += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
				change = true;
				break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
				argumentDescs[i].ConstantBufferView.RootParameterIndex = rootParameterIndex;
				commandStride += sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
				change = true;
				break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
				argumentDescs[i].VertexBuffer.Slot = rootParameterIndex;
				commandStride += sizeof(D3D12_VERTEX_BUFFER_VIEW);
				change = true;
				break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
				argumentDescs[i].VertexBuffer.Slot = rootParameterIndex;
				commandStride += sizeof(D3D12_INDEX_BUFFER_VIEW);
				change = true;
				break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW: commandStride += sizeof(IndirectDrawArguments); break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED: commandStride += sizeof(IndirectDrawIndexArguments); break;
			case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH: commandStride += sizeof(IndirectDispatchArguments); break;
			default: ASSERT(false); break;
		}
	}

	if (change)
	{
		ASSERT(pDesc->pRootSignature);
	}

	commandStride = round_up(commandStride, 16);

	pCommandSignature->mDrawCommandStride = commandStride;
	pCommandSignature->mIndirectArgDescCounts = pDesc->mIndirectArgCount;

	D3D12_COMMAND_SIGNATURE_DESC commandSignatureDesc = {};
	commandSignatureDesc.pArgumentDescs = argumentDescs;
	commandSignatureDesc.NumArgumentDescs = pDesc->mIndirectArgCount;
	commandSignatureDesc.ByteStride = commandStride;
	// If running Linked Mode (SLI) create command signature for all nodes
	// #NOTE : In non SLI mode, mNodeCount will be 0 which sets nodeMask to default value
	commandSignatureDesc.NodeMask = util_calculate_shared_node_mask(pRenderer);
	HRESULT hres = pRenderer->pDxDevice->CreateCommandSignature(
		&commandSignatureDesc, change ? pDesc->pRootSignature->pDxRootSignature : NULL, IID_ARGS(&pCommandSignature->pDxCommandSignautre));
	ASSERT(SUCCEEDED(hres));

	*ppCommandSignature = pCommandSignature;
}

void removeIndirectCommandSignature(Renderer* pRenderer, CommandSignature* pCommandSignature)
{
	UNREF_PARAM(pRenderer);
	SAFE_RELEASE(pCommandSignature->pDxCommandSignautre);
	SAFE_FREE(pCommandSignature);
}

void cmdExecuteIndirect(
	Cmd* pCmd, CommandSignature* pCommandSignature, uint maxCommandCount, Buffer* pIndirectBuffer, uint64_t bufferOffset,
	Buffer* pCounterBuffer, uint64_t counterBufferOffset)
{
	ASSERT(pCommandSignature);
	ASSERT(pIndirectBuffer);

	if (!pCounterBuffer)
		pCmd->pDxCmdList->ExecuteIndirect(
			pCommandSignature->pDxCommandSignautre, maxCommandCount, pIndirectBuffer->pDxResource, bufferOffset, NULL, 0);
	else
		pCmd->pDxCmdList->ExecuteIndirect(
			pCommandSignature->pDxCommandSignautre, maxCommandCount, pIndirectBuffer->pDxResource, bufferOffset,
			pCounterBuffer->pDxResource, counterBufferOffset);
}
/************************************************************************/
// Query Heap Implementation
/************************************************************************/
void getTimestampFrequency(Queue* pQueue, double* pFrequency)
{
	ASSERT(pQueue);
	ASSERT(pFrequency);

	UINT64 freq = 0;
	pQueue->pDxQueue->GetTimestampFrequency(&freq);
	*pFrequency = (double)freq;
}

void addQueryPool(Renderer* pRenderer, const QueryPoolDesc* pDesc, QueryPool** ppQueryPool)
{
	QueryPool* pQueryPool = (QueryPool*)conf_calloc(1, sizeof(*pQueryPool));
	pQueryPool->mDesc = *pDesc;

	D3D12_QUERY_HEAP_DESC desc = {};
	desc.Count = pDesc->mQueryCount;
	desc.NodeMask = util_calculate_node_mask(pRenderer, pDesc->mNodeIndex);
	desc.Type = util_to_dx_query_heap_type(pDesc->mType);
	pRenderer->pDxDevice->CreateQueryHeap(&desc, IID_ARGS(&pQueryPool->pDxQueryHeap));

	*ppQueryPool = pQueryPool;
}

void removeQueryPool(Renderer* pRenderer, QueryPool* pQueryPool)
{
	UNREF_PARAM(pRenderer);
	SAFE_RELEASE(pQueryPool->pDxQueryHeap);
	SAFE_FREE(pQueryPool);
}

void cmdResetQueryPool(Cmd* pCmd, QueryPool* pQueryPool, uint32_t startQuery, uint32_t queryCount)
{
	UNREF_PARAM(pCmd);
	UNREF_PARAM(pQueryPool);
	UNREF_PARAM(startQuery);
	UNREF_PARAM(queryCount);
}

void cmdBeginQuery(Cmd* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	D3D12_QUERY_TYPE type = util_to_dx_query_type(pQueryPool->mDesc.mType);
	switch (type)
	{
		case D3D12_QUERY_TYPE_OCCLUSION: break;
		case D3D12_QUERY_TYPE_BINARY_OCCLUSION: break;
		case D3D12_QUERY_TYPE_TIMESTAMP: pCmd->pDxCmdList->EndQuery(pQueryPool->pDxQueryHeap, type, pQuery->mIndex); break;
		case D3D12_QUERY_TYPE_PIPELINE_STATISTICS: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3: break;
		default: break;
	}
}

void cmdEndQuery(Cmd* pCmd, QueryPool* pQueryPool, QueryDesc* pQuery)
{
	D3D12_QUERY_TYPE type = util_to_dx_query_type(pQueryPool->mDesc.mType);
	switch (type)
	{
		case D3D12_QUERY_TYPE_OCCLUSION: break;
		case D3D12_QUERY_TYPE_BINARY_OCCLUSION: break;
		case D3D12_QUERY_TYPE_TIMESTAMP: pCmd->pDxCmdList->EndQuery(pQueryPool->pDxQueryHeap, type, pQuery->mIndex); break;
		case D3D12_QUERY_TYPE_PIPELINE_STATISTICS: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM1: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM2: break;
		case D3D12_QUERY_TYPE_SO_STATISTICS_STREAM3: break;
		default: break;
	}
}

void cmdResolveQuery(Cmd* pCmd, QueryPool* pQueryPool, Buffer* pReadbackBuffer, uint32_t startQuery, uint32_t queryCount)
{
	pCmd->pDxCmdList->ResolveQueryData(
		pQueryPool->pDxQueryHeap, util_to_dx_query_type(pQueryPool->mDesc.mType), startQuery, queryCount, pReadbackBuffer->pDxResource,
		startQuery * 8);
}
/************************************************************************/
// Memory Stats Implementation
/************************************************************************/
void calculateMemoryStats(Renderer* pRenderer, char** stats) { resourceAllocBuildStatsString(pRenderer->pResourceAllocator, stats, 0); }

void freeMemoryStats(Renderer* pRenderer, char* stats) { resourceAllocFreeStatsString(pRenderer->pResourceAllocator, stats); }
/************************************************************************/
// Debug Marker Implementation
/************************************************************************/
void cmdBeginDebugMarker(Cmd* pCmd, float r, float g, float b, const char* pName)
{
	// note: USE_PIX isn't the ideal test because we might be doing a debug build where pix
	// is not installed, or a variety of other reasons. It should be a separate #ifdef flag?
#if defined(USE_PIX)
	//color is in B8G8R8X8 format where X is padding
	uint64_t color = packColorF32(r, g, b, 0 /*there is no alpha, that's padding*/);
	PIXBeginEvent(pCmd->pDxCmdList, color, pName);
#endif
}

void cmdEndDebugMarker(Cmd* pCmd)
{
#ifndef FORGE_JHABLE_EDITS_V01
#if defined(USE_PIX)
	PIXEndEvent(pCmd->pDxCmdList);
#endif
#endif
}

void cmdAddDebugMarker(Cmd* pCmd, float r, float g, float b, const char* pName)
{
#if defined(USE_PIX)
	//color is in B8G8R8X8 format where X is padding
	uint64_t color = packColorF32(r, g, b, 0 /*there is no alpha, that's padding*/);
	PIXSetMarker(pCmd->pDxCmdList, color, pName);
#endif
}
/************************************************************************/
// Resource Debug Naming Interface
/************************************************************************/
void setBufferName(Renderer* pRenderer, Buffer* pBuffer, const char* pName)
{
	ASSERT(pRenderer);
	ASSERT(pBuffer);
	ASSERT(pName);

	size_t length = strlen(pName);

	ASSERT(length < MAX_PATH && "Name too long");

	wchar_t wName[MAX_PATH] = {};
	wName[strlen(pName)] = '\0';
	size_t numConverted = 0;
	mbstowcs_s(&numConverted, wName, pName, strlen(pName));
	pBuffer->pDxResource->SetName(wName);
}

void setTextureName(Renderer* pRenderer, Texture* pTexture, const char* pName)
{
	ASSERT(pRenderer);
	ASSERT(pTexture);
	ASSERT(pName);

	size_t length = strlen(pName);

	ASSERT(length < MAX_PATH && "Name too long");

	wchar_t wName[MAX_PATH] = {};
	wName[strlen(pName)] = '\0';
	size_t numConverted = 0;
	mbstowcs_s(&numConverted, wName, pName, strlen(pName));
	pTexture->pDxResource->SetName(wName);
}

/************************************************************************/
// Virtual Texture
/************************************************************************/
uvec3 alignedDivision(const D3D12_TILED_RESOURCE_COORDINATE& extent, const D3D12_TILED_RESOURCE_COORDINATE& granularity)
{
	uvec3 res;
	res.setX(extent.X / granularity.X + ((extent.X  % granularity.X) ? 1u : 0u));
	res.setY(extent.Y / granularity.Y + ((extent.Y % granularity.Y) ? 1u : 0u));
	res.setZ(extent.Z / granularity.Z + ((extent.Z  % granularity.Z) ? 1u : 0u));
	return res;
}

VirtualTexturePage* addPage(Renderer* pRenderer, Texture* pTexture, D3D12_TILED_RESOURCE_COORDINATE offset, D3D12_TILED_RESOURCE_COORDINATE extent,
														const uint32_t size, const uint32_t mipLevel, uint32_t layer)
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

void releasePage(Cmd* pCmd, Renderer* pRenderer, Texture* pTexture)
{
	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;

	uint removePageCount;

	bool map = !pTexture->mRemovePageCount->pCpuMappedAddress;
	if (map)
	{
		mapBuffer(pRenderer, pTexture->mRemovePageCount, NULL);
	}
	memcpy(&removePageCount, pTexture->mRemovePageCount->pCpuMappedAddress, sizeof(uint));
	if (map)
	{
		unmapBuffer(pRenderer, pTexture->mRemovePageCount);
	}

	if (removePageCount == 0)
		return;

	eastl::vector<uint32_t> RemovePageTable;
	RemovePageTable.resize(removePageCount);

	map = !pTexture->mRemovePage->pCpuMappedAddress;
	if (map)
	{
		mapBuffer(pRenderer, pTexture->mRemovePage, NULL);
	}
	memcpy(RemovePageTable.data(), pTexture->mRemovePage->pCpuMappedAddress, sizeof(uint));
	if (map)
	{
		unmapBuffer(pRenderer, pTexture->mRemovePage);
	}	

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
	eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>* pSparseCoordinates = (eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>*)pTexture->pSparseCoordinates;
	eastl::vector<uint32_t>* pHeapRangeStartOffsets = (eastl::vector<uint32_t>*)pTexture->pHeapRangeStartOffsets;

	eastl::vector<uint32_t> tileCounts;
	eastl::vector<D3D12_TILE_REGION_SIZE> regionSizes;
	eastl::vector<D3D12_TILE_RANGE_FLAGS> rangeFlags;
	//eastl::vector<uint32_t> rangeStartOffsets;

	pSparseCoordinates->set_capacity(0);
	pHeapRangeStartOffsets->set_capacity(0);

	uint alivePageCount;

	bool map = !pTexture->mAlivePageCount->pCpuMappedAddress;
	if (map)
	{
		mapBuffer(pRenderer, pTexture->mAlivePageCount, NULL);
	}

	memcpy(&alivePageCount, pTexture->mAlivePageCount->pCpuMappedAddress, sizeof(uint));

	if (map)
	{
		unmapBuffer(pRenderer, pTexture->mAlivePageCount);
	}

	map = !pTexture->mAlivePage->pCpuMappedAddress;
	if (map)
	{
		mapBuffer(pRenderer, pTexture->mAlivePage, NULL);
	}

	eastl::vector<uint> VisibilityData;
	VisibilityData.resize(alivePageCount);
	memcpy(VisibilityData.data(), pTexture->mAlivePage->pCpuMappedAddress, VisibilityData.size() * sizeof(uint));

	if (map)
	{
		unmapBuffer(pRenderer, pTexture->mAlivePage);
	}

	D3D12_TILE_REGION_SIZE regionSize = { 1, true, 1, 1, 1 };
	D3D12_TILE_RANGE_FLAGS rangeFlag = D3D12_TILE_RANGE_FLAG_NONE;

	for (int i = 0; i < (int)VisibilityData.size(); ++i)
	{
		uint pageIndex = VisibilityData[i];
		VirtualTexturePage* pPage = &(*pPageTable)[pageIndex];

		if (allocateVirtualPage(pRenderer, pTexture, *pPage))
		{
			void* pData = (void*)((unsigned char*)pTexture->mVirtualImageData + (pageIndex * pPage->size));

			map = !pPage->pIntermediateBuffer->pCpuMappedAddress;
			if (map)
			{
				mapBuffer(pRenderer, pPage->pIntermediateBuffer, NULL);
			}

			memcpy(pPage->pIntermediateBuffer->pCpuMappedAddress, pData, pPage->size);

			
			D3D12_TILED_RESOURCE_COORDINATE startCoord;
			startCoord.X = pPage->offset.X / (uint)pTexture->mSparseVirtualTexturePageWidth;
			startCoord.Y = pPage->offset.Y / (uint)pTexture->mSparseVirtualTexturePageHeight;
			startCoord.Z = pPage->offset.Z;
			startCoord.Subresource = pPage->offset.Subresource;

			pSparseCoordinates->push_back(startCoord);
			tileCounts.push_back(1);
			regionSizes.push_back(regionSize);
			rangeFlags.push_back(rangeFlag);
			pHeapRangeStartOffsets->push_back(pageIndex);

			D3D12_RESOURCE_DESC         Desc = pTexture->pDxResource->GetDesc();
			D3D12_TEXTURE_COPY_LOCATION Dst = {};
			Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			Dst.pResource = pTexture->pDxResource;
			Dst.SubresourceIndex = startCoord.Subresource;

			D3D12_TEXTURE_COPY_LOCATION Src = {};
			Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			Src.pResource = pPage->pIntermediateBuffer->pDxResource;
			Src.PlacedFootprint =
				D3D12_PLACED_SUBRESOURCE_FOOTPRINT{ 0,
									{ Desc.Format,
										(UINT)pTexture->mSparseVirtualTexturePageWidth, (UINT)pTexture->mSparseVirtualTexturePageHeight, 1, (UINT)pTexture->mSparseVirtualTexturePageWidth * sizeof(uint32_t) } };

			pCmd->pDxCmdList->CopyTextureRegion(&Dst, startCoord.X * (UINT)pTexture->mSparseVirtualTexturePageWidth, startCoord.Y * (UINT)pTexture->mSparseVirtualTexturePageHeight, 0, &Src, NULL);

			if (map)
			{
				unmapBuffer(pRenderer, pPage->pIntermediateBuffer);
			}
		}
	}

	// Update sparse bind info
	if (pSparseCoordinates->size() > 0)
	{
		pCmd->pCmdPool->pQueue->pDxQueue->UpdateTileMappings(pTexture->pDxResource,
			(UINT)pSparseCoordinates->size(),
			pSparseCoordinates->data(),
			regionSizes.data(),
			pTexture->pSparseImageMemory,
			(UINT)pSparseCoordinates->size(),
			rangeFlags.data(),
			pHeapRangeStartOffsets->data(),
			tileCounts.data(),
			D3D12_TILE_MAPPING_FLAG_NONE);
	}
	
	regionSizes.set_capacity(0);	
	rangeFlags.set_capacity(0);
	tileCounts.set_capacity(0);
}

// Fill smallest (non-tail) mip map level
void fillVirtualTextureLevel(Cmd* pCmd, Renderer* pRenderer, Texture* pTexture, uint32_t mipLevel)
{
	TextureBarrier barriers[] = {
				{ pTexture, RESOURCE_STATE_COPY_DEST }
	};
	cmdResourceBarrier(pCmd, 0, NULL, 1, barriers);

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;
	eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>* pSparseCoordinates = (eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>*)pTexture->pSparseCoordinates;

	eastl::vector<uint32_t> tileCounts;
	eastl::vector<D3D12_TILE_REGION_SIZE> regionSizes;
	eastl::vector<D3D12_TILE_RANGE_FLAGS> rangeFlags;
	eastl::vector<uint32_t> rangeStartOffsets;

	pSparseCoordinates->set_capacity(0);

	D3D12_TILE_REGION_SIZE regionSize = { 1, true, 1, 1, 1 };
	D3D12_TILE_RANGE_FLAGS rangeFlag = D3D12_TILE_RANGE_FLAG_NONE;

	for (int i = 0; i < (int)pPageTable->size(); i++)
	{
		VirtualTexturePage* pPage = &(*pPageTable)[i];
		uint32_t pageIndex = pPage->index;
		int globalOffset = 0;

		if ((pPage->mipLevel == mipLevel) && (pPage->pIntermediateBuffer == NULL))
		{
			if (allocateVirtualPage(pRenderer, pTexture, *pPage))
			{
				void* pData = (void*)((unsigned char*)pTexture->mVirtualImageData + (pageIndex * (uint32_t)pPage->size));

				//CPU to GPU
				bool map = !pPage->pIntermediateBuffer->pCpuMappedAddress;
				if (map)
				{
					mapBuffer(pRenderer, pPage->pIntermediateBuffer, NULL);
				}

				memcpy(pPage->pIntermediateBuffer->pCpuMappedAddress, pData, pPage->size);				

				D3D12_TILED_RESOURCE_COORDINATE startCoord;
				startCoord.X = pPage->offset.X / (uint)pTexture->mSparseVirtualTexturePageWidth;
				startCoord.Y = pPage->offset.Y / (uint)pTexture->mSparseVirtualTexturePageHeight;
				startCoord.Z = pPage->offset.Z;
				startCoord.Subresource = pPage->offset.Subresource;

				pSparseCoordinates->push_back(startCoord);
				tileCounts.push_back(1);
				regionSizes.push_back(regionSize);
				rangeFlags.push_back(rangeFlag);
				rangeStartOffsets.push_back(pageIndex);

				D3D12_RESOURCE_DESC         Desc = pTexture->pDxResource->GetDesc();
				D3D12_TEXTURE_COPY_LOCATION Dst = {};
				Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				Dst.pResource = pTexture->pDxResource;
				Dst.SubresourceIndex = mipLevel;


				D3D12_TEXTURE_COPY_LOCATION Src = {};
				Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				Src.pResource = pPage->pIntermediateBuffer->pDxResource;
				Src.PlacedFootprint =
					D3D12_PLACED_SUBRESOURCE_FOOTPRINT{ 0,
										{ Desc.Format,
											(UINT)pTexture->mSparseVirtualTexturePageWidth, (UINT)pTexture->mSparseVirtualTexturePageHeight, 1, (UINT)pTexture->mSparseVirtualTexturePageWidth * sizeof(uint32_t) } };

				pCmd->pDxCmdList->CopyTextureRegion(
					&Dst, startCoord.X * (UINT)pTexture->mSparseVirtualTexturePageWidth, startCoord.Y * (UINT)pTexture->mSparseVirtualTexturePageHeight, 0, &Src, NULL);

				if (map)
				{
					unmapBuffer(pRenderer, pPage->pIntermediateBuffer);
				}
			}
		}
	}

	// Update sparse bind info
	if (pSparseCoordinates->size() > 0)
	{
		pCmd->pCmdPool->pQueue->pDxQueue->UpdateTileMappings(
			pTexture->pDxResource,
			(UINT)pSparseCoordinates->size(),
			pSparseCoordinates->data(),
			regionSizes.data(),
			pTexture->pSparseImageMemory,
			(UINT)pSparseCoordinates->size(),
			rangeFlags.data(),
			rangeStartOffsets.data(),
			tileCounts.data(),
			D3D12_TILE_MAPPING_FLAG_NONE);
	}

	regionSizes.set_capacity(0);
	rangeFlags.set_capacity(0);
	rangeStartOffsets.set_capacity(0);
	tileCounts.set_capacity(0);
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

	//set texture properties
	pTexture->mDesc = *pDesc;

	//add to gpu
	D3D12_RESOURCE_DESC desc = {};
	DXGI_FORMAT         dxFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DescriptorType      descriptors = pDesc->mDescriptors;

	ASSERT(DXGI_FORMAT_UNKNOWN != dxFormat);

	desc.Width = pDesc->mWidth;
	desc.Height = pDesc->mHeight;
	desc.MipLevels = pDesc->mMipLevels;
	desc.Format = dxFormat;
	desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	desc.DepthOrArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

	D3D12_RESOURCE_STATES res_states = util_to_dx_resource_state(pDesc->mStartState);

	HRESULT hres = pRenderer->pDxDevice->CreateReservedResource(&desc, res_states, NULL, IID_ARGS(&pTexture->pDxResource));
	ASSERT(SUCCEEDED(hres));

	pTexture->mCurrentState = RESOURCE_STATE_COPY_DEST;

	D3D12_SHADER_RESOURCE_VIEW_DESC  srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = dxFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	add_srv(pRenderer, pTexture->pDxResource, &srvDesc, &pTexture->mDxSRVDescriptor);

	UINT numTiles = 0;
	D3D12_PACKED_MIP_INFO packedMipInfo;
	D3D12_TILE_SHAPE tileShape = {};
	UINT subresourceCount = pDesc->mMipLevels;
	eastl::vector<D3D12_SUBRESOURCE_TILING> tilings(subresourceCount);
	pRenderer->pDxDevice->GetResourceTiling(pTexture->pDxResource, &numTiles, &packedMipInfo, &tileShape, &subresourceCount, 0, &tilings[0]);

	pTexture->mSparseVirtualTexturePageWidth = tileShape.WidthInTexels;
	pTexture->mSparseVirtualTexturePageHeight = tileShape.HeightInTexels;

	pTexture->pPages = (eastl::vector<VirtualTexturePage>*)conf_calloc(1, sizeof(eastl::vector<VirtualTexturePage>));
	pTexture->pSparseCoordinates = (eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>*)conf_calloc(1, sizeof(eastl::vector<D3D12_TILED_RESOURCE_COORDINATE>));
	pTexture->pHeapRangeStartOffsets = (eastl::vector<uint32_t>*)conf_calloc(1, sizeof(eastl::vector<uint32_t>));

	// Sparse bindings for each mip level of all layers outside of the mip tail
	for (uint32_t layer = 0; layer < 1; layer++)
	{
		// sparseMemoryReq.imageMipTailFirstLod is the first mip level that's stored inside the mip tail
		for (uint32_t mipLevel = 0; mipLevel < packedMipInfo.NumStandardMips; mipLevel++)
		{
			D3D12_TILED_RESOURCE_COORDINATE extent;
			extent.X = max(pDesc->mWidth >> mipLevel, 1u);
			extent.Y = max(pDesc->mHeight >> mipLevel, 1u);
			extent.Z = max(pDesc->mDepth >> mipLevel, 1u);

			// Aligned sizes by image granularity
			D3D12_TILED_RESOURCE_COORDINATE imageGranularity;
			imageGranularity.X = tileShape.WidthInTexels;
			imageGranularity.Y = tileShape.HeightInTexels;
			imageGranularity.Z = tileShape.DepthInTexels;

			uvec3 sparseBindCounts = alignedDivision(extent, imageGranularity);
			uvec3 lastBlockExtent;
			lastBlockExtent.setX((extent.X % imageGranularity.X) ? extent.X % imageGranularity.X : imageGranularity.X);
			lastBlockExtent.setY((extent.Y % imageGranularity.Y) ? extent.Y % imageGranularity.Y : imageGranularity.Y);
			lastBlockExtent.setZ((extent.Z % imageGranularity.Z) ? extent.Z % imageGranularity.Z : imageGranularity.Z);

			// Alllocate memory for some blocks
			for (uint32_t z = 0; z < sparseBindCounts.getZ(); z++)
			{
				for (uint32_t y = 0; y < sparseBindCounts.getY(); y++)
				{
					for (uint32_t x = 0; x < sparseBindCounts.getX(); x++)
					{
						// Offset 
						D3D12_TILED_RESOURCE_COORDINATE offset;
						offset.X = x * imageGranularity.X;
						offset.Y = y * imageGranularity.Y;
						offset.Z = z * imageGranularity.Z;
						offset.Subresource = mipLevel;
						// Size of the page
						D3D12_TILED_RESOURCE_COORDINATE extent;
						extent.X = (x == sparseBindCounts.getX() - 1) ? lastBlockExtent.getX() : imageGranularity.X;
						extent.Y = (y == sparseBindCounts.getY() - 1) ? lastBlockExtent.getY() : imageGranularity.Y;
						extent.Z = (z == sparseBindCounts.getZ() - 1) ? lastBlockExtent.getZ() : imageGranularity.Z;
						extent.Subresource = mipLevel;

						// Add new virtual page
						VirtualTexturePage *newPage = addPage(pRenderer, pTexture, offset, extent, (uint32_t)pTexture->mSparseVirtualTexturePageWidth * (uint32_t)pTexture->mSparseVirtualTexturePageHeight * sizeof(uint), mipLevel, layer);

					}
				}
			}
		}
	}

	eastl::vector<VirtualTexturePage>* pPageTable = (eastl::vector<VirtualTexturePage>*)pTexture->pPages;

	// Create Memeory
	UINT heapSize = UINT(pTexture->mSparseVirtualTexturePageWidth * pTexture->mSparseVirtualTexturePageHeight * sizeof(uint32_t) * (uint32_t)pPageTable->size());

	D3D12_HEAP_DESC desc_heap = {};
	desc_heap.Alignment = 0;
	desc_heap.Flags = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
	desc_heap.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
	desc_heap.SizeInBytes = heapSize;

	hres = pRenderer->pDxDevice->CreateHeap(&desc_heap, __uuidof(pTexture->pSparseImageMemory), (void**)&pTexture->pSparseImageMemory);
	ASSERT(SUCCEEDED(hres));

	LOGF(LogLevel::eINFO, "Virtual Texture info: Dim %d x %d Pages %d", pTexture->mDesc.mWidth, pTexture->mDesc.mHeight, (uint32_t)(((eastl::vector<VirtualTexturePage>*)pTexture->pPages)->size()));

	fillVirtualTextureLevel(cmd, pRenderer, pTexture, packedMipInfo.NumStandardMips - 1);

	endCmd(cmd);

	queueSubmit(graphicsQueue, 1, &cmd, NULL, 0, NULL, 0, NULL);
	waitQueueIdle(graphicsQueue);

	// Delete command buffer
	removeCmd(cmdPool, cmd);
	removeCmdPool(pRenderer, cmdPool);
	removeQueue(graphicsQueue);

	////save tetxure in given pointer
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

		bool map = !pTexture->mAlivePageCount->pCpuMappedAddress;
		if (map)
		{
			mapBuffer(pRenderer, pTexture->mAlivePageCount, NULL);
		}
		memcpy(pTexture->mAlivePageCount->pCpuMappedAddress, alivePageCount, sizeof(uint) * 4);
		if (map)
		{
			unmapBuffer(pRenderer, pTexture->mAlivePageCount);
		}

		map = !pTexture->mRemovePageCount->pCpuMappedAddress;
		if (map)
		{
			mapBuffer(pRenderer, pTexture->mRemovePageCount, NULL);
		}
		memcpy(pTexture->mRemovePageCount->pCpuMappedAddress, alivePageCount, sizeof(uint) * 4);
		if (map)
		{
			unmapBuffer(pRenderer, pTexture->mRemovePageCount);
		}
	}
}
#endif
#endif
