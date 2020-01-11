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

// USERMACRO: SAMPLE_COUNT [1,2,4]
// USERMACRO: AO_QUALITY [1,2,3,4]

//--------------------------------------------------------------------------------------
// Gather pattern
//--------------------------------------------------------------------------------------

#include <metal_stdlib>
using namespace metal;

// Gather defines
#define RING_1    (1)
#define RING_2    (2)
#define RING_3    (3)
#define RING_4    (4)
#define NUM_RING_1_GATHERS    (2)
#define NUM_RING_2_GATHERS    (6)
#define NUM_RING_3_GATHERS    (12)
#define NUM_RING_4_GATHERS    (20)

// Ring sample pattern
static constant float2 g_f2HDAORingPattern[NUM_RING_4_GATHERS] =
{
    // Ring 1
    { 1, -1 },
    { 0, 1 },
    
    // Ring 2
    { 0, 3 },
    { 2, 1 },
    { 3, -1 },
    { 1, -3 },
        
    // Ring 3
    { 1, -5 },
    { 3, -3 },
    { 5, -1 },
    { 4, 1 },
    { 2, 3 },
    { 0, 5 },
    
    // Ring 4
    { 0, 7 },
    { 2, 5 },
    { 4, 3 },
    { 6, 1 },
    { 7, -1 },
    { 5, -3 },
    { 3, -5 },
    { 1, -7 },
};

// Ring weights
static constant float4 g_f4HDAORingWeight[NUM_RING_4_GATHERS] =
{
    // Ring 1 (Sum = 5.30864)
    { 1.00000, 0.50000, 0.44721, 0.70711 },
    { 0.50000, 0.44721, 0.70711, 1.00000 },
    
    // Ring 2 (Sum = 6.08746)
    { 0.30000, 0.29104, 0.37947, 0.40000 },
    { 0.42426, 0.33282, 0.37947, 0.53666 },
    { 0.40000, 0.30000, 0.29104, 0.37947 },
    { 0.53666, 0.42426, 0.33282, 0.37947 },
    
    // Ring 3 (Sum = 6.53067)
    { 0.31530, 0.29069, 0.24140, 0.25495 },
    { 0.36056, 0.29069, 0.26000, 0.30641 },
    { 0.26000, 0.21667, 0.21372, 0.25495 },
    { 0.29069, 0.24140, 0.25495, 0.31530 },
    { 0.29069, 0.26000, 0.30641, 0.36056 },
    { 0.21667, 0.21372, 0.25495, 0.26000 },
    
    // Ring 4 (Sum = 7.00962)
    { 0.17500, 0.17365, 0.19799, 0.20000 },
    { 0.22136, 0.20870, 0.24010, 0.25997 },
    { 0.24749, 0.21864, 0.24010, 0.28000 },
    { 0.22136, 0.19230, 0.19799, 0.23016 },
    { 0.20000, 0.17500, 0.17365, 0.19799 },
    { 0.25997, 0.22136, 0.20870, 0.24010 },
    { 0.28000, 0.24749, 0.21864, 0.24010 },
    { 0.23016, 0.22136, 0.19230, 0.19799 },
};

static constant float g_fRingWeightsTotal[RING_4] =
{
    5.30864,
    11.39610,
    17.92677,
    24.93639,
};

struct PsIn
{
    float4 position [[position]];
};

struct HDAOParameters
{
	packed_float2 f2RTSize;                  // Used by HDAO shaders for scaling texture coords
	float fHDAORejectRadius;          // HDAO param
	float fHDAOIntensity;             // HDAO param
	float fHDAOAcceptRadius;          // HDAO param
	float fQ;                         // far / (far - near)
	float fQTimesZNear;               // Q * near
};

//----------------------------------------------------------------------------------------
// Helper function to Gather samples
//----------------------------------------------------------------------------------------
float4 GatherSamples(
#if SAMPLE_COUNT > 1
                     depth2d_ms<float,access::read> Tex,
#else
                     depth2d<float,access::sample> Tex,
#endif
                     sampler TexSampler, float2 f2TexCoord, HDAOParameters params)
{
    float4 f4Ret = 0;

#if SAMPLE_COUNT > 1
    uint2 loc = uint2(f2TexCoord * params.f2RTSize);
    f4Ret.x = Tex.read(loc + uint2(0,1), 0);
    f4Ret.y = Tex.read(loc + uint2(1,1), 0);
    f4Ret.z = Tex.read(loc + uint2(1,0), 0);
    f4Ret.w = Tex.read(loc + uint2(0,0), 0);
#else
    f4Ret = Tex.gather(TexSampler, f2TexCoord, int2(0));
#endif

    return f4Ret;
}


//--------------------------------------------------------------------------------------
// Helper function to gather Z values in 10.1 and 10.0 modes
//--------------------------------------------------------------------------------------
float4 GatherZSamples(
#if SAMPLE_COUNT > 1
                      depth2d_ms<float,access::read> Tex,
#else
                      depth2d<float,access::sample> Tex,
#endif
                      sampler TexSampler, float2 f2TexCoord, HDAOParameters params)
{
    float4 f4Ret;
    float4 f4Gather;
        
    f4Gather = GatherSamples( Tex, TexSampler, f2TexCoord, params);
    f4Ret = float4(-params.fQTimesZNear / ( f4Gather - params.fQ ));
    
    return f4Ret;
}

