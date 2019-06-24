StructuredBuffer<float4> vplAttribs[3] : register(t0);
ByteAddressBuffer keyIndexList : register(t3);
RWStructuredBuffer<float4> vplAttribsSorted[3] : register(u0);
RWStructuredBuffer<int> CellStartIdList : register(u3);
RWStructuredBuffer<int> CellEndIdList : register(u4);

cbuffer CSConstants : register(b0)
{
	int numVPLs;
}

[numthreads(512, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x < numVPLs)
	{
		uint2 KeyIndexPair = keyIndexList.Load2(8 * DTid.x);
		int index = KeyIndexPair.x;
		int key = KeyIndexPair.y;

		int PrevKey = DTid.x == 0 ? -1 : keyIndexList.Load(8 * DTid.x - 4);
		int NextKey = DTid.x == numVPLs - 1 ? 2000000000 : keyIndexList.Load(8 * DTid.x + 12);

		if (PrevKey < key)
		{
			CellStartIdList[key] = DTid.x;
		}

		if (NextKey > key)
		{
			CellEndIdList[key] = DTid.x;
		}

		vplAttribsSorted[0][DTid.x] = vplAttribs[0][index];
		vplAttribsSorted[1][DTid.x] = vplAttribs[1][index];
		vplAttribsSorted[2][DTid.x] = vplAttribs[2][index];
	}
}