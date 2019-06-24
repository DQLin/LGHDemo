//TILE_DIM must be 2x RADIUS + groupsize
#define RADIUS 10
#define GROUPSIZE 16
#define TILE_DIM 36
#define PI 3.141592654

cbuffer Constants : register(b0)
{
	int tileWidth;
	int tileHeight;
}

Texture2D<float3> ShadowedStochastic : register(t2);
Texture2D<float3> UnshadowedStochastic : register(t3);
RWTexture2D<float> NoiseEstimation : register(u0);

groupshared float3 SGroup[TILE_DIM * TILE_DIM];
groupshared float3 UGroup[TILE_DIM * TILE_DIM];

float3 Tap(float2 pos2D)
{
	int pos1D = int(pos2D.y) * TILE_DIM + int(pos2D.x);
	float3 n = SGroup[pos1D];
	float3 d = UGroup[pos1D];

	float3 res;
	for (int i = 0; i < 3; ++i) {
		res[i] = (d[i] < 0.000001) ? 1.0 : (n[i] / d[i]);
	}
	return res;
}

float hash(float2 st) {
	return frac(1.0e4 * sin(17.0*st.x + 0.1*st.y) *
		(0.1 + abs(sin(13.0*st.y + st.x)))
	);
}
 
[numthreads(GROUPSIZE, GROUPSIZE, 1)]
void main( uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
	int curTileXMin = (DTid.x / tileWidth) * tileWidth;
	int curTileXMax = curTileXMin + tileWidth;
	int curTileYMin = (DTid.y / tileHeight) * tileHeight;
	int curTileYMax = curTileYMin + tileHeight;

	if (GI < TILE_DIM*TILE_DIM / 4)
	{
		int2 anchor = int2(Gid.xy)*GROUPSIZE - RADIUS;
		int2 coord = anchor + int2(GI % TILE_DIM, GI / TILE_DIM);
		int2 coord2 = anchor + int2((GI + TILE_DIM * TILE_DIM / 4) % TILE_DIM, (GI + TILE_DIM * TILE_DIM / 4) / TILE_DIM);
		int2 coord3 = anchor + int2((GI + TILE_DIM * TILE_DIM / 2) % TILE_DIM, (GI + TILE_DIM * TILE_DIM / 2) / TILE_DIM);
		int2 coord4 = anchor + int2((GI + TILE_DIM * TILE_DIM * 3 / 4) % TILE_DIM, (GI + TILE_DIM * TILE_DIM * 3 / 4) / TILE_DIM);

		if (coord.x < curTileXMin) coord.x = (curTileXMin << 1) - coord.x; //mirrored
		if (coord.x >= curTileXMax) coord.x = (curTileXMax << 1) - coord.x;
		if (coord.y < curTileYMin) coord.y = (curTileYMin << 1) - coord.y; //mirrored
		if (coord.y >= curTileYMax) coord.y = (curTileYMax << 1) - coord.y;
		if (coord2.x < curTileXMin) coord2.x = (curTileXMin << 1) - coord2.x; //mirrored
		if (coord2.x >= curTileXMax) coord2.x = (curTileXMax << 1) - coord2.x;
		if (coord2.y < curTileYMin) coord2.y = (curTileYMin << 1) - coord2.y; //mirrored
		if (coord2.y >= curTileYMax) coord2.y = (curTileYMax << 1) - coord2.y;
		if (coord3.x < curTileXMin) coord3.x = (curTileXMin << 1) - coord3.x; //mirrored
		if (coord3.x >= curTileXMax) coord3.x = (curTileXMax << 1) - coord3.x;
		if (coord3.y < curTileYMin) coord3.y = (curTileYMin << 1) - coord3.y; //mirrored
		if (coord3.y >= curTileYMax) coord3.y = (curTileYMax << 1) - coord3.y;
		if (coord4.x < curTileXMin) coord4.x = (curTileXMin << 1) - coord4.x; //mirrored
		if (coord4.x >= curTileXMax) coord4.x = (curTileXMax << 1) - coord4.x;
		if (coord4.y < curTileYMin) coord4.y = (curTileYMin << 1) - coord4.y; //mirrored
		if (coord4.y >= curTileYMax) coord4.y = (curTileYMax << 1) - coord4.y;

		SGroup[GI] = ShadowedStochastic[coord];
		SGroup[GI + TILE_DIM * TILE_DIM / 4] = ShadowedStochastic[coord2];
		SGroup[GI + TILE_DIM * TILE_DIM / 2] = ShadowedStochastic[coord3];
		SGroup[GI + TILE_DIM * TILE_DIM * 3 / 4] = ShadowedStochastic[coord4];

		UGroup[GI] = UnshadowedStochastic[coord];
		UGroup[GI + TILE_DIM * TILE_DIM / 4] = UnshadowedStochastic[coord2];
		UGroup[GI + TILE_DIM * TILE_DIM / 2] = UnshadowedStochastic[coord3];
		UGroup[GI + TILE_DIM * TILE_DIM * 3 / 4] = UnshadowedStochastic[coord4];
	}

	GroupMemoryBarrierWithGroupSync();
	
	// pick a random line intergration direction
	float angle = 2 * PI * hash(DTid.xy + 0.5);

	const int N = 4; //number of line integrals
	float result = 0.0;

	float2 tilePos = GTid.xy + RADIUS + 0.5;

	for (int t = 0; t < N; ++t, angle += PI / float(N))
	{
		float2 axis = float2(cos(angle), sin(angle));
		float3 v2 = Tap(tilePos - RADIUS * axis);
		float3 v1 = Tap(tilePos + (1 - RADIUS) * axis);
		float d2mag = 0.0;
		// The first two points are accounted for above
		for (int r = -RADIUS + 2; r <= RADIUS; ++r) {
			float3 v0 = Tap(tilePos + axis * r);
			float3 d2 = v2 - 2.0*v1 + v0;
			d2mag += length(d2);
			v2 = v1, v1 = v0;
		}
		result = max(result, saturate(sqrt(d2mag / RADIUS)));
	}

	NoiseEstimation[DTid.xy] = result;
}