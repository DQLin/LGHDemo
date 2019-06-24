#include "DefaultBlockSize.hlsli"

#define POSITION 0
#define NORMAL 1
#define COLOR 2
#define STDEV 3
#define PI 3.141592654
StructuredBuffer<float4> levelAttribs[4] : register(t0);
StructuredBuffer<float> levelWeights : register(t4);
StructuredBuffer<uint> levelAddress : register(t5);
RWStructuredBuffer<float4> levelAttribsCompact[4] : register(u0);

cbuffer CSConstants : register(b0)
{
	int level;
}

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float weight = levelWeights[DTid.x];
	int addr = levelAddress[DTid.x] - 1;
	if (weight > 0)
	{
		float3 avgPos = levelAttribs[POSITION][DTid.x].xyz / weight;
		levelAttribsCompact[POSITION][addr] = float4(avgPos, level);
		levelAttribsCompact[NORMAL][addr] = levelAttribs[NORMAL][DTid.x] / weight;
		levelAttribsCompact[COLOR][addr] = levelAttribs[COLOR][DTid.x];
		levelAttribsCompact[STDEV][addr] = float4(PI * sqrt(abs(levelAttribs[STDEV][DTid.x].xyz / weight - avgPos * avgPos)), weight);
	}
}