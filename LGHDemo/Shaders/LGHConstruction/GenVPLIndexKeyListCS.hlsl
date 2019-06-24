#include "DefaultBlockSize.hlsli"

StructuredBuffer<float4> vplPositions : register(t0);
RWByteAddressBuffer keyIndexList : register(u0);

cbuffer CSConstants : register(b0)
{
	int numVpls;
	int numCells1D;
	float cellSize;
	int finestLevel;
	float3 corner;
};

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x < numVpls)
	{
		//find cell
		float3 normPos = (vplPositions[DTid.x].xyz - corner) / cellSize;
		int3 cellId = int3(normPos);
		
		//linear order
		int key = cellId.x + cellId.y * numCells1D + numCells1D * numCells1D * cellId.z;


		uint2 KeyIndexPair = uint2(DTid.x, key);
		keyIndexList.Store2(8*DTid.x, KeyIndexPair);
	}
}