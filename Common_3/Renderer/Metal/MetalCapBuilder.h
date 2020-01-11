#pragma once


inline void utils_caps_builder(Renderer* pRenderer) {
	// for metal this is a case of going through each family and looking up the info off apple documentation
	// we start low and go higher, add things as we go
	// data from https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf

	memset(pRenderer->capBits.canShaderReadFrom, 0, sizeof(pRenderer->capBits.canShaderReadFrom));
	memset(pRenderer->capBits.canShaderWriteTo, 0, sizeof(pRenderer->capBits.canShaderWriteTo));
	memset(pRenderer->capBits.canRenderTargetWriteTo, 0, sizeof(pRenderer->capBits.canRenderTargetWriteTo));

	// all pixel formats that metal support it claims can be sampled from if they exist on the platform
	// this is however a lie when compressed texture formats
	for(uint32_t i = 0; i < TinyImageFormat_Count;++i) {
		TinyImageFormat_MTLPixelFormat mtlFmt = TinyImageFormat_ToMTLPixelFormat((TinyImageFormat) i);

		if(mtlFmt != TIF_MTLPixelFormatInvalid) {
#ifndef TARGET_IOS
			pRenderer->capBits.canShaderReadFrom[i] = TinyImageFormat_MTLPixelFormatOnMac(mtlFmt);
#else
			pRenderer->capBits.canShaderReadFrom[i] = TinyImageFormat_MTLPixelFormatOnIOs(mtlFmt);
#endif
		} else {
			pRenderer->capBits.canShaderReadFrom[i] = false;
		}
	}
#define CAN_SHADER_WRITE(x) pRenderer->capBits.canShaderWriteTo[TinyImageFormat_##x] = true;
#define CAN_RENDER_TARGET_WRITE(x) pRenderer->capBits.canRenderTargetWriteTo[TinyImageFormat_##x] = true;

	// this call is supported on mac and ios
	// technially I think you can write but not read some texture, this is telling
	// you you can do both. TODO work out the semantics behind write vs read/write.
	MTLReadWriteTextureTier rwTextureTier = [pRenderer->pDevice readWriteTextureSupport];
	// intentional fall through on this switch
	switch(rwTextureTier) {
	default:
	case MTLReadWriteTextureTier2:
		CAN_SHADER_WRITE(R32G32B32A32_SFLOAT);
		CAN_SHADER_WRITE(R32G32B32A32_UINT);
		CAN_SHADER_WRITE(R32G32B32A32_SINT);
		CAN_SHADER_WRITE(R16G16B16A16_SFLOAT);
		CAN_SHADER_WRITE(R16G16B16A16_UINT);
		CAN_SHADER_WRITE(R16G16B16A16_SINT);
		CAN_SHADER_WRITE(R8G8B8A8_UNORM);
		CAN_SHADER_WRITE(R8G8B8A8_UINT);
		CAN_SHADER_WRITE(R8G8B8A8_SINT);
		CAN_SHADER_WRITE(R16_SFLOAT);
		CAN_SHADER_WRITE(R16_UINT);
		CAN_SHADER_WRITE(R16_SINT);
		CAN_SHADER_WRITE(R8_UNORM);
		CAN_SHADER_WRITE(R8_UINT);
		CAN_SHADER_WRITE(R8_SINT);
	case MTLReadWriteTextureTier1:
		CAN_SHADER_WRITE(R32_SFLOAT);
		CAN_SHADER_WRITE(R32_UINT);
		CAN_SHADER_WRITE(R32_SINT);
	case MTLReadWriteTextureTierNone: break;
	}

#ifndef TARGET_IOS
	uint32_t familyTier = 0;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_macOS_GPUFamily1_v1] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_macOS_GPUFamily1_v2] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_macOS_GPUFamily1_v3] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_macOS_GPUFamily1_v4] ? 1 : familyTier;

	if( familyTier >= 1 ) {
		CAN_RENDER_TARGET_WRITE(R8_UNORM); // this has a subscript 8 which makes no sense
		CAN_RENDER_TARGET_WRITE(R8_SNORM);
		CAN_RENDER_TARGET_WRITE(R8_UINT);
		CAN_RENDER_TARGET_WRITE(R8_SINT);

		CAN_RENDER_TARGET_WRITE(R16_UNORM);
		CAN_RENDER_TARGET_WRITE(R16_SNORM);
		CAN_RENDER_TARGET_WRITE(R16_SFLOAT);
		CAN_RENDER_TARGET_WRITE(R16_UINT);
		CAN_RENDER_TARGET_WRITE(R16_SINT);

		CAN_RENDER_TARGET_WRITE(R8G8_UNORM);
		CAN_RENDER_TARGET_WRITE(R8G8_SNORM);
		CAN_RENDER_TARGET_WRITE(R8G8_UINT);
		CAN_RENDER_TARGET_WRITE(R8G8_SINT);

		CAN_RENDER_TARGET_WRITE(R8G8B8A8_UNORM);
		CAN_RENDER_TARGET_WRITE(R8G8B8A8_SNORM);
		CAN_RENDER_TARGET_WRITE(R8G8B8A8_SRGB);
		CAN_RENDER_TARGET_WRITE(B8G8R8A8_UNORM);
		CAN_RENDER_TARGET_WRITE(B8G8R8A8_SRGB);
		CAN_RENDER_TARGET_WRITE(R16G16_UNORM);
		CAN_RENDER_TARGET_WRITE(R16G16_SNORM);
		CAN_RENDER_TARGET_WRITE(R16G16_SFLOAT);
		CAN_RENDER_TARGET_WRITE(R32_SFLOAT);
		CAN_RENDER_TARGET_WRITE(A2R10G10B10_UNORM);
		CAN_RENDER_TARGET_WRITE(A2B10G10R10_UNORM);
		CAN_RENDER_TARGET_WRITE(B10G11R11_UFLOAT);

		CAN_RENDER_TARGET_WRITE(R16G16B16A16_UNORM);
		CAN_RENDER_TARGET_WRITE(R16G16B16A16_SNORM);
		CAN_RENDER_TARGET_WRITE(R16G16B16A16_SFLOAT);
		CAN_RENDER_TARGET_WRITE(R32G32_SFLOAT);

		CAN_RENDER_TARGET_WRITE(R32G32B32A32_SFLOAT);

		CAN_RENDER_TARGET_WRITE(D16_UNORM);
		CAN_RENDER_TARGET_WRITE(D32_SFLOAT);
		CAN_RENDER_TARGET_WRITE(S8_UINT);
		CAN_RENDER_TARGET_WRITE(D32_SFLOAT_S8_UINT);
	};
    
    bool depth24Stencil8Supported = [pRenderer->pDevice isDepth24Stencil8PixelFormatSupported];
    
    pRenderer->capBits.canShaderReadFrom[TinyImageFormat_D24_UNORM_S8_UINT] = depth24Stencil8Supported;
    pRenderer->capBits.canShaderReadFrom[TinyImageFormat_X8_D24_UNORM] = depth24Stencil8Supported;
    pRenderer->capBits.canShaderWriteTo[TinyImageFormat_D24_UNORM_S8_UINT] = depth24Stencil8Supported;
    pRenderer->capBits.canShaderWriteTo[TinyImageFormat_X8_D24_UNORM] = depth24Stencil8Supported;
    pRenderer->capBits.canRenderTargetWriteTo[TinyImageFormat_D24_UNORM_S8_UINT] = depth24Stencil8Supported;
    pRenderer->capBits.canRenderTargetWriteTo[TinyImageFormat_X8_D24_UNORM] = depth24Stencil8Supported;
    
