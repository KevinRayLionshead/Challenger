#ifndef MissShader_h
#define MissShader_h

#include "ShaderTypes.h"

struct MetalMissShader {
	
	uint _activeThreadIndex;
	uint _pathIndex;
	PathCallStack _callStack;
	
	device Ray& _ray;
	device Payload& _payload;
	
	short _nextSubshader;
	
	MetalMissShader(uint tid,
					  constant RaytracingArguments& arguments,
					  const device uint* pathIndices,
					  const device uint2& pathBaseOffsetAndCount,
					  constant short& shaderIndex,
					  short subshaderIndex) :
	_activeThreadIndex(pathBaseOffsetAndCount.x + tid),
	_pathIndex(pathIndices[_activeThreadIndex]),
	_callStack(arguments.pathCallStackHeaders, arguments.pathCallStackFunctions, _pathIndex, arguments.uniforms.maxCallStackDepth),
	_ray(arguments.rays[_activeThreadIndex]),
	_payload(arguments.payloads[_pathIndex]),
	_nextSubshader(subshaderIndex + 1 < MetalMissShader::subshaderCount() ? shaderIndex + 1 : -1)
	{
		_ray.maxDistance = -1.f;
	}
	
	void setupNextShader()
	{
		if (_nextSubshader >= 0)
		{
			_callStack.PushFunction(_nextSubshader);
		}
	}
	
	void SkipSubshaders()
	{
		_nextSubshader = -1;
	}
	
	void CallShader(uint shaderIndex)
	{
		_callStack.PushFunction(shaderIndex);
	}
	
	void TraceRay(Ray ray, short missShaderIndex = -1, uchar rayContributionToHitGroupIndex = 0)
	{
		_ray = ray;
		_callStack.SetRayContributionToHitGroupIndex(rayContributionToHitGroupIndex);
//		_callStack.SetMultiplierForGeometryContributionToHitGroupIndex(multiplierForGeometryContributionToHitGroupIndex);
		if (missShaderIndex >= 0)
		{
			_callStack.SetMissShaderIndex(missShaderIndex);
		}
		else
		{
			_callStack.SetHitShaderOnly();
		}
		
		_payload.intersectionIndex = _activeThreadIndex;
	}
	
	static ushort subshaderCount();
	
	// ---------------------------------------------------------------------------------------
	// User shaders.
	
	void shader0(uint pathIndex,
				 constant Uniforms & uniforms,
				 device Payload &payload,
				 constant CSDataPerFrame& csDataPerFrame,
				 constant CSData& csData
				 );
	
	void shader1(uint pathIndex,
				constant Uniforms & uniforms,
				device Payload &payload,
				constant CSDataPerFrame& csDataPerFrame,
				constant CSData& csData
	);
	
	void shader2(uint pathIndex,
				constant Uniforms & uniforms,
				device Payload &payload,
				constant CSDataPerFrame& csDataPerFrame,
				constant CSData& csData
	);
	
	void shader3(uint pathIndex,
				constant Uniforms & uniforms,
				device Payload &payload,
				constant CSDataPerFrame& csDataPerFrame,
				constant CSData& csData
	);
	
	void shader4(uint pathIndex,
				constant Uniforms & uniforms,
				device Payload &payload,
				constant CSDataPerFrame& csDataPerFrame,
				constant CSData& csData
	);
};

#define DEFINE_METAL_MISS_SHADER(name, index) \
kernel void name(uint tid                                [[thread_position_in_grid]], \
				   constant RaytracingArguments & arguments	[[buffer(0)]], \
				   const device uint* pathIndices			 	[[buffer(1)]], \
				   const device uint2& pathBaseOffsetAndCount	[[buffer(2)]], \
				   constant short& shaderIndex				[[buffer(3)]], \
				   constant CSDataPerFrame& csDataPerFrame	[[buffer(UPDATE_FREQ_PER_FRAME)]], \
				   constant CSData& csData					[[buffer(UPDATE_FREQ_NONE)]] \
				   ) \
{ \
	if (tid >= pathBaseOffsetAndCount.y) { \
		return; \
	} \
	MetalMissShader missShader(tid, arguments, pathIndices, pathBaseOffsetAndCount, shaderIndex, /* subshaderIndex = */ index); \
	missShader.shader##index(missShader._pathIndex, arguments.uniforms, missShader._payload, csDataPerFrame, csData); \
	missShader.setupNextShader(); \
}

#endif /* MissShader_h */
