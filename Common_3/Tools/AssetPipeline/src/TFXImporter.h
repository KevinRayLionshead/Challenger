#pragma once

/*
*Copyright (c) 2017 Advanced Micro Devices, Inc. All rights reserved.
*
*Permission is hereby granted, free of charge, to any person obtaining a copy
*of this software and associated documentation files (the "Software"), to deal
*in the Software without restriction, including without limitation the rights
*to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*copies of the Software, and to permit persons to whom the Software is
*furnished to do so, subject to the following conditions:
*
*The above copyright notice and this permission notice shall be included in
*all copies or substantial portions of the Software.
*
*THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
*AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
*THE SOFTWARE.
*/

#include "../../../ThirdParty/OpenSource/EASTL/string.h"
#include "../../../ThirdParty/OpenSource/EASTL/vector.h"
#include "../../../OS/Math/MathTypes.h"
#include "../../../OS/Interfaces/IFileSystem.h"

struct TFXAsset
{
	// Hair data from *.tfx
	eastl::vector<float4> mPositions;
	eastl::vector<float2> mStrandUV;
	eastl::vector<float4> mRefVectors;
	eastl::vector<float4> mGlobalRotations;
	eastl::vector<float4> mLocalRotations;
	eastl::vector<float4> mTangents;
	eastl::vector<float4> mFollowRootOffsets;
	eastl::vector<int>    mStrandTypes;
	eastl::vector<float>  mThicknessCoeffs;
	eastl::vector<float>  mRestLengths;
	eastl::vector<int>    mTriangleIndices;
	int                     mNumVerticesPerStrand;
	int                     mNumGuideStrands;
};

struct TFXVertex
{
	float3 mPosition;
	float3 mNormal;
	uint   mBoneIndices[4];
	float  mBoneWeights[4];
};

struct TFXMesh
{
	eastl::vector<eastl::string> mBones;
	eastl::vector<TFXVertex>       mVertices;
	eastl::vector<uint>            mIndices;
};

class TFXImporter
{
	public:
	static bool ImportTFX(
		const Path* filePath, int numFollowHairs, float tipSeperationFactor, float maxRadiusAroundGuideHair,
		TFXAsset* tfxAsset);
	static bool ImportTFXMesh(const Path* filePath, TFXMesh* tfxMesh);

	private:
	static bool ImportTFXV3(FileStream* fileHandle, int numFollowHairs, TFXAsset* tfxAsset);
};
