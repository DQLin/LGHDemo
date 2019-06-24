#include "ScanCS.hlsli"

[numthreads(groupthreads, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	uint x = Input[DTid.x];
	CSScan(DTid, GI, x);
}
