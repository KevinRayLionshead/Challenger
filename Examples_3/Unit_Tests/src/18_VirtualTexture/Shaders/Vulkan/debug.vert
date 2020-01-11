#version 450 core

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

layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Normal;
layout(location = 2) in vec2 UV;

layout(location = 0) out vec2 outUV;

void main ()
{	
  uint vertexID = gl_VertexIndex;

  vec4 position;
  position.zw = vec2(0.0, 1.0);

  if(vertexID == 0)
  {
      position.x = -1.0;
      position.y = -1.0;
  }
  else if(vertexID == 1)
  {
      position.x = -1.0;
      position.y = 1.0;
  }
  else if(vertexID == 2)
  {
      position.x = 1.0;
      position.y = -1.0;
  }
  else if(vertexID == 3)
  {
      position.x = 1.0;
      position.y = -1.0;
  }
  else if(vertexID == 4)
  {
      position.x = -1.0;
      position.y = 1.0;
  }
  else
  {
      position.x = 1.0;
      position.y = 1.0;
  }

  gl_Position = position;
  outUV = position.xy * vec2(0.5, (-0.5)) + vec2(0.5);
}