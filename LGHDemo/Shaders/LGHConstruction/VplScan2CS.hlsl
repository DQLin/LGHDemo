#include "ScanCS.hlsli"

cbuffer Constants : register(b0)
{
	uint stride;
}

// record and scan the sum of each bucket
[numthreads(groupthreads, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	// out of bound reads 0
	uint x = Input[DTid.x*stride - 1];   // Change the type of x here if scan other types
	CSScan(DTid, GI, x);
}
