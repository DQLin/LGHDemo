#include "DefaultBlockSize.hlsli"

RWStructuredBuffer<float4> levelVplPositions : register(u0);
RWStructuredBuffer<float4> levelVplNormals : register(u1);
RWStructuredBuffer<float4> levelVplStdevs : register(u2);
StructuredBuffer<float> levelVplWeights : register(t0);

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	int vertId = DTid.x;
	float w = levelVplWeights[vertId];
	if (w > 0)
	{
		float3 p = levelVplPositions[vertId].xyz;
		float3 n = levelVplNormals[vertId].xyz;
		float3 e = levelVplStdevs[vertId].xyz;
		p /= w;
		n /= w;
		e = e / w - p * p;

		levelVplPositions[vertId] = float4(p, 0);
		levelVplNormals[vertId] = float4(n, 0);
		levelVplStdevs[vertId] = float4(e, 0);
	}
}