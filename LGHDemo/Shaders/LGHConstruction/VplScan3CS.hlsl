#include "ScanCS.hlsli"

cbuffer Constants : register(b0)
{
	uint stride;
}

// add the bucket scanned result to each bucket to get the final result
[numthreads(groupthreads, 1, 1)]
void main(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	Result[DTid.x] = Result[DTid.x] + Input[DTid.x / stride];
}