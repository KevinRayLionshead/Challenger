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


// Shader for simple shading with a point light
// for skeletons in Unit Tests Animation

#define MAX_INSTANCES 815

layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Normal;

layout(location = 0) out vec4 Color;

layout (std140, UPDATE_FREQ_PER_DRAW, binding=0) uniform uniformBlock {
	uniform mat4 mvp;

    uniform vec4 color[MAX_INSTANCES];
    // Point Light Information
    uniform vec4 lightPosition;
    uniform vec4 lightColor;

    uniform mat4 toWorld[MAX_INSTANCES];
};

void main ()
{
  float scaleFactor = 0.065f;
  mat4 scaleMat = {{scaleFactor, 0.0, 0.0, 0.0}, {0.0f, scaleFactor, 0.0, 0.0}, {0.0f, 0.0, scaleFactor, 0.0}, {0.0f, 0.0, 0.0, 1.0f}};
	mat4 tempMat = mvp * toWorld[gl_InstanceIndex] * scaleMat;
	gl_Position = tempMat * vec4(Position.xyz, 1.0f);
	
	vec4 normal = normalize(toWorld[gl_InstanceIndex] * vec4(Normal.xyz, 0.0f));
	vec4 pos = toWorld[gl_InstanceIndex] * vec4(Position.xyz, 1.0f);
	
	float lightIntensity = 1.0f;
    float quadraticCoeff = 1.2;
    float ambientCoeff = 0.4;
	
	vec3 lightDir = normalize(lightPosition.xyz - pos.xyz);
	
    float distance = length(lightDir);
    float attenuation = 1.0 / (quadraticCoeff * distance * distance);
    float intensity = lightIntensity * attenuation;

    vec3 baseColor = color[gl_InstanceIndex].xyz;
    vec3 blendedColor = lightColor.xyz * baseColor * lightIntensity;
    vec3 diffuse = blendedColor * max(dot(normal.xyz, lightDir), 0.0);
    vec3 ambient = baseColor * ambientCoeff;
    Color = vec4(diffuse + ambient, 1.0);
}
