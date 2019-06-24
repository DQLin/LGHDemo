StructuredBuffer<float4> levelAttribs[4] : register(t0);
RWStructuredBuffer<float4> instanceAttribs[4] : register(u0);

cbuffer CSConstants : register(b0)
{
	int levelOffset;
	int levelSize;
	int isLevelZero;
}

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x < levelSize)
	{
		int id = levelOffset + DTid.x;
		instanceAttribs[0][id] = levelAttribs[0][DTid.x];
		instanceAttribs[1][id] = levelAttribs[1][DTid.x];
		instanceAttribs[2][id] = levelAttribs[2][DTid.x];
		if (isLevelZero)
		{
			instanceAttribs[0][id].w = 0;
			instanceAttribs[3][id] = 0;
		}
		else
		{
			instanceAttribs[3][id] = levelAttribs[3][DTid.x];
		}
	}
}