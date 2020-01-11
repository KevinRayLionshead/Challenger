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

#ifndef IMAGE_CLASS_ALLOWED
static_assert(false, "Image.h can only be included by ResourceLoader.cpp and Image.cpp");
#endif

#ifndef COMMON_3_OS_IMAGE_IMAGE_H_
#define COMMON_3_OS_IMAGE_IMAGE_H_

#include "../../ThirdParty/OpenSource/tinyimageformat/tinyimageformat_base.h"
#include "../../ThirdParty/OpenSource/tinyimageformat/tinyimageformat_query.h"
#include "../Interfaces/IFileSystem.h"
#include "../../ThirdParty/OpenSource/EASTL/string.h"

#ifndef IMAGE_DISABLE_GOOGLE_BASIS
//Google basis Transcoder
#include "../../ThirdParty/OpenSource/basis_universal/transcoder/basisu_transcoder.h"
#endif
#define ALL_MIPLEVELS 127

/************************************************************************************/
// Define some useful macros
#define MCHAR2(a, b) (a | (b << 8))
#define MAKE_CHAR4(a, b, c, d) (a | (b << 8) | (c << 16) | (d << 24))

/*************************************************************************************/

typedef void* (*memoryAllocationFunc)(class Image* pImage, uint64_t memoryRequirement, void* pUserData);

class Image
{
private:
	Image();
	Image(const Image& img);
	void Destroy();

	friend class ResourceLoader;
	friend class AssetPipeline;
	friend bool convertAndSaveImage(const Image& image, bool (Image::*saverFunction)(const Path*), const Path* filePath);
	friend Image* conf_placement_new<Image>(void* ptr);

	unsigned char* Create(const TinyImageFormat fmt, const int w, const int h, const int d, const int mipMapCount, const int arraySize = 1);
	// The following Create function will use passed in data as reference without allocating memory for internal pData (meaning the Image object will not own the data)
	unsigned char* Create(const TinyImageFormat fmt, const int w, const int h, const int d, const int mipMapCount, const int arraySize, const unsigned char* rawData);

	void Clear();

    //load image
    bool LoadFromFile(
                      const Path* filePath, memoryAllocationFunc pAllocator = NULL, void* pUserData = NULL);
    bool LoadFromMemory(
                        void const* mem, uint32_t size, char const* extension, memoryAllocationFunc pAllocator = NULL,
                        void* pUserData = NULL);

public:

    void RedefineDimensions(const TinyImageFormat fmt, const int w, const int h, const int d, const int mipMapCount, const int arraySize = 1);

	unsigned char* GetPixels() const { return pData; }
	unsigned char* GetPixels(const uint mipMapLevel) const;
	unsigned char* GetPixels(unsigned char* pDstData, const uint mipMapLevel, const uint dummy);
	unsigned char* GetPixels(const uint mipMapLevel, const uint arraySlice) const;

	void SetPixels(unsigned char* pixelData, bool own = false)
	{
		mOwnsMemory = own;
		pData = pixelData;
	}
	void SetPath(const Path* path) {
        mLoadFilePath = fsCopyPath(path);
    }

	uint                 GetWidth() const { return mWidth; }
	uint                 GetHeight() const { return mHeight; }
	uint                 GetDepth() const { return mDepth; }
	uint                 GetWidth(const int mipMapLevel) const;
	uint                 GetHeight(const int mipMapLevel) const;
	uint                 GetDepth(const int mipMapLevel) const;
	uint                 GetMipMapCount() const { return mMipMapCount; }
	const Path*          GetPath() const { return mLoadFilePath; }
	uint                 GetMipMapCountFromDimensions() const;
	uint                 GetArraySliceSize(const uint mipMapLevel = 0, TinyImageFormat srcFormat = TinyImageFormat_UNDEFINED) const;
	uint                 GetNumberOfPixels(const uint firstMipLevel = 0, uint numMipLevels = ALL_MIPLEVELS) const;
	bool                 GetColorRange(float& min, float& max);
	TinyImageFormat    	 GetFormat() const { return mFormat; }
	uint                 GetArrayCount() const { return mArrayCount; }
	uint                 GetMipMappedSize(
		const uint firstMipLevel = 0, uint numMipLevels = ALL_MIPLEVELS, TinyImageFormat srcFormat = TinyImageFormat_UNDEFINED) const;

	bool                 Is1D() const { return (mDepth == 1 && mHeight == 1); }
	bool                 Is2D() const { return (mDepth == 1 && mHeight > 1); }
	bool                 Is3D() const { return (mDepth > 1); }
	bool                 IsArray() const { return (mArrayCount > 1); }
	bool                 IsCube() const { return (mDepth == 0); }
	bool                 IsSrgb() const { return TinyImageFormat_IsSRGB(mFormat); }
	bool                 IsLinearLayout() const { return mLinearLayout; }
	bool                 AreMipsAfterSlices() const { return mMipsAfterSlices; }

	void                 SetMipsAfterSlices(bool onoff) { mMipsAfterSlices = onoff; }

	bool                 Normalize();
	bool                 Uncompress();
	bool                 Unpack();

	bool                 Convert(const TinyImageFormat newFormat);
	bool                 GenerateMipMaps(const uint32_t mipMaps = ALL_MIPLEVELS);

	bool                 iSwap(const int c0, const int c1);

	// Image Format Saving
	bool                 iSaveDDS(const Path* filePath);
	bool                 iSaveKTX(const Path* filePath);
	bool                 iSaveTGA(const Path* filePath);
	bool                 iSaveBMP(const Path* filePath);
	bool                 iSavePNG(const Path* filePath);
	bool                 iSaveHDR(const Path* filePath);
	bool                 iSaveJPG(const Path* filePath);
	bool                 iSaveSVT(const Path* filePath, uint pageSize = 128);
	bool                 Save(const Path* filePath);

protected:
	unsigned char*       pData;
	PathHandle           mLoadFilePath;
	uint                 mWidth, mHeight, mDepth;
	uint                 mMipMapCount;
	uint                 mArrayCount;
	TinyImageFormat    	 mFormat;
	int                  mAdditionalDataSize;
	unsigned char*       pAdditionalData;
	bool                 mLinearLayout;
	bool                 mOwnsMemory;
	// is memory (mipmaps*w*h*d)*s or
	// mipmaps * (w*h*d*s) with s being constant for all mipmaps
	bool				 mMipsAfterSlices;
	
	static void Init();
	static void Exit();

public:
	typedef bool (*ImageLoaderFunction)(
		Image* pImage, const char* memory, uint32_t memSize, memoryAllocationFunc pAllocator, void* pUserData);
	static void AddImageLoader(const char* pExtension, ImageLoaderFunction pFunc);
};

static inline uint32_t calculateMipMapLevels(uint32_t width, uint32_t height)
{
	if (width == 0 || height == 0)
		return 0;

	uint32_t result = 1;
	while (width > 1 || height > 1)
	{
		width >>= 1;
		height >>= 1;
		result++;
	}
	return result;
}

#endif    // COMMON_3_OS_IMAGE_IMAGE_H_