#else // iOS
	uint32_t familyTier = 0;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily1_v1] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily1_v2] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily1_v3] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily1_v4] ? 1 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily1_v5] ? 1 : familyTier;

	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily2_v1] ? 2 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily2_v2] ? 2 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily2_v3] ? 2 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily2_v4] ? 2 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily2_v5] ? 2 : familyTier;

	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily3_v1] ? 3 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily3_v2] ? 3 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily3_v3] ? 3 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily3_v4] ? 3 : familyTier;

	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily4_v1] ? 4 : familyTier;
	familyTier = [pRenderer->pDevice supportsFeatureSet: MTLFeatureSet_iOS_GPUFamily4_v2] ? 4 : familyTier;

    if(familyTier == 1) {
		// this is a tier 1 decide so no astc and XR
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_4x4_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_4x4_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_5x4_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_5x4_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_5x5_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_5x5_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_6x5_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_6x5_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_6x6_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_6x6_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_8x5_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_8x5_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_8x6_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_8x6_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_8x8_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_8x8_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x5_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x5_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x6_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x6_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x8_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x8_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x10_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_10x10_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_12x10_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_12x10_SRGB] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_12x12_UNORM] = false;
		pRenderer->capBits.canShaderReadFrom[TinyImageFormat_ASTC_12x12_SRGB] = false;

		// TODO when TinyImageFormat supports XR formats exclude them here for tier 1
	}

    if(familyTier >= 1) {
		CAN_RENDER_TARGET_WRITE(R8_UNORM); // this has a subscript 8 which makes no sense
		CAN_RENDER_TARGET_WRITE(R8_SNORM);
		CAN_RENDER_TARGET_WRITE(R8_UINT);
		CAN_RENDER_TARGET_WRITE(R8_SINT);
		CAN_RENDER_TARGET_WRITE(R8_SRGB);

		CAN_RENDER_TARGET_WRITE(R16_UNORM);
		CAN_RENDER_TARGET_WRITE(R16_SNORM);
		CAN_RENDER_TARGET_WRITE(R16_SFLOAT);
		CAN_RENDER_TARGET_WRITE(R16_UINT);
		CAN_RENDER_TARGET_WRITE(R16_SINT);

		CAN_RENDER_TARGET_WRITE(R8G8_UNORM);
		CAN_RENDER_TARGET_WRITE(R8G8_SNORM);
		CAN_RENDER_TARGET_WRITE(R8G8_SRGB);
		CAN_RENDER_TARGET_WRITE(R8G8_UINT);
		CAN_RENDER_TARGET_WRITE(R8G8_SINT);

		CAN_RENDER_TARGET_WRITE(B5G6R5_UNORM);
		CAN_RENDER_TARGET_WRITE(A1R5G5B5_UNORM);
		CAN_RENDER_TARGET_WRITE(A4B4G4R4_UNORM);
		CAN_RENDER_TARGET_WRITE(R5G6B5_UNORM);
		CAN_RENDER_TARGET_WRITE(B5G5R5A1_UNORM);

		CAN_RENDER_TARGET_WRITE(R8G8B8A8_UNORM);
		CAN_RENDER_TARGET_WRITE(R8G8B8A8_SNORM);
		CAN_RENDER_TARGET_WRITE(R8G8B8A8_SRGB);
		CAN_RENDER_TARGET_WRITE(B8G8R8A8_UNORM);
		CAN_RENDER_TARGET_WRITE(B8G8R8A8_SRGB);
		CAN_RENDER_TARGET_WRITE(R16G16_UNORM);
		CAN_RENDER_TARGET_WRITE(R16G16_SNORM);
		CAN_RENDER_TARGET_WRITE(R16G16_SFLOAT);
		CAN_RENDER_TARGET_WRITE(R32_SFLOAT);
		CAN_RENDER_TARGET_WRITE(A2R10G10B10_UNORM);
		CAN_RENDER_TARGET_WRITE(A2B10G10R10_UNORM);
		CAN_RENDER_TARGET_WRITE(B10G11R11_UFLOAT);

		CAN_RENDER_TARGET_WRITE(R16G16B16A16_UNORM);
		CAN_RENDER_TARGET_WRITE(R16G16B16A16_SNORM);
		CAN_RENDER_TARGET_WRITE(R16G16B16A16_SFLOAT);
		CAN_RENDER_TARGET_WRITE(R32G32_SFLOAT);

		CAN_RENDER_TARGET_WRITE(R32G32B32A32_SFLOAT);

		CAN_RENDER_TARGET_WRITE(D32_SFLOAT);
		CAN_RENDER_TARGET_WRITE(S8_UINT);
		CAN_RENDER_TARGET_WRITE(D32_SFLOAT_S8_UINT);
		CAN_RENDER_TARGET_WRITE(D32_SFLOAT_S8_UINT);
	}

	if (@available(iOS 13, *)) {
		pRenderer->capBits.canShaderWriteTo[TinyImageFormat_D16_UNORM] = true;
		pRenderer->capBits.canRenderTargetWriteTo[TinyImageFormat_D16_UNORM] = true;
	}

#endif

#undef CAN_SHADER_WRITE
#undef CAN_RENDER_TARGET_WRITE
}