#if AO_QUALITY == 2
static constant int iNumRings = RING_2;
static constant int iNumRingGathers = NUM_RING_2_GATHERS;
#elif AO_QUALITY == 3
static constant int iNumRings = RING_3;
static constant int iNumRingGathers = NUM_RING_3_GATHERS;
#elif AO_QUALITY == 4
static constant int iNumRings = RING_4;
static constant int iNumRingGathers = NUM_RING_4_GATHERS;
#else
static constant int iNumRings = RING_1;
static constant int iNumRingGathers = NUM_RING_1_GATHERS;
#endif

struct FSData {
    sampler g_SamplePoint;
#if SAMPLE_COUNT > 1
    depth2d_ms<float,access::read>  g_txDepth;
#else
    depth2d<float,access::sample>  g_txDepth;
#endif
};

fragment float4 stageMain(PsIn In [[stage_in]],
    constant FSData& fsData [[buffer(UPDATE_FREQ_NONE)]],
    constant HDAOParameters& HDAORootConstants    [[buffer(UPDATE_FREQ_USER)]]
)
{
    // Locals
    uint2  ui2ScreenCoord;
    float2 f2ScreenCoord;
    float2 f2TexCoord;
    float2 f2MirrorTexCoord;
    float2 f2InvRTSize;
    float4 f4SampledZ[2];
    float4 f4Diff;
    float4 f4Compare[2];
    float4 f4Occlusion = 0.0f;
    float fOcclusion;
    int iGather;
    float2 f2KernelScale = float2( HDAORootConstants.f2RTSize.x / 1024.0f, HDAORootConstants.f2RTSize.y / 1024.0f );
                            
    // Compute integer screen coord, and store off the inverse of the RT Size
    f2InvRTSize = 1.0f / HDAORootConstants.f2RTSize;
    f2ScreenCoord = In.position.xy;
    ui2ScreenCoord = uint2( f2ScreenCoord );
                
    {                
        f2TexCoord = float2( f2ScreenCoord * f2InvRTSize );
        // Sample the center pixel for camera Z
        float fDepth = 0;
#if SAMPLE_COUNT > 1
        fDepth = fsData.g_txDepth.read(ui2ScreenCoord, 0);
#else
        fDepth = fsData.g_txDepth.sample(fsData.g_SamplePoint, f2TexCoord, level(0.0f));
        f2ScreenCoord = float2(ui2ScreenCoord);
#endif
        float fCenterZ = -HDAORootConstants.fQTimesZNear / ( fDepth - HDAORootConstants.fQ );
            
        // Loop through each gather location, and compare with its mirrored location
        for( iGather=0; iGather < iNumRingGathers; iGather++ )
        {
            float2 f2SampleOffSet = f2KernelScale * g_f2HDAORingPattern[iGather];
            float2 f2MirrorSampleOffSet = (f2SampleOffSet + float2(1.0f, 1.0f)) * float2(-1.0f, -1.0f) ;
            
            // Sample
            f2TexCoord = float2(f2ScreenCoord + f2SampleOffSet)  * f2InvRTSize;
            f2MirrorTexCoord = float2(f2ScreenCoord + f2MirrorSampleOffSet) * f2InvRTSize;
            
#if SAMPLE_COUNT > 1
            f4SampledZ[0] = GatherZSamples( fsData.g_txDepth, fsData.g_SamplePoint, f2TexCoord, HDAORootConstants);
            f4SampledZ[1] = GatherZSamples( fsData.g_txDepth, fsData.g_SamplePoint, f2MirrorTexCoord, HDAORootConstants);
#else
            f4SampledZ[0] = GatherZSamples( fsData.g_txDepth, fsData.g_SamplePoint, f2TexCoord +  f2InvRTSize, HDAORootConstants);
            f4SampledZ[1] = GatherZSamples( fsData.g_txDepth, fsData.g_SamplePoint, f2MirrorTexCoord  + f2InvRTSize, HDAORootConstants);
#endif
                        
            // Detect valleys
            f4Diff = -float4(fCenterZ) + f4SampledZ[0];
            f4Compare[0] =  all(f4Diff < float4(HDAORootConstants.fHDAORejectRadius)) ? ( 1.0f ) : ( 0.0f );
            f4Compare[0] *= all(f4Diff > float4(HDAORootConstants.fHDAOAcceptRadius)) ? ( 1.0f ) : ( 0.0f );
            f4Diff = -float4(fCenterZ) + f4SampledZ[1];
            f4Compare[1] = all(f4Diff < float4(HDAORootConstants.fHDAORejectRadius)) ? ( 1.0f ) : ( 0.0f );
            f4Compare[1] *= all(f4Diff > float4(HDAORootConstants.fHDAOAcceptRadius)) ? ( 1.0f ) : ( 0.0f );
            
            f4Occlusion.xyzw += ( g_f4HDAORingWeight[iGather].xyzw * ( f4Compare[0].xyzw * f4Compare[1].zwxy ) );    
        }
    }
                    
    // Finally calculate the HDAO occlusion value
    fOcclusion = ( ( f4Occlusion.x + f4Occlusion.y + f4Occlusion.z + f4Occlusion.w ) / ( 2.0f * g_fRingWeightsTotal[iNumRings - 1] ) );

    fOcclusion *= ( HDAORootConstants.fHDAOIntensity );
    fOcclusion = 1.0f - saturate( fOcclusion );
    return fOcclusion;
}
