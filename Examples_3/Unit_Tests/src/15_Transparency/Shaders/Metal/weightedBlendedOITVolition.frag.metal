/* Write your header comments here */
#include <metal_stdlib>
using namespace metal;

struct Fragment_Shader
{
#ifndef MAX_NUM_OBJECTS
#define MAX_NUM_OBJECTS 64
#endif

#define SPECULAR_EXP 10.0
#if USE_SHADOWS!=0
    texture2d<float> VSM;
    sampler VSMSampler;
#if PT_USE_CAUSTICS!=0
    texture2d<float> VSMRed;
    texture2d<float> VSMGreen;
    texture2d<float> VSMBlue;
#endif

    float2 ComputeMoments(float depth)
    {
        float2 moments;
        (moments.x = depth);
        float2 pd = float2(dfdx(depth), dfdy(depth));
        (moments.y = ((depth * depth) + (0.25 * dot(pd, pd))));
        return moments;
    };
    float ChebyshevUpperBound(float2 moments, float t)
    {
        float p = (t <= moments.x);
        float variance = (moments.y - (moments.x * moments.x));
        (variance = max(variance, 0.0010000000));
        float d = (t - moments.x);
        float pMax = (variance / (variance + (d * d)));
        return max(p, pMax);
    };
    float3 ShadowContribution(float2 shadowMapPos, float distanceToLight)
    {
        float2 moments = VSM.sample(VSMSampler, shadowMapPos).xy;
        float3 shadow = ChebyshevUpperBound(moments, distanceToLight);
#if PT_USE_CAUSTICS!=0
        (moments = (float2)(VSMRed.sample(VSMSampler, shadowMapPos).xy));
        (shadow.r *= ChebyshevUpperBound(moments, distanceToLight));
        (moments = (float2)(VSMGreen.sample(VSMSampler, shadowMapPos).xy));
        (shadow.g *= ChebyshevUpperBound(moments, distanceToLight));
        (moments = (float2)(VSMBlue.sample(VSMSampler, shadowMapPos).xy));
        (shadow.b *= ChebyshevUpperBound(moments, distanceToLight));
#endif

        return shadow;
    };
#endif

    struct Material
    {
        float4 Color;
        float4 Transmission;
        float RefractionRatio;
        float Collimation;
		float2 Padding;
        uint TextureFlags;
        uint AlbedoTexID;
        uint MetallicTexID;
        uint RoughnessTexID;
        uint EmissiveTexID;
    };
    struct Uniforms_LightUniformBlock
    {
        float4x4 lightViewProj;
        float4 lightDirection;
        float4 lightColor;
    };
    constant Uniforms_LightUniformBlock & LightUniformBlock;
    struct Uniforms_CameraUniform
    {
        float4x4 camViewProj;
        float4x4 camViewMat;
        float4 camClipInfo;
        float4 camPosition;
    };
    constant Uniforms_CameraUniform & CameraUniform;
    struct Uniforms_MaterialUniform
    {
        Material Materials[MAX_NUM_OBJECTS];
    };
    constant Uniforms_MaterialUniform & MaterialUniform;
    struct Uniforms_MaterialTextures
    {
        array<texture2d<float, access::sample>, MAX_NUM_TEXTURES> Textures;
    };
    constant texture2d<float, access::sample>* MaterialTextures;
    sampler LinearSampler;
    float4 Shade(uint matID, float2 uv, float3 worldPos, float3 normal)
    {
        float nDotl = dot(normal, (-LightUniformBlock.lightDirection.xyz));
        Material mat = MaterialUniform.Materials[matID];
        float4 matColor = (((mat.TextureFlags & (uint)(1)))?(MaterialTextures[mat.AlbedoTexID].sample(LinearSampler, uv)):(mat.Color));
        float3 viewVec = normalize((worldPos - CameraUniform.camPosition.xyz));
        if ((nDotl < 0.05))
        {
            (nDotl = 0.05);
        }
        float3 diffuse = ((LightUniformBlock.lightColor.xyz * matColor.xyz) * (float3)(nDotl));
        float3 specular = (LightUniformBlock.lightColor.xyz * (float3)(pow(saturate(dot(reflect((-LightUniformBlock.lightDirection.xyz), normal), viewVec)), SPECULAR_EXP)));
        float3 finalColor = saturate((diffuse + (specular * (float3)(0.5))));
#if USE_SHADOWS!=0
        float4 shadowMapPos = ((LightUniformBlock.lightViewProj)*(float4(worldPos, 1.0)));
        (shadowMapPos.y = (-shadowMapPos.y));
        (shadowMapPos.xy = ((shadowMapPos.xy + (float2)(1.0)) * (float2)(0.5)));
        if ((((clamp(shadowMapPos.x, 0.01, 0.99) == shadowMapPos.x) && (clamp(shadowMapPos.y, 0.01, 0.99) == shadowMapPos.y)) && (shadowMapPos.z > 0.0)))
        {
            float3 lighting = ShadowContribution(shadowMapPos.xy, shadowMapPos.z);
            (finalColor *= lighting);
        }
#endif

        return float4(finalColor, matColor.a);
    };
    struct VSOutput
    {
        float4 Position [[position]];
        float4 WorldPosition;
        float4 Normal;
        float4 UV;
        uint MatID;
    };
    struct PSOutput
    {
        float4 Accumulation [[color(0)]];
        float4 Revealage [[color(1)]];
    };
    struct Uniforms_WBOITSettings
    {
        float opacitySensitivity = 3.0;
        float weightBias = 5.0;
        float precisionScalar = 10000.0;
        float maximumWeight = 20.0;
        float maximumColorValue = 1000.0;
        float additiveSensitivity = 10.0;
        float emissiveSensitivityValue = 0.5;
    };
    constant Uniforms_WBOITSettings & WBOITSettings;
    void weighted_oit_process(thread float4(& accum), thread float(& revealage), thread float(& emissive_weight), float4 premultiplied_alpha_color, float raw_emissive_luminance, float view_depth, float current_camera_exposure)
    {
        float relative_emissive_luminance = (raw_emissive_luminance * current_camera_exposure);
        const float emissive_sensitivity = ((float)(1.0) / WBOITSettings.emissiveSensitivityValue);
        float clamped_emissive = saturate(relative_emissive_luminance);
        float clamped_alpha = saturate(premultiplied_alpha_color.a);
        float a = saturate(((clamped_alpha * WBOITSettings.opacitySensitivity) + (clamped_emissive * emissive_sensitivity)));
        const float canonical_near_z = 0.5;
        const float canonical_far_z = 300.0;
        float range = (canonical_far_z - canonical_near_z);
        float canonical_depth = saturate(((canonical_far_z / range) - ((canonical_far_z * canonical_near_z) / (view_depth * range))));
        float b = ((float)(1.0) - canonical_depth);
        float3 clamped_color = min(premultiplied_alpha_color.rgb, WBOITSettings.maximumColorValue);
        float w = (((WBOITSettings.precisionScalar * b) * b) * b);
        (w += WBOITSettings.weightBias);
        (w = min(w, WBOITSettings.maximumWeight));
        (w *= ((a * a) * a));
        (accum = float4((clamped_color * (float3)(w)), w));
        (revealage = clamped_alpha);
        (emissive_weight = (saturate((relative_emissive_luminance * WBOITSettings.additiveSensitivity)) / 8.0));
    };
    PSOutput main(VSOutput input)
    {
        PSOutput output;
        float4 finalColor = Shade(input.MatID, input.UV.xy, input.WorldPosition.xyz, normalize(input.Normal.xyz));
        float d = (input.Position.z / input.Position.w);
        float4 premultipliedColor = float4((finalColor.rgb * (float3)(finalColor.a)), finalColor.a);
        float emissiveLuminance = dot(finalColor.rgb, float3(0.21260000, 0.71520000, 0.072200000));
        (output.Revealage = (float4)(0.0));
        float revealage = 0.0;
        float emissiveWeight = 0.0;
        weighted_oit_process(output.Accumulation, revealage, emissiveWeight, premultipliedColor, emissiveLuminance, d, 1.0);
        output.Revealage.x = revealage;
        output.Revealage.w = emissiveWeight;
        return output;
    };

