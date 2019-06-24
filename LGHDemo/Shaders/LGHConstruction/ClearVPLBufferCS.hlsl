#include "DefaultBlockSize.hlsli"

RWStructuredBuffer<float4> levelAttribs[4] : register(u0);
RWStructuredBuffer<float> levelWeights : register(u4);

cbuffer CSConstants : register(b0)
{
	int numVerts;
};

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x < numVerts)
	{
		levelAttribs[0][DTid.x] = 0;
		levelAttribs[1][DTid.x] = 0;
		levelAttribs[2][DTid.x] = 0;
		levelAttribs[3][DTid.x] = 0;
		levelWeights[DTid.x] = 0;
	}
}