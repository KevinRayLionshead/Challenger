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

#include "shader_defs.h"
#include "packing.h"

#ifndef CONF_EARLY_DEPTH_STENCIL
#define CONF_EARLY_DEPTH_STENCIL
#endif

ConstantBuffer<RootConstant> indirectRootConstant : register(b1);

struct PsInOpaque
{
    float4 position : SV_Position;
};

uint calculateOutputVBID(bool opaque, uint drawID, uint primitiveID)
{
    uint drawID_primID = ((drawID << 23) & 0x7F800000) | (primitiveID & 0x007FFFFF);
    return (opaque) ? drawID_primID : (1 << 31) | drawID_primID;
}

//#define __XBOX_FORCE_PS_ZORDER_EARLY_Z_THEN_RE_Z
CONF_EARLY_DEPTH_STENCIL
float4 main(PsInOpaque In, uint primitiveId : SV_PrimitiveID) : SV_Target
{
    return unpackUnorm4x8(calculateOutputVBID(true, indirectRootConstant.drawId, primitiveId));
}
