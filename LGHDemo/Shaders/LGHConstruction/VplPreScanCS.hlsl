#include "DefaultBlockSize.hlsli"

StructuredBuffer<float> levelWeights : register(t0);
RWStructuredBuffer<uint> preAddressBuffer : register(u0);
[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (levelWeights[DTid.x] > 0) preAddressBuffer[DTid.x] = 1;
	else preAddressBuffer[DTid.x] = 0;
}