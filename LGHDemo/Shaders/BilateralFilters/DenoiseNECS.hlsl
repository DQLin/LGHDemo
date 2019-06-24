#define RADIUS 1
#define GROUPSIZE 16
#define TILE_DIM 18

cbuffer Constants : register(b0)
{
	int tileWidth;
	int tileHeight;
}

Texture2D<float> NoiseEstimation : register(t0);
RWTexture2D<float> DenoisedNE: register(u1);

groupshared float cache[TILE_DIM * TILE_DIM];

SamplerState sampler0 : register(s0);

[numthreads(GROUPSIZE, GROUPSIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
	int curTileXMin = (DTid.x / tileWidth) * tileWidth;
	int curTileXMax = curTileXMin + tileWidth;
	int curTileYMin = (DTid.y / tileHeight) * tileHeight;
	int curTileYMax = curTileYMin + tileHeight;

	int2 tilePos = GTid.xy + RADIUS;

	if (GI < TILE_DIM*TILE_DIM / 2)
	{
		int2 coord = int2(Gid.xy)*GROUPSIZE - RADIUS + int2(GI % TILE_DIM, GI / TILE_DIM);
		int2 coord2 = coord + int2(0, TILE_DIM / 2);
		if (coord.x < curTileXMin) coord.x = (curTileXMin << 1) - coord.x; //mirrored
		if (coord.x >= curTileXMax) coord.x = (curTileXMax << 1) - coord.x;
		if (coord.y < curTileYMin) coord.y = (curTileYMin << 1) - coord.y; //mirrored
		if (coord.y >= curTileYMax) coord.y = (curTileYMax << 1) - coord.y;
		if (coord2.x < curTileXMin) coord2.x = (curTileXMin << 1) - coord2.x; //mirrored
		if (coord2.x >= curTileXMax) coord2.x = (curTileXMax << 1) - coord2.x;
		if (coord2.y < curTileYMin) coord2.y = (curTileYMin << 1) - coord2.y; //mirrored
		if (coord2.y >= curTileYMax) coord2.y = (curTileYMax << 1) - coord2.y;

		cache[GI] = NoiseEstimation[coord];
		cache[GI + TILE_DIM * TILE_DIM / 2] = NoiseEstimation[coord2];
	}

	GroupMemoryBarrierWithGroupSync();

	//a simple box filter
	float res = 0;
	for (int dy = -RADIUS; dy <= RADIUS; ++dy) {
		for (int dx = -RADIUS; dx <= RADIUS; ++dx) {
			int x = tilePos.x + dx;
			int y = tilePos.y + dy;
			res += cache[y*TILE_DIM + x];
		}
	}

	float factor = 2.0 * float(RADIUS) + 1.0;
	res *= 1.0 / (factor*factor);
	DenoisedNE[DTid.xy] = res;
}