    Fragment_Shader(

#if USE_SHADOWS!=0
texture2d<float> VSM,sampler VSMSampler,
#if PT_USE_CAUSTICS!=0
texture2d<float> VSMRed,texture2d<float> VSMGreen,texture2d<float> VSMBlue,
#endif

#endif
constant Uniforms_LightUniformBlock & LightUniformBlock,constant Uniforms_CameraUniform & CameraUniform,constant Uniforms_MaterialUniform & MaterialUniform,constant texture2d<float, access::sample>* MaterialTextures,sampler LinearSampler,constant Uniforms_WBOITSettings & WBOITSettings) :

#if USE_SHADOWS!=0
VSM(VSM),VSMSampler(VSMSampler),
#if PT_USE_CAUSTICS!=0
VSMRed(VSMRed),VSMGreen(VSMGreen),VSMBlue(VSMBlue),
#endif

#endif
LightUniformBlock(LightUniformBlock),CameraUniform(CameraUniform),MaterialUniform(MaterialUniform),MaterialTextures(MaterialTextures),LinearSampler(LinearSampler),WBOITSettings(WBOITSettings) {}
};

struct FSData {
#if USE_SHADOWS!=0
    texture2d<float> VSM;
    sampler VSMSampler;
#if PT_USE_CAUSTICS!=0
    texture2d<float> VSMRed;
    texture2d<float> VSMGreen;
    texture2d<float> VSMBlue;
#endif
#endif
    texture2d<float, access::sample> MaterialTextures[MAX_NUM_TEXTURES];
    sampler LinearSampler;
};

struct FSDataPerFrame {
    constant Fragment_Shader::Uniforms_LightUniformBlock & LightUniformBlock;
    constant Fragment_Shader::Uniforms_CameraUniform & CameraUniform;
    constant Fragment_Shader::Uniforms_MaterialUniform & MaterialUniform;
    constant Fragment_Shader::Uniforms_WBOITSettings & WBOITSettings;
};

fragment Fragment_Shader::PSOutput stageMain(
    Fragment_Shader::VSOutput input [[stage_in]],
    constant FSData& fsData [[buffer(UPDATE_FREQ_NONE)]],
    constant FSDataPerFrame& fsDataPerFrame [[buffer(UPDATE_FREQ_PER_FRAME)]]
)
{
    Fragment_Shader::VSOutput input0;
    input0.Position = float4(input.Position.xyz, 1.0 / input.Position.w);
    input0.WorldPosition = input.WorldPosition;
    input0.Normal = input.Normal;
    input0.UV = input.UV;
    input0.MatID = input.MatID;
    Fragment_Shader main(
#if USE_SHADOWS!=0
    fsData.VSM,
    fsData.VSMSampler,
#if PT_USE_CAUSTICS!=0
    fsData.VSMRed,
    fsData.VSMGreen,
    fsData.VSMBlue,
#endif
#endif
    fsDataPerFrame.LightUniformBlock,
    fsDataPerFrame.CameraUniform,
    fsDataPerFrame.MaterialUniform,
    fsData.MaterialTextures,
    fsData.LinearSampler,
    fsDataPerFrame.WBOITSettings);
    return main.main(input0);
}
