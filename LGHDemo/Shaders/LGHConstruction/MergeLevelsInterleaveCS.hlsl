StructuredBuffer<float4> levelAttribs[4] : register(t0);
StructuredBuffer<int> levelOffsetOfTile : register(t4);
RWStructuredBuffer<float4> instanceAttribs[4] : register(u0);

cbuffer CSConstants : register(b0)
{
	int baseTileLevelSize;
	int baseOffset;
	int level;
	int levelSize;
	int numLevels;
	int numTiles;
}

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x < levelSize)
	{
		int firstBaseTile = baseOffset / (baseTileLevelSize + 1);
		int tileId = DTid.x < baseOffset ? DTid.x / (baseTileLevelSize + 1) : firstBaseTile + (DTid.x - baseOffset) / baseTileLevelSize;
		int tileOffset = tileId < firstBaseTile ?
			tileId * (baseTileLevelSize + 1) :
			firstBaseTile * (baseTileLevelSize + 1) + (tileId - firstBaseTile) * baseTileLevelSize;
		int tileThreadId = DTid.x - tileOffset;

		int id = levelOffsetOfTile[tileId * numLevels + level] + tileThreadId;
		int srcId = numTiles * tileThreadId + tileId;

		instanceAttribs[0][id] = levelAttribs[0][srcId];
		instanceAttribs[1][id] = levelAttribs[1][srcId];
		instanceAttribs[2][id] = levelAttribs[2][srcId];
		if (level == 0) {
			instanceAttribs[0][id].w = 0;
			instanceAttribs[3][id] = 0; 
		}
		else instanceAttribs[3][id] = levelAttribs[3][srcId];

	}
}
