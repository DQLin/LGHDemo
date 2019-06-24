cbuffer IRTracingConstants : register(b0)
{
	uint currentVPL;
	float invNumPaths;
	float sceneRadius;
}

struct ShadowRayPayload
{
	float RayHitT;
};

RaytracingAccelerationStructure g_accel : register(t0);
Texture2D<float4> texPosition		: register(t1);
Texture2D<float4> texNormal		    : register(t2);
Texture2D<float4> texAlbedo			: register(t3);
RWStructuredBuffer<float4> g_vplPositions : register(u2);
RWStructuredBuffer<float4> g_vplNormals : register(u3);
RWStructuredBuffer<float4> g_vplColors : register(u4);
RWTexture2D<float4> resultBuffer : register(u1);

static const float FLT_MAX = asfloat(0x7F7FFFFF);
SamplerState sampler0 : register(s0);